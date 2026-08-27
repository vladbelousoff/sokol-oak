/*
 * Dear ImGui, through cimgui and sokol_imgui.h.
 *
 * ImGui is optional: a build without -Dimgui=enabled still registers this
 * whole module, because the .oak stub declares these functions unconditionally
 * and Oak refuses to load a package whose stub declares a function the library
 * does not implement. What changes is the body. With SOKOL_OAK_IMGUI undefined
 * every binding becomes the no-op below `available()` returns false for, and a
 * program that calls them gets its own values back rather than an error.
 *
 * The value-editing widgets take the current value and return the new one.
 * That is not a translation of ImGui's bool-returning, pointer-taking style so
 * much as the only shape available: Oak has no out-parameters, and an
 * immediate-mode widget has to communicate the edited value somehow.
 */
#include "sokol_oak.h"

#include "sokol_app.h"

#ifdef SOKOL_OAK_IMGUI
/* cimgui only exposes its C-callable declarations behind this define;
 * without it the header falls back to the C++ ones. */
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_imgui.h"
#endif

/* Defined in sokol_oak_app.c: the sapp_event the event callback is currently
 * delivering, which is what simgui_handle_event needs and an Oak Event record
 * cannot carry. */
const sapp_event* oaks_app_current_event(oak_value_t rec);

/*
 * Without ImGui the module is still fully bound, so each binding needs a body
 * that does nothing and answers with something harmless. Writing thirty of
 * those by hand would be thirty chances to return the wrong thing, so the
 * bodies are generated from the same macros in both configurations and only
 * the payload differs.
 */
#ifdef SOKOL_OAK_IMGUI
#define OAKS_UI_IF(with, without) with
#else
#define OAKS_UI_IF(with, without) without
#endif

static oak_fn_call_result_t oaks_ui_available(oak_native_call_t* call,
                                             const oak_value_t* args,
                                             const usize argc, oak_value_t* out)
{
  (void)call; (void)args; (void)argc;
  *out = OAK_VALUE_BOOL(OAKS_UI_IF(1, 0));
  return OAK_FN_CALL_OK;
}

/* ------------------------------------------------------------ lifecycle -- */

static oak_fn_call_result_t oaks_ui_setup(oak_native_call_t* call,
                                         const oak_value_t* args,
                                         const usize argc, oak_value_t* out)
{
  (void)call; (void)argc; (void)out;
#ifdef SOKOL_OAK_IMGUI
  simgui_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  desc.max_vertices = oaks_field_int(args[0], "max_vertices", 0);
  desc.no_default_font = oaks_field_bool(args[0], "no_default_font", 0);
  desc.logger.func = slog_func;
  simgui_setup(&desc);
#else
  (void)args;
#endif
  return OAK_FN_CALL_OK;
}

/* Every remaining no-argument, no-result entry point is this shape. */
#define OAKS_UI_VOID(fn_name, body)                                             \
  static oak_fn_call_result_t fn_name(oak_native_call_t* call,                 \
                                      const oak_value_t* args,                 \
                                      const usize argc, oak_value_t* out)      \
  {                                                                            \
    (void)call;                                                                \
    (void)args;                                                                \
    (void)argc;                                                                \
    (void)out;                                                                 \
    OAKS_UI_IF(body, (void)0);                                                  \
    return OAK_FN_CALL_OK;                                                     \
  }

OAKS_UI_VOID(oaks_ui_shutdown, simgui_shutdown())
OAKS_UI_VOID(oaks_ui_end, igEnd())
OAKS_UI_VOID(oaks_ui_tree_pop, igTreePop())
OAKS_UI_VOID(oaks_ui_end_child, igEndChild())
OAKS_UI_VOID(oaks_ui_separator, igSeparator())
OAKS_UI_VOID(oaks_ui_same_line, igSameLine(0.0f, -1.0f))
OAKS_UI_VOID(oaks_ui_spacing, igSpacing())
OAKS_UI_VOID(oaks_ui_new_line, igNewLine())
OAKS_UI_VOID(oaks_ui_indent, igIndent(0.0f))
OAKS_UI_VOID(oaks_ui_unindent, igUnindent(0.0f))
OAKS_UI_VOID(oaks_ui_pop_id, igPopID())

