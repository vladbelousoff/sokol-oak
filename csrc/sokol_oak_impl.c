/*
 * The one translation unit that instantiates sokol.
 *
 * sokol's headers are single-file libraries: including one normally gives you
 * declarations, and defining SOKOL_IMPL first gives you the implementation
 * too. That must happen in exactly one translation unit, which is this one.
 * Every other file here includes the same headers without SOKOL_IMPL and gets
 * only the declarations.
 *
 * The backend defines are chosen by the build (meson.build picks one per
 * platform) rather than here, because sokol_gfx.h, sokol_app.h and
 * sokol_imgui.h all have to agree on it and the build is the only place that
 * sees all three.
 */
#define SOKOL_IMPL

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
