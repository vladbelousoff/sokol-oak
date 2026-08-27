/*
 * sokol_app.h -- the window, the frame loop and input.
 *
 * The inversion of control is the whole problem this file solves. sokol_app
 * owns the loop: you hand it callbacks and it calls them until the window
 * closes. Oak's VM, meanwhile, is in the middle of a native call to `run`
 * when that happens, so each callback has to re-enter the VM it is already
 * inside. oak_vm_call does exactly that -- it pushes a frame on the VM whose
 * stack already holds our own call -- which is why `run` can block for the
 * lifetime of the application without the VM being idle or reentrancy being
 * a problem.
 *
 * What that costs is the state below. A C callback receives only a void* of
 * our choosing, and sapp's callbacks do not even get that, so the VM and the
 * four Oak closures live in one file-scope struct for the duration of the
 * run.
 */
#include "sokol_oak.h"

#include "sokol_app.h"
#include "sokol_log.h"

#include <string.h>

typedef struct oaks_app
{
  oak_vm_t* vm;
  oak_allocator_t* allocator;
  /* The Oak closures from Desc, each holding a reference for as long as the
   * application runs. */
  oak_value_t init;
  oak_value_t frame;
  oak_value_t event;
  oak_value_t cleanup;
  /* The Desc itself, handed back to every callback.
   *
   * Oak has no closures and no module-level mutable state, so a callback can
   * reach nothing but its own arguments. Passing the Desc back is what lets
   * one exist at all: the program keeps its buffers, pipelines and counters
   * in the Desc's scratch arrays, and a callback declared `mut` may write
   * them. Without this a frame callback could not see what init created. */
  oak_value_t desc;
  int running;
} oaks_app_t;

static oaks_app_t g_app;

/* imgui.c needs to turn a sapp_event back into the pointer it came from, to
 * hand it to simgui_handle_event. Keeping the mapping here rather than
 * exposing sapp_event through the header keeps sokol_app.h out of every other
 * translation unit. */
const sapp_event* oaks_app_current_event(oak_value_t rec);

static void oaks_call(const oak_value_t fn,
                     const oak_value_t* args,
                     const usize argc)
{
  if (!g_app.vm)
    return;
  if (!oak_is_fn(fn) && !oak_is_native_fn(fn))
    return;
  /* The result is taken and dropped rather than discarded with a null
   * out_result. A callback returns nothing, so this is only ever a `none` --
   * but asking for it keeps the stack balanced on Oak builds that leave a
   * discarded result behind, and this runs once per frame, where one leaked
   * slot per call overflows the stack in seconds.
   *
   * A runtime error inside a callback has already been reported by the VM,
   * and there is nowhere to propagate it to: sokol_app is between us and the
   * Oak call that started the loop. Quitting is the honest response -- it
   * runs cleanup and unwinds back into `run` -- rather than calling the same
   * failing function again every frame for the life of the window. */
  oak_value_t result = OAK_VALUE_NONE;
  if (oak_vm_call(g_app.vm, fn, args, argc, &result) != OAK_VM_OK)
    sapp_quit();
  else
    oak_value_decref(result);
}

/* ---------------------------------------------------------- the Event -- */

/* Field order here is the order the fields are written below; the names are
 * the ones src/sokol.oak declares for the Event record. The two lists are the
 * contract between this file and that stub, so they are kept adjacent. */
static const char* const OAKS_EVENT_FIELDS[] = {
  "type",         "frame_count",  "key_code",          "char_code",
  "key_repeat",   "modifiers",    "mouse_button",      "mouse_x",
  "mouse_y",      "mouse_dx",     "mouse_dy",          "scroll_x",
  "scroll_y",     "window_width", "window_height",     "framebuffer_width",
  "framebuffer_height",
};

#define OAKS_EVENT_FIELD_COUNT                                                  \
  ((int)(sizeof(OAKS_EVENT_FIELDS) / sizeof(OAKS_EVENT_FIELDS[0])))

/* The sapp_event the current event callback is delivering. Valid only for the
 * duration of that callback, which is exactly as long as the Oak Event record
 * built from it is meant to describe anything. */
