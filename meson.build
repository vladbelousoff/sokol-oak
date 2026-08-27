# sokol-oak -- sokol bindings for the Oak scripting language.
#
# Builds one shared library, libsokol_oak, which Oak loads as a native package
# plugin. `meson install` puts it where oak.json says a native package keeps
# its binaries: native/<os>-<arch>/, the same string `oak --version` prints.

project(
  'sokol-oak',
  'c',
  version: '0.1.0',
  license: 'Zlib',
  default_options: [
    'c_std=c17',
    'warning_level=2',
    'default_library=shared',
  ],
)

cc = meson.get_compiler('c')
host_system = host_machine.system()

sokol_dir = meson.current_source_dir() / 'third_party' / 'sokol'
if not import('fs').is_file(sokol_dir / 'sokol_gfx.h')
  error(
    'third_party/sokol is empty -- run: git submodule update --init --recursive',
  )
endif

# -- backend ------------------------------------------------------------------
#
# sokol_gfx.h, sokol_app.h and sokol_imgui.h must all be compiled with the same
# backend define, so it is resolved once here and applied to every target.

backend = get_option('gfx_backend')
if backend == 'auto'
  if host_system == 'windows'
    backend = 'd3d11'
  elif host_system == 'darwin'
    backend = 'metal'
  elif host_system == 'emscripten' or host_system == 'android'
    backend = 'gles3'
  else
    backend = 'glcore'
  endif
endif

backend_defines = {
  'glcore': '-DSOKOL_GLCORE',
  'gles3': '-DSOKOL_GLES3',
  'd3d11': '-DSOKOL_D3D11',
  'metal': '-DSOKOL_METAL',
  'wgpu': '-DSOKOL_WGPU',
}
# SOKOL_NO_ENTRY: sokol_app.h otherwise defines main() and expects the program
# to supply sokol_main(). Here the entry point is Oak's, and sapp_run() is
# called from inside a native function, which is exactly the case this define
# exists for.
sokol_args = [backend_defines[backend], '-DSOKOL_NO_ENTRY']

# -- platform dependencies ----------------------------------------------------

deps = [cc.find_library('m', required: false)]

if host_system == 'linux' or host_system == 'freebsd' or host_system == 'netbsd'
  deps += [
    dependency('threads'),
    dependency('x11'),
    dependency('xi'),
    dependency('xcursor'),
    cc.find_library('dl', required: false),
  ]
  if backend == 'glcore'
    deps += dependency('gl')
  else
    deps += cc.find_library('EGL', required: false)
    deps += cc.find_library('GLESv2', required: false)
  endif
  # sokol_app.h wants ALSA even when sokol_audio.h is not compiled in, because
  # its Linux backend links the symbol unconditionally.
  deps += dependency('alsa', required: false)
elif host_system == 'windows'
  foreach lib : ['kernel32', 'user32', 'shell32', 'gdi32', 'dxgi', 'd3d11', 'ole32']
    deps += cc.find_library(lib, required: false)
  endforeach
elif host_system == 'darwin'
  deps += dependency(
    'appleframeworks',
    modules: ['Cocoa', 'QuartzCore', 'Metal', 'MetalKit', 'AudioToolbox'],
  )
endif

# -- oak ----------------------------------------------------------------------
#
# The plugin compiles against Oak's installed public headers and links acorn.
# pkg-config is how a plugin author is meant to find both; a source checkout
# can be pointed at with -Dpkg_config_path.

oak_dep = dependency('oak', required: true)

# -- Dear ImGui ---------------------------------------------------------------
#
# Optional. The sokol.imgui module is bound either way -- the .oak stub
# declares it unconditionally, so the library has to implement it -- but
# without ImGui every binding is a no-op and available() reports false.

cimgui_dir = meson.current_source_dir() / 'third_party' / 'cimgui'
imgui_opt = get_option('imgui')
have_imgui = false

if not imgui_opt.disabled()
  if import('fs').is_file(cimgui_dir / 'imgui' / 'imgui.cpp')
    have_imgui = true
  elif imgui_opt.enabled()
    error(
      'imgui is enabled but third_party/cimgui is empty -- run: git submodule update --init --recursive',
    )
  else
    message('cimgui submodule not present: building without ImGui support')
  endif
endif

imgui_lib = []
imgui_args = []
imgui_inc = []

if have_imgui
  # cimgui and Dear ImGui are C++, and are the only C++ in this project. They
  # go into a static library of their own so the C sources stay C.
  add_languages('cpp', native: false, required: true)
  imgui_inc = include_directories(
    'third_party/cimgui',
    'third_party/cimgui/imgui',
  )
  imgui_lib = static_library(
    'sokol_oak_imgui_cxx',
    files(
      'third_party/cimgui/cimgui.cpp',
      'third_party/cimgui/imgui/imgui.cpp',
      'third_party/cimgui/imgui/imgui_demo.cpp',
      'third_party/cimgui/imgui/imgui_draw.cpp',
      'third_party/cimgui/imgui/imgui_tables.cpp',
      'third_party/cimgui/imgui/imgui_widgets.cpp',
    ),
    include_directories: imgui_inc,
    cpp_args: ['-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS=1'],
    override_options: ['warning_level=0'],
    pic: true,
  )
  imgui_args = ['-DSOKOL_OAK_IMGUI']
endif

# -- the plugin ---------------------------------------------------------------

sources = files(
  'csrc/sokol_oak_app.c',
  'csrc/sokol_oak_gfx.c',
  'csrc/sokol_oak_imgui.c',
  'csrc/sokol_oak_imgui_impl.c',
  'csrc/sokol_oak_impl.c',
  'csrc/sokol_oak_plugin.c',
  'csrc/sokol_oak_time.c',
  'csrc/sokol_oak_util.c',
)

# The platform directory a native package keeps its binaries in is
# <os>-<arch>, matching what `oak --version` prints.
os_names = {
  'linux': 'linux',
  'windows': 'windows',
  'darwin': 'macos',
  'emscripten': 'wasm',
}
arch_names = {
  'x86_64': 'x86_64',
  'aarch64': 'aarch64',
  'arm64': 'aarch64',
}
plat_os = os_names.get(host_system, host_system)
plat_arch = arch_names.get(host_machine.cpu_family(), host_machine.cpu_family())
platform_dir = plat_os + '-' + plat_arch

sokol_oak = shared_library(
  'sokol_oak',
  sources,
  include_directories: [include_directories('csrc', 'third_party/sokol', 'third_party/sokol/util')] + [imgui_inc],
  c_args: sokol_args + imgui_args,
  link_with: imgui_lib,
  dependencies: deps + [oak_dep],
  gnu_symbol_visibility: 'hidden',
  install: true,
  install_dir: meson.current_source_dir() / 'native' / platform_dir,
  name_prefix: host_system == 'windows' ? '' : 'lib',
)

summary(
  {
    'backend': backend,
    'imgui': have_imgui,
    'platform dir': 'native/' + platform_dir,
  },
  section: 'sokol-oak',
)