static oak_fn_call_result_t oaks_ui_new_frame(oak_native_call_t* call,
                                             const oak_value_t* args,
                                             const usize argc, oak_value_t* out)
{
  (void)call; (void)args; (void)argc; (void)out;
#ifdef SOKOL_OAK_IMGUI
  /* sokol_imgui needs the current framebuffer size and frame time itself;
   * taking them from sokol_app here is what keeps `new_frame()` argument-free
   * on the Oak side. */
  simgui_frame_desc_t d;
  memset(&d, 0, sizeof(d));
  d.width = sapp_width();
  d.height = sapp_height();
  d.delta_time = sapp_frame_duration();
  d.dpi_scale = sapp_dpi_scale();
  simgui_new_frame(&d);
#endif
  return OAK_FN_CALL_OK;
}

OAKS_UI_VOID(oaks_ui_render, simgui_render())

static oak_fn_call_result_t oaks_ui_handle_event(oak_native_call_t* call,
                                                const oak_value_t* args,
                                                const usize argc,
                                                oak_value_t* out)
{
  (void)call; (void)argc;
#ifdef SOKOL_OAK_IMGUI
  const sapp_event* ev = oaks_app_current_event(args[0]);
  *out = OAK_VALUE_BOOL(ev ? simgui_handle_event(ev) : 0);
#else
  (void)args;
  *out = OAK_VALUE_BOOL(0);
#endif
  return OAK_FN_CALL_OK;
}

#define OAKS_UI_WANTS(fn_name, field)                                           \
  static oak_fn_call_result_t fn_name(oak_native_call_t* call,                 \
                                      const oak_value_t* args,                 \
                                      const usize argc, oak_value_t* out)      \
  {                                                                            \
    (void)call;                                                                \
    (void)args;                                                                \
    (void)argc;                                                                \
    *out = OAK_VALUE_BOOL(OAKS_UI_IF(igGetIO_Nil()->field, 0));                 \
    return OAK_FN_CALL_OK;                                                     \
  }

OAKS_UI_WANTS(oaks_ui_wants_mouse, WantCaptureMouse)
OAKS_UI_WANTS(oaks_ui_wants_keyboard, WantCaptureKeyboard)

static oak_fn_call_result_t oaks_ui_framerate(oak_native_call_t* call,
                                             const oak_value_t* args,
                                             const usize argc, oak_value_t* out)
{
  (void)call; (void)args; (void)argc;
  *out = OAK_VALUE_F32(OAKS_UI_IF(igGetIO_Nil()->Framerate, 0.0f));
  return OAK_FN_CALL_OK;
}

/* ---------------------------------------------------------------- widgets -- */

/* label -> bool. `without` is the value a build with no ImGui answers with,
 * which for every one of these is "the user did nothing". */
#define OAKS_UI_STR_BOOL(fn_name, expr)                                         \
  static oak_fn_call_result_t fn_name(oak_native_call_t* call,                 \
                                      const oak_value_t* args,                 \
                                      const usize argc, oak_value_t* out)      \
  {                                                                            \
    const char* label;                                                         \
    if (!oak_arg_cstring(call, args, argc, 0, &label))                         \
      return OAK_FN_CALL_RUNTIME_ERROR;                                        \
    (void)label;                                                               \
    *out = OAK_VALUE_BOOL(OAKS_UI_IF(expr, 0));                                 \
    return OAK_FN_CALL_OK;                                                     \
  }

OAKS_UI_STR_BOOL(oaks_ui_begin, igBegin(label, OAK_NULL, 0))
OAKS_UI_STR_BOOL(oaks_ui_button, igButton(label, (ImVec2_c){ 0, 0 }))
OAKS_UI_STR_BOOL(oaks_ui_small_button, igSmallButton(label))
OAKS_UI_STR_BOOL(oaks_ui_tree_node, igTreeNode_Str(label))

/* label -> nothing. */
#define OAKS_UI_STR_VOID(fn_name, expr)                                         \
  static oak_fn_call_result_t fn_name(oak_native_call_t* call,                 \
                                      const oak_value_t* args,                 \
                                      const usize argc, oak_value_t* out)      \
  {                                                                            \
    (void)out;                                                                 \
    const char* s;                                                             \
    if (!oak_arg_cstring(call, args, argc, 0, &s))                             \
      return OAK_FN_CALL_RUNTIME_ERROR;                                        \
    (void)s;                                                                   \
    OAKS_UI_IF(expr, (void)0);                                                  \
    return OAK_FN_CALL_OK;                                                     \
  }

