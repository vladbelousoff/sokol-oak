/*
 * The plugin entry point.
 *
 * Oak looks up one symbol in the shared library, checks the handshake in the
 * struct it returns, and calls `bind`. Everything the package exports is
 * registered from there, in four calls that mirror the four .oak modules.
 *
 * The module names are dotted because the package's own namespace is `sokol`
 * and its submodules live under it: src/sokol/gfx.oak is reached as
 * `sokol.gfx`, and a native module is matched to a source module by exactly
 * that dotted name.
 */
#include "sokol_oak.h"

#include "oak_plugin.h"
#include "oak_version.h"

static int oaks_bind(oak_compile_options_t* opts)
{
  oak_bind_module_t* app = oak_bind_module(opts, "sokol");
  oak_bind_module_t* gfx = oak_bind_module(opts, "sokol.gfx");
  oak_bind_module_t* time_ = oak_bind_module(opts, "sokol.time");
  oak_bind_module_t* imgui = oak_bind_module(opts, "sokol.imgui");
  if (!app || !gfx || !time_ || !imgui)
    return -1;

  /* A partial registration is worse than none: the loader would accept the
   * plugin and every stub behind the module that failed would report itself
   * as unimplemented, one error per function. Failing the whole bind names
   * the real problem once. */
  if (oaks_bind_app(opts, app) != 0)
    return -1;
  if (oaks_bind_gfx(opts, gfx) != 0)
    return -1;
  if (oaks_bind_time(opts, time_) != 0)
    return -1;
  if (oaks_bind_imgui(opts, imgui) != 0)
    return -1;

  return 0;
}

/* The version string here is the package's, and has to match oak.json --
 * the loader checks the name against the package that shipped the library,
 * and reports the version in diagnostics. */
static const oak_plugin_t oaks_plugin = {
  OAK_PLUGIN_ABI, "vladbelousoff/sokol-oak", "0.1.0", OAK_VERSION_STRING,
  oaks_bind,
};

OAK_PLUGIN_EXPORT const oak_plugin_t* oak_plugin_main(void)
{
  return &oaks_plugin;
}