static const sapp_event* g_current_event;

const sapp_event* oaks_app_current_event(oak_value_t rec)
{
  /* The record is not consulted: only one event is in flight at a time, and
   * an Oak program can only reach handle_event from inside the callback that
   * set this. Taking the record anyway keeps the call site honest about what
   * it is asking for. */
  (void)rec;
  return g_current_event;
}

static oak_value_t oaks_event_record(const sapp_event* ev)
{
  oak_obj_record_t* r = oak_record_new(
      g_app.allocator, OAKS_EVENT_FIELD_COUNT, "Event", OAKS_EVENT_FIELDS);
  if (!r)
    return OAK_VALUE_NONE;

  int i = 0;
  r->fields[i++] = OAK_VALUE_I32((int)ev->type);
  r->fields[i++] = OAK_VALUE_I32((int)ev->frame_count);
  r->fields[i++] = OAK_VALUE_I32((int)ev->key_code);
  r->fields[i++] = OAK_VALUE_I32((int)ev->char_code);
  r->fields[i++] = OAK_VALUE_BOOL(ev->key_repeat);
  r->fields[i++] = OAK_VALUE_I32((int)ev->modifiers);
  r->fields[i++] = OAK_VALUE_I32((int)ev->mouse_button);
  r->fields[i++] = OAK_VALUE_F32(ev->mouse_x);
  r->fields[i++] = OAK_VALUE_F32(ev->mouse_y);
  r->fields[i++] = OAK_VALUE_F32(ev->mouse_dx);
  r->fields[i++] = OAK_VALUE_F32(ev->mouse_dy);
  r->fields[i++] = OAK_VALUE_F32(ev->scroll_x);
  r->fields[i++] = OAK_VALUE_F32(ev->scroll_y);
  r->fields[i++] = OAK_VALUE_I32(ev->window_width);
  r->fields[i++] = OAK_VALUE_I32(ev->window_height);
  r->fields[i++] = OAK_VALUE_I32(ev->framebuffer_width);
  r->fields[i++] = OAK_VALUE_I32(ev->framebuffer_height);

  return oak_value_obj(&r->obj);
}

/* ------------------------------------------------- sokol_app callbacks -- */

static void oaks_init_cb(void)
{
  oaks_call(g_app.init, &g_app.desc, 1);
}

static void oaks_frame_cb(void)
{
  oaks_call(g_app.frame, &g_app.desc, 1);
}

static void oaks_event_cb(const sapp_event* ev)
{
  if (!oak_is_fn(g_app.event) && !oak_is_native_fn(g_app.event))
    return;
  g_current_event = ev;
  const oak_value_t rec = oaks_event_record(ev);
  if (!oak_is_none(rec))
  {
    const oak_value_t args[2] = { g_app.desc, rec };
    oaks_call(g_app.event, args, 2);
    /* oaks_call does not take its arguments, so the reference minted by
     * oak_record_new is still ours to drop. */
    oak_value_decref(rec);
  }
  g_current_event = OAK_NULL;
}

static void oaks_cleanup_cb(void)
{
  oaks_call(g_app.cleanup, &g_app.desc, 1);

  oak_value_decref(g_app.desc);
  oak_value_decref(g_app.init);
  oak_value_decref(g_app.frame);
  oak_value_decref(g_app.event);
  oak_value_decref(g_app.cleanup);
  memset(&g_app, 0, sizeof(g_app));
}

/* ------------------------------------------------------------ bindings -- */