/* "%s" rather than `s` as the format: an Oak string is data, and passing it
 * as a format string would make a stray %n in a label a bug in this file. */
OAKS_UI_STR_VOID(oaks_ui_text, igText("%s", s))
OAKS_UI_STR_VOID(oaks_ui_bullet_text, igBulletText("%s", s))
OAKS_UI_STR_VOID(oaks_ui_push_id, igPushID_Str(s))

static oak_fn_call_result_t oaks_ui_label_text(oak_native_call_t* call,
                                              const oak_value_t* args,
                                              const usize argc,
                                              oak_value_t* out)
{
  (void)out;
  const char* label;
  const char* s;
  if (!oak_arg_cstring(call, args, argc, 0, &label) ||
      !oak_arg_cstring(call, args, argc, 1, &s))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)label; (void)s;
  OAKS_UI_IF(igLabelText(label, "%s", s), (void)0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_ui_text_colored(oak_native_call_t* call,
                                                const oak_value_t* args,
                                                const usize argc,
                                                oak_value_t* out)
{
  (void)out;
  const char* s;
  float r, g, b, a;
  if (!oak_arg_cstring(call, args, argc, 0, &s) ||
      !oak_arg_number(call, args, argc, 1, &r) ||
      !oak_arg_number(call, args, argc, 2, &g) ||
      !oak_arg_number(call, args, argc, 3, &b) ||
      !oak_arg_number(call, args, argc, 4, &a))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)s; (void)r; (void)g; (void)b; (void)a;
  OAKS_UI_IF(igTextColored((ImVec4_c){ r, g, b, a }, "%s", s), (void)0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_ui_checkbox(oak_native_call_t* call,
                                            const oak_value_t* args,
                                            const usize argc, oak_value_t* out)
{
  const char* label;
  int value;
  if (!oak_arg_cstring(call, args, argc, 0, &label) ||
      !oak_arg_bool(call, args, argc, 1, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)label;
#ifdef SOKOL_OAK_IMGUI
  bool v = value ? true : false;
  igCheckbox(label, &v);
  value = v ? 1 : 0;
#endif
  *out = OAK_VALUE_BOOL(value);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_ui_radio_button(oak_native_call_t* call,
                                                const oak_value_t* args,
                                                const usize argc,
                                                oak_value_t* out)
{
  const char* label;
  int active;
  if (!oak_arg_cstring(call, args, argc, 0, &label) ||
      !oak_arg_bool(call, args, argc, 1, &active))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)label;
  *out = OAK_VALUE_BOOL(OAKS_UI_IF(igRadioButton_Bool(label, active != 0), 0));
  return OAK_FN_CALL_OK;
}

/*
 * The float editors are one shape: read a label and the current value, let
 * ImGui edit a local copy, hand the copy back. `call_expr` names the cimgui
 * call and uses `v` as the pointer, so the three differ by one line each.
 */
#define OAKS_UI_FLOAT_EDIT(fn_name, nextra, read_extra, call_expr)              \
  static oak_fn_call_result_t fn_name(oak_native_call_t* call,                 \
                                      const oak_value_t* args,                 \
                                      const usize argc, oak_value_t* out)      \
  {                                                                            \
    const char* label;                                                         \
    float value;                                                               \
    float e0 = 0.0f, e1 = 0.0f, e2 = 0.0f;                                     \
    (void)e0; (void)e1; (void)e2;                                              \
    if (!oak_arg_cstring(call, args, argc, 0, &label) ||                       \
        !oak_arg_number(call, args, argc, 1, &value))                          \
      return OAK_FN_CALL_RUNTIME_ERROR;                                        \
    read_extra;                                                                \
    (void)label;                                                               \
    (void)nextra;                                                              \
    OAKS_UI_IF(                                                                 \
        do {                                                                   \
          float* v = &value;                                                   \
          call_expr;                                                           \
        } while (0),                                                           \
        (void)0);                                                              \
    *out = OAK_VALUE_F32(value);                                               \
    return OAK_FN_CALL_OK;                                                     \
  }

OAKS_UI_FLOAT_EDIT(
    oaks_ui_slider_float, 2,
    (void)(!oak_arg_number(call, args, argc, 2, &e0) ||
           !oak_arg_number(call, args, argc, 3, &e1)),
    igSliderFloat(label, v, e0, e1, "%.3f", 0))

OAKS_UI_FLOAT_EDIT(
    oaks_ui_drag_float, 3,
    (void)(!oak_arg_number(call, args, argc, 2, &e0) ||
           !oak_arg_number(call, args, argc, 3, &e1) ||
           !oak_arg_number(call, args, argc, 4, &e2)),
    igDragFloat(label, v, e0, e1, e2, "%.3f", 0))

OAKS_UI_FLOAT_EDIT(oaks_ui_input_float, 0, (void)0,
                  igInputFloat(label, v, 0.0f, 0.0f, "%.3f", 0))

static oak_fn_call_result_t oaks_ui_slider_int(oak_native_call_t* call,
                                              const oak_value_t* args,
                                              const usize argc,
                                              oak_value_t* out)
{
  const char* label;
  int value, lo, hi;
  if (!oak_arg_cstring(call, args, argc, 0, &label) ||
      !oak_arg_i32(call, args, argc, 1, &value) ||
      !oak_arg_i32(call, args, argc, 2, &lo) ||
      !oak_arg_i32(call, args, argc, 3, &hi))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)label; (void)lo; (void)hi;
  OAKS_UI_IF(igSliderInt(label, &value, lo, hi, "%d", 0), (void)0);
  *out = OAK_VALUE_I32(value);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_ui_input_int(oak_native_call_t* call,
                                             const oak_value_t* args,
                                             const usize argc, oak_value_t* out)
{
  const char* label;
  int value;
  if (!oak_arg_cstring(call, args, argc, 0, &label) ||
      !oak_arg_i32(call, args, argc, 1, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)label;
  OAKS_UI_IF(igInputInt(label, &value, 1, 100, 0), (void)0);
  *out = OAK_VALUE_I32(value);
  return OAK_FN_CALL_OK;
}

/* Formats by what the number actually is rather than by a declared type:
 * Oak keeps whole numbers as i32 and fractional ones as f32, so a count shows
 * as `7` and a ratio as `0.25` without the caller choosing. */
static oak_fn_call_result_t oaks_ui_value(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc, oak_value_t* out)
{
  (void)out;
  const char* label;
  if (!oak_arg_cstring(call, args, argc, 0, &label))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (!oak_is_number(args[1]))
    return oak_native_error(call, "value: expected a number");
  (void)label;
#ifdef SOKOL_OAK_IMGUI
  if (oak_is_i32(args[1]))
    igLabelText(label, "%d", oak_as_i32(args[1]));
  else
    igLabelText(label, "%g", (double)oak_as_f32(args[1]));
#endif
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_ui_progress_bar(oak_native_call_t* call,
                                                const oak_value_t* args,
                                                const usize argc,
                                                oak_value_t* out)
{
  (void)out;
  float fraction;
  const char* overlay;
  if (!oak_arg_number(call, args, argc, 0, &fraction) ||
      !oak_arg_cstring(call, args, argc, 1, &overlay))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)fraction; (void)overlay;
  OAKS_UI_IF(igProgressBar(fraction, (ImVec2_c){ -1.0f, 0.0f },
                          overlay[0] ? overlay : OAK_NULL),
            (void)0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_ui_set_next_item_width(oak_native_call_t* call,
                                                       const oak_value_t* args,
                                                       const usize argc,
                                                       oak_value_t* out)
{
  (void)out;
  float w;
  if (!oak_arg_number(call, args, argc, 0, &w))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)w;
  OAKS_UI_IF(igSetNextItemWidth(w), (void)0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_ui_set_next_window_pos(oak_native_call_t* call,
                                                       const oak_value_t* args,
                                                       const usize argc,
                                                       oak_value_t* out)
{
  (void)out;
  float x, y;
  int cond;
  if (!oak_arg_number(call, args, argc, 0, &x) ||
      !oak_arg_number(call, args, argc, 1, &y) ||
      !oak_arg_i32(call, args, argc, 2, &cond))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)x; (void)y; (void)cond;
  OAKS_UI_IF(
      igSetNextWindowPos((ImVec2_c){ x, y }, cond, (ImVec2_c){ 0, 0 }),
      (void)0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_ui_set_next_window_size(oak_native_call_t* call,
                                                        const oak_value_t* args,
                                                        const usize argc,
                                                        oak_value_t* out)
{
  (void)out;
  float w, h;
  int cond;
  if (!oak_arg_number(call, args, argc, 0, &w) ||
      !oak_arg_number(call, args, argc, 1, &h) ||
      !oak_arg_i32(call, args, argc, 2, &cond))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)w; (void)h; (void)cond;
  OAKS_UI_IF(igSetNextWindowSize((ImVec2_c){ w, h }, cond), (void)0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_ui_begin_child(oak_native_call_t* call,
                                               const oak_value_t* args,
                                               const usize argc,
                                               oak_value_t* out)
{
  const char* id;
  float w, h;
  int border;
  if (!oak_arg_cstring(call, args, argc, 0, &id) ||
      !oak_arg_number(call, args, argc, 1, &w) ||
      !oak_arg_number(call, args, argc, 2, &h) ||
      !oak_arg_bool(call, args, argc, 3, &border))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)id; (void)w; (void)h; (void)border;
  *out = OAK_VALUE_BOOL(
      OAKS_UI_IF(igBeginChild_Str(id, (ImVec2_c){ w, h }, border ? 1 : 0, 0), 0));
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_ui_show_demo_window(oak_native_call_t* call,
                                                    const oak_value_t* args,
                                                    const usize argc,
                                                    oak_value_t* out)
{
  int open;
  if (!oak_arg_bool(call, args, argc, 0, &open))
    return OAK_FN_CALL_RUNTIME_ERROR;
#ifdef SOKOL_OAK_IMGUI
  if (open)
  {
    bool o = true;
    igShowDemoWindow(&o);
    open = o ? 1 : 0;
  }
#endif
  *out = OAK_VALUE_BOOL(open);
  return OAK_FN_CALL_OK;
}

/* ------------------------------------------------------------- binding -- */

int oaks_bind_imgui(oak_compile_options_t* opts, oak_bind_module_t* m)
{
  static const oak_bind_enum_variant_t conds[] = {
    { "None", 0 },
    { "Always", 1 << 0 },
    { "Once", 1 << 1 },
    { "FirstUseEver", 1 << 2 },
    { "Appearing", 1 << 3 },
  };
  if (OAKS_ENUM(opts, m, "Cond", conds) != 0)
    return -1;

  const oak_bind_type_ref_t p_any[] = { OAKS_ANY };
  const oak_bind_type_ref_t p_str[] = { OAKS_STR };
  const oak_bind_type_ref_t p_bool[] = { OAKS_BOOL };
  const oak_bind_type_ref_t p_num[] = { OAKS_NUM };
  const oak_bind_type_ref_t p_str_str[] = { OAKS_STR, OAKS_STR };
  const oak_bind_type_ref_t p_str_bool[] = { OAKS_STR, OAKS_BOOL };
  const oak_bind_type_ref_t p_str_num[] = { OAKS_STR, OAKS_NUM };
  const oak_bind_type_ref_t p_num_str[] = { OAKS_NUM, OAKS_STR };
  const oak_bind_type_ref_t p_num2_cond[] = { OAKS_NUM, OAKS_NUM, OAKS_NUM };
  const oak_bind_type_ref_t p_slider[] = { OAKS_STR, OAKS_NUM, OAKS_NUM,
                                                  OAKS_NUM };
  const oak_bind_type_ref_t p_drag[] = { OAKS_STR, OAKS_NUM, OAKS_NUM,
                                                OAKS_NUM, OAKS_NUM };
  const oak_bind_type_ref_t p_colored[] = { OAKS_STR, OAKS_NUM, OAKS_NUM,
                                                   OAKS_NUM, OAKS_NUM };
  const oak_bind_type_ref_t p_child[] = { OAKS_STR, OAKS_NUM, OAKS_NUM,
                                                 OAKS_BOOL };

  if (oaks_fn(opts, m, "available", oaks_ui_available, OAKS_BOOL, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "setup", oaks_ui_setup, OAKS_VOID, p_any, 1) != 0 ||
      oaks_fn(opts, m, "shutdown", oaks_ui_shutdown, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "new_frame", oaks_ui_new_frame, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "render", oaks_ui_render, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "handle_event", oaks_ui_handle_event, OAKS_BOOL, p_any, 1) != 0 ||
      oaks_fn(opts, m, "wants_mouse", oaks_ui_wants_mouse, OAKS_BOOL, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "wants_keyboard", oaks_ui_wants_keyboard, OAKS_BOOL, OAK_NULL, 0) != 0)
    return -1;

  if (oaks_fn(opts, m, "begin", oaks_ui_begin, OAKS_BOOL, p_str, 1) != 0 ||
      oaks_fn(opts, m, "end", oaks_ui_end, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "set_next_window_pos", oaks_ui_set_next_window_pos, OAKS_VOID, p_num2_cond, 3) != 0 ||
      oaks_fn(opts, m, "set_next_window_size", oaks_ui_set_next_window_size, OAKS_VOID, p_num2_cond, 3) != 0 ||
      oaks_fn(opts, m, "tree_node", oaks_ui_tree_node, OAKS_BOOL, p_str, 1) != 0 ||
      oaks_fn(opts, m, "tree_pop", oaks_ui_tree_pop, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "begin_child", oaks_ui_begin_child, OAKS_BOOL, p_child, 4) != 0 ||
      oaks_fn(opts, m, "end_child", oaks_ui_end_child, OAKS_VOID, OAK_NULL, 0) != 0)
    return -1;

  if (oaks_fn(opts, m, "text", oaks_ui_text, OAKS_VOID, p_str, 1) != 0 ||
      oaks_fn(opts, m, "text_colored", oaks_ui_text_colored, OAKS_VOID, p_colored, 5) != 0 ||
      oaks_fn(opts, m, "bullet_text", oaks_ui_bullet_text, OAKS_VOID, p_str, 1) != 0 ||
      oaks_fn(opts, m, "label_text", oaks_ui_label_text, OAKS_VOID, p_str_str, 2) != 0 ||
      oaks_fn(opts, m, "button", oaks_ui_button, OAKS_BOOL, p_str, 1) != 0 ||
      oaks_fn(opts, m, "small_button", oaks_ui_small_button, OAKS_BOOL, p_str, 1) != 0 ||
      oaks_fn(opts, m, "checkbox", oaks_ui_checkbox, OAKS_BOOL, p_str_bool, 2) != 0 ||
      oaks_fn(opts, m, "radio_button", oaks_ui_radio_button, OAKS_BOOL, p_str_bool, 2) != 0 ||
      oaks_fn(opts, m, "slider_float", oaks_ui_slider_float, OAKS_NUM, p_slider, 4) != 0 ||
      oaks_fn(opts, m, "slider_int", oaks_ui_slider_int, OAKS_NUM, p_slider, 4) != 0 ||
      oaks_fn(opts, m, "drag_float", oaks_ui_drag_float, OAKS_NUM, p_drag, 5) != 0 ||
      oaks_fn(opts, m, "input_float", oaks_ui_input_float, OAKS_NUM, p_str_num, 2) != 0 ||
      oaks_fn(opts, m, "input_int", oaks_ui_input_int, OAKS_NUM, p_str_num, 2) != 0 ||
      oaks_fn(opts, m, "value", oaks_ui_value, OAKS_VOID, p_str_num, 2) != 0 ||
      oaks_fn(opts, m, "progress_bar", oaks_ui_progress_bar, OAKS_VOID, p_num_str, 2) != 0)
    return -1;

  if (oaks_fn(opts, m, "separator", oaks_ui_separator, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "same_line", oaks_ui_same_line, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "spacing", oaks_ui_spacing, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "new_line", oaks_ui_new_line, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "indent", oaks_ui_indent, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "unindent", oaks_ui_unindent, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "push_id", oaks_ui_push_id, OAKS_VOID, p_str, 1) != 0 ||
      oaks_fn(opts, m, "pop_id", oaks_ui_pop_id, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "set_next_item_width", oaks_ui_set_next_item_width, OAKS_VOID, p_num, 1) != 0)
    return -1;

  if (oaks_fn(opts, m, "framerate", oaks_ui_framerate, OAKS_NUM, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "show_demo_window", oaks_ui_show_demo_window, OAKS_BOOL, p_bool, 1) != 0)
    return -1;

  return 0;
}
