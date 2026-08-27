/*
 * sokol_imgui.h's implementation, kept apart from sokol_oak_impl.c because it
 * is conditional: a build without ImGui compiles neither this file nor
 * cimgui.
 *
 * sokol_imgui.h can be compiled as C++ against imgui.h or as C against
 * cimgui.h. This is C, so cimgui.h comes first and sokol_imgui.h calls the
 * `ig`-prefixed bindings -- which is also why the Oak-facing widget layer in
 * sokol_oak_imgui.c can be plain C.
 */
#ifdef SOKOL_OAK_IMGUI

/* cimgui only exposes its C-callable declarations behind this define;
 * without it the header falls back to the C++ ones. */
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"

#define SOKOL_IMGUI_IMPL
#include "sokol_imgui.h"

#endif