static oak_fn_call_result_t oaks_run(oak_native_call_t* call,
                                    const oak_value_t* args,
                                    const usize argc,
                                    oak_value_t* out)
{
  (void)argc;
  (void)out;
  if (!oak_is_record(args[0]))
    return oak_native_error(call, "run expects a Desc record");
  if (g_app.running)
    return oak_native_error(call, "run has already been called");

  const oak_value_t desc = args[0];

  memset(&g_app, 0, sizeof(g_app));
  g_app.vm = call->vm;
  g_app.allocator = call->allocator;
  g_app.running = 1;
  g_app.init = oaks_field(desc, "init");
  g_app.frame = oaks_field(desc, "frame");
  g_app.event = oaks_field(desc, "event");
  g_app.cleanup = oaks_field(desc, "cleanup");
  g_app.desc = desc;
  /* The Desc record is only borrowed for this call, so the closures it holds
   * have to be retained for as long as sokol_app might call them. */
  oak_value_incref(g_app.init);
  oak_value_incref(g_app.frame);
  oak_value_incref(g_app.event);
  oak_value_incref(g_app.cleanup);
  oak_value_incref(g_app.desc);

  sapp_desc d;
  memset(&d, 0, sizeof(d));
  d.init_cb = oaks_init_cb;
  d.frame_cb = oaks_frame_cb;
  d.event_cb = oaks_event_cb;
  d.cleanup_cb = oaks_cleanup_cb;
  d.width = oaks_field_int(desc, "width", 800);
  d.height = oaks_field_int(desc, "height", 600);
  d.sample_count = oaks_field_int(desc, "sample_count", 1);
  d.high_dpi = oaks_field_bool(desc, "high_dpi", 1);
  d.fullscreen = oaks_field_bool(desc, "fullscreen", 0);
  d.srgb = oaks_field_bool(desc, "srgb", 0);
  d.enable_clipboard = oaks_field_bool(desc, "enable_clipboard", 0);
  if (d.enable_clipboard)
    d.clipboard_size = 8192;
  d.window_title = oaks_field_str(desc, "title");
  if (!d.window_title)
    d.window_title = "sokol-oak";
  d.logger.func = slog_func;

  /* Blocks until the application quits, and on the web backend does not
   * return at all -- the browser drives the frame callback and this call
   * unwinds immediately. Either way the cleanup callback has already dropped
   * our references by the time control comes back. */
  sapp_run(&d);
  g_app.running = 0;
  return OAK_FN_CALL_OK;
}

/*
 * The queries below are all "no arguments, one scalar out" or "one scalar in,
 * nothing out", so each is a single line of body wrapped in the same six of
 * boilerplate. These macros generate them, which keeps the ratio of intent to
 * ceremony readable and makes the binding table below the real description of
 * the module.
 */
#define OAKS_GETTER(fn_name, expr, wrap)                                        \
  static oak_fn_call_result_t fn_name(oak_native_call_t* call,                 \
                                      const oak_value_t* args,                 \
                                      const usize argc, oak_value_t* out)      \
  {                                                                            \
    (void)call;                                                                \
    (void)args;                                                                \
    (void)argc;                                                                \
    *out = wrap(expr);                                                         \
    return OAK_FN_CALL_OK;                                                     \
  }

#define OAKS_ACTION(fn_name, stmt)                                              \
  static oak_fn_call_result_t fn_name(oak_native_call_t* call,                 \
                                      const oak_value_t* args,                 \
                                      const usize argc, oak_value_t* out)      \
  {                                                                            \
    (void)call;                                                                \
    (void)args;                                                                \
    (void)argc;                                                                \
    (void)out;                                                                 \
    stmt;                                                                      \
    return OAK_FN_CALL_OK;                                                     \
  }

OAKS_GETTER(oaks_width, sapp_width(), OAK_VALUE_I32)
OAKS_GETTER(oaks_height, sapp_height(), OAK_VALUE_I32)
OAKS_GETTER(oaks_dpi_scale, sapp_dpi_scale(), OAK_VALUE_F32)
OAKS_GETTER(oaks_frame_count, (int)sapp_frame_count(), OAK_VALUE_I32)
OAKS_GETTER(oaks_frame_duration, (float)sapp_frame_duration(), OAK_VALUE_F32)
OAKS_GETTER(oaks_is_fullscreen, sapp_is_fullscreen(), OAK_VALUE_BOOL)
OAKS_GETTER(oaks_mouse_shown, sapp_mouse_shown(), OAK_VALUE_BOOL)
OAKS_GETTER(oaks_mouse_locked, sapp_mouse_locked(), OAK_VALUE_BOOL)

OAKS_ACTION(oaks_request_quit, sapp_request_quit())
OAKS_ACTION(oaks_quit, sapp_quit())
OAKS_ACTION(oaks_toggle_fullscreen, sapp_toggle_fullscreen())

static oak_fn_call_result_t oaks_show_mouse(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           const usize argc, oak_value_t* out)
{
  (void)out;
  int shown;
  if (!oak_arg_bool(call, args, argc, 0, &shown))
    return OAK_FN_CALL_RUNTIME_ERROR;
  sapp_show_mouse(shown);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_lock_mouse(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           const usize argc, oak_value_t* out)
{
  (void)out;
  int locked;
  if (!oak_arg_bool(call, args, argc, 0, &locked))
    return OAK_FN_CALL_RUNTIME_ERROR;
  sapp_lock_mouse(locked);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_set_window_title(oak_native_call_t* call,
                                                 const oak_value_t* args,
                                                 const usize argc,
                                                 oak_value_t* out)
{
  (void)out;
  const char* title;
  if (!oak_arg_cstring(call, args, argc, 0, &title))
    return OAK_FN_CALL_RUNTIME_ERROR;
  sapp_set_window_title(title);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_set_clipboard(oak_native_call_t* call,
                                              const oak_value_t* args,
                                              const usize argc,
                                              oak_value_t* out)
{
  (void)out;
  const char* text;
  if (!oak_arg_cstring(call, args, argc, 0, &text))
    return OAK_FN_CALL_RUNTIME_ERROR;
  sapp_set_clipboard_string(text);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_get_clipboard(oak_native_call_t* call,
                                              const oak_value_t* args,
                                              const usize argc,
                                              oak_value_t* out)
{
  (void)args;
  (void)argc;
  const char* text = sapp_get_clipboard_string();
  *out = oak_vm_string_value(call->vm, text ? text : "");
  return OAK_FN_CALL_OK;
}

int oaks_bind_app(oak_compile_options_t* opts, oak_bind_module_t* m)
{
  static const oak_bind_enum_variant_t event_types[] = {
    { "Invalid", SAPP_EVENTTYPE_INVALID },
    { "KeyDown", SAPP_EVENTTYPE_KEY_DOWN },
    { "KeyUp", SAPP_EVENTTYPE_KEY_UP },
    { "Char", SAPP_EVENTTYPE_CHAR },
    { "MouseDown", SAPP_EVENTTYPE_MOUSE_DOWN },
    { "MouseUp", SAPP_EVENTTYPE_MOUSE_UP },
    { "MouseScroll", SAPP_EVENTTYPE_MOUSE_SCROLL },
    { "MouseMove", SAPP_EVENTTYPE_MOUSE_MOVE },
    { "MouseEnter", SAPP_EVENTTYPE_MOUSE_ENTER },
    { "MouseLeave", SAPP_EVENTTYPE_MOUSE_LEAVE },
    { "Resized", SAPP_EVENTTYPE_RESIZED },
    { "Iconified", SAPP_EVENTTYPE_ICONIFIED },
    { "Restored", SAPP_EVENTTYPE_RESTORED },
    { "Focused", SAPP_EVENTTYPE_FOCUSED },
    { "Unfocused", SAPP_EVENTTYPE_UNFOCUSED },
    { "Suspended", SAPP_EVENTTYPE_SUSPENDED },
    { "Resumed", SAPP_EVENTTYPE_RESUMED },
    { "QuitRequested", SAPP_EVENTTYPE_QUIT_REQUESTED },
    { "ClipboardPasted", SAPP_EVENTTYPE_CLIPBOARD_PASTED },
    { "FilesDropped", SAPP_EVENTTYPE_FILES_DROPPED },
  };
  static const oak_bind_enum_variant_t mouse_buttons[] = {
    { "Invalid", SAPP_MOUSEBUTTON_INVALID },
    { "Left", SAPP_MOUSEBUTTON_LEFT },
    { "Right", SAPP_MOUSEBUTTON_RIGHT },
    { "Middle", SAPP_MOUSEBUTTON_MIDDLE },
  };
  /* A bit mask rather than a sequence: `modifiers & Modifier.Shift` has to
   * mean what it says. */
  static const oak_bind_enum_variant_t modifiers[] = {
    { "Shift", SAPP_MODIFIER_SHIFT },
    { "Ctrl", SAPP_MODIFIER_CTRL },
    { "Alt", SAPP_MODIFIER_ALT },
    { "Super", SAPP_MODIFIER_SUPER },
    { "LeftMouse", SAPP_MODIFIER_LMB },
    { "RightMouse", SAPP_MODIFIER_RMB },
    { "MiddleMouse", SAPP_MODIFIER_MMB },
  };
  /* sokol's key codes follow the USB/GLFW numbering and are not contiguous. */
  static const oak_bind_enum_variant_t keys[] = {
    { "Invalid", SAPP_KEYCODE_INVALID },
    { "Space", SAPP_KEYCODE_SPACE },
    { "Apostrophe", SAPP_KEYCODE_APOSTROPHE },
    { "Comma", SAPP_KEYCODE_COMMA },
    { "Minus", SAPP_KEYCODE_MINUS },
    { "Period", SAPP_KEYCODE_PERIOD },
    { "Slash", SAPP_KEYCODE_SLASH },
    { "Num0", SAPP_KEYCODE_0 }, { "Num1", SAPP_KEYCODE_1 },
    { "Num2", SAPP_KEYCODE_2 }, { "Num3", SAPP_KEYCODE_3 },
    { "Num4", SAPP_KEYCODE_4 }, { "Num5", SAPP_KEYCODE_5 },
    { "Num6", SAPP_KEYCODE_6 }, { "Num7", SAPP_KEYCODE_7 },
    { "Num8", SAPP_KEYCODE_8 }, { "Num9", SAPP_KEYCODE_9 },
    { "Semicolon", SAPP_KEYCODE_SEMICOLON },
    { "Equal", SAPP_KEYCODE_EQUAL },
    { "A", SAPP_KEYCODE_A }, { "B", SAPP_KEYCODE_B },
    { "C", SAPP_KEYCODE_C }, { "D", SAPP_KEYCODE_D },
    { "E", SAPP_KEYCODE_E }, { "F", SAPP_KEYCODE_F },
    { "G", SAPP_KEYCODE_G }, { "H", SAPP_KEYCODE_H },
    { "I", SAPP_KEYCODE_I }, { "J", SAPP_KEYCODE_J },
    { "K", SAPP_KEYCODE_K }, { "L", SAPP_KEYCODE_L },
    { "M", SAPP_KEYCODE_M }, { "N", SAPP_KEYCODE_N },
    { "O", SAPP_KEYCODE_O }, { "P", SAPP_KEYCODE_P },
    { "Q", SAPP_KEYCODE_Q }, { "R", SAPP_KEYCODE_R },
    { "S", SAPP_KEYCODE_S }, { "T", SAPP_KEYCODE_T },
    { "U", SAPP_KEYCODE_U }, { "V", SAPP_KEYCODE_V },
    { "W", SAPP_KEYCODE_W }, { "X", SAPP_KEYCODE_X },
    { "Y", SAPP_KEYCODE_Y }, { "Z", SAPP_KEYCODE_Z },
    { "LeftBracket", SAPP_KEYCODE_LEFT_BRACKET },
    { "Backslash", SAPP_KEYCODE_BACKSLASH },
    { "RightBracket", SAPP_KEYCODE_RIGHT_BRACKET },
    { "GraveAccent", SAPP_KEYCODE_GRAVE_ACCENT },
    { "Escape", SAPP_KEYCODE_ESCAPE },
    { "Enter", SAPP_KEYCODE_ENTER },
    { "Tab", SAPP_KEYCODE_TAB },
    { "Backspace", SAPP_KEYCODE_BACKSPACE },
    { "Insert", SAPP_KEYCODE_INSERT },
    { "Delete", SAPP_KEYCODE_DELETE },
    { "Right", SAPP_KEYCODE_RIGHT }, { "Left", SAPP_KEYCODE_LEFT },
    { "Down", SAPP_KEYCODE_DOWN }, { "Up", SAPP_KEYCODE_UP },
    { "PageUp", SAPP_KEYCODE_PAGE_UP },
    { "PageDown", SAPP_KEYCODE_PAGE_DOWN },
    { "Home", SAPP_KEYCODE_HOME }, { "End", SAPP_KEYCODE_END },
    { "CapsLock", SAPP_KEYCODE_CAPS_LOCK },
    { "ScrollLock", SAPP_KEYCODE_SCROLL_LOCK },
    { "NumLock", SAPP_KEYCODE_NUM_LOCK },
    { "PrintScreen", SAPP_KEYCODE_PRINT_SCREEN },
    { "Pause", SAPP_KEYCODE_PAUSE },
    { "F1", SAPP_KEYCODE_F1 }, { "F2", SAPP_KEYCODE_F2 },
    { "F3", SAPP_KEYCODE_F3 }, { "F4", SAPP_KEYCODE_F4 },
    { "F5", SAPP_KEYCODE_F5 }, { "F6", SAPP_KEYCODE_F6 },
    { "F7", SAPP_KEYCODE_F7 }, { "F8", SAPP_KEYCODE_F8 },
    { "F9", SAPP_KEYCODE_F9 }, { "F10", SAPP_KEYCODE_F10 },
    { "F11", SAPP_KEYCODE_F11 }, { "F12", SAPP_KEYCODE_F12 },
    { "LeftShift", SAPP_KEYCODE_LEFT_SHIFT },
    { "LeftControl", SAPP_KEYCODE_LEFT_CONTROL },
    { "LeftAlt", SAPP_KEYCODE_LEFT_ALT },
    { "LeftSuper", SAPP_KEYCODE_LEFT_SUPER },
    { "RightShift", SAPP_KEYCODE_RIGHT_SHIFT },
    { "RightControl", SAPP_KEYCODE_RIGHT_CONTROL },
    { "RightAlt", SAPP_KEYCODE_RIGHT_ALT },
    { "RightSuper", SAPP_KEYCODE_RIGHT_SUPER },
    { "Menu", SAPP_KEYCODE_MENU },
  };

  if (OAKS_ENUM(opts, m, "EventType", event_types) != 0 ||
      OAKS_ENUM(opts, m, "MouseButton", mouse_buttons) != 0 ||
      OAKS_ENUM(opts, m, "Modifier", modifiers) != 0 ||
      OAKS_ENUM(opts, m, "Key", keys) != 0)
    return -1;

  const oak_bind_type_ref_t p_any[] = { OAKS_ANY };
  const oak_bind_type_ref_t p_bool[] = { OAKS_BOOL };
  const oak_bind_type_ref_t p_str[] = { OAKS_STR };

  if (oaks_fn(opts, m, "run", oaks_run, OAKS_VOID, p_any, 1) != 0 ||
      oaks_fn(opts, m, "request_quit", oaks_request_quit, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "quit", oaks_quit, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "width", oaks_width, OAKS_NUM, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "height", oaks_height, OAKS_NUM, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "dpi_scale", oaks_dpi_scale, OAKS_NUM, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "frame_count", oaks_frame_count, OAKS_NUM, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "frame_duration", oaks_frame_duration, OAKS_NUM, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "is_fullscreen", oaks_is_fullscreen, OAKS_BOOL, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "toggle_fullscreen", oaks_toggle_fullscreen, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "show_mouse", oaks_show_mouse, OAKS_VOID, p_bool, 1) != 0 ||
      oaks_fn(opts, m, "mouse_shown", oaks_mouse_shown, OAKS_BOOL, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "lock_mouse", oaks_lock_mouse, OAKS_VOID, p_bool, 1) != 0 ||
      oaks_fn(opts, m, "mouse_locked", oaks_mouse_locked, OAKS_BOOL, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "set_window_title", oaks_set_window_title, OAKS_VOID, p_str, 1) != 0 ||
      oaks_fn(opts, m, "set_clipboard", oaks_set_clipboard, OAKS_VOID, p_str, 1) != 0 ||
      oaks_fn(opts, m, "get_clipboard", oaks_get_clipboard, OAKS_STR, OAK_NULL, 0) != 0)
    return -1;

  return 0;
}
