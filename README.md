# sokol-oak

[sokol](https://github.com/floooh/sokol) bindings for the
[Oak](https://github.com/vladbelousoff/oak) scripting language: a window, a
frame loop, input, a GPU rendering API, and Dear ImGui — all callable from
Oak source.

```oak
import { Desc, desc, run } from sokol;
import sokol.gfx as gfx;

fn init(mut d : Desc) {
  gfx.setup(gfx.desc());
}

fn frame(mut d : Desc) {
  gfx.begin_pass(gfx.clear_action(gfx.color(0.2, 0.4, 0.6, 1.0)));
  gfx.end_pass();
  gfx.commit();
}

fn cleanup(mut d : Desc) {
  gfx.shutdown();
}

let d = desc();
d.title = 'hello';
d.init = init;
d.frame = frame;
d.cleanup = cleanup;
run(d);
```

## Modules

| Module | Wraps | What it gives you |
| --- | --- | --- |
| `sokol` | `sokol_app.h` | the window, the frame loop, keyboard and mouse events, the clipboard |
| `sokol.gfx` | `sokol_gfx.h` | buffers, images, samplers, shaders, pipelines, passes, draw calls |
| `sokol.time` | `sokol_time.h` | a monotonic high-resolution clock |
| `sokol.imgui` | `sokol_imgui.h` + [cimgui](https://github.com/cimgui/cimgui) | Dear ImGui windows and widgets |

`sokol_glue.h` is used internally and deliberately not exposed: `gfx.setup()`
and `gfx.begin_pass()` already target the window that `run` opened, so there is
no environment or swapchain to pass around.

## Install

A native package has to reach you with its shared library, and this
repository deliberately contains no binaries — they are build output, not
source. Releases are therefore published as **archives**: each one carries
the `.oak` sources plus a prebuilt library for every supported platform.

```json
"deps": {
  "sokol": {
    "url": "https://github.com/vladbelousoff/sokol-oak/releases/download/v0.1.0/sokol-oak-0.1.0.tar.gz",
    "sha256": "<published alongside the archive>"
  }
}
```

Then `oak-pkg install`, and `import sokol;` — the package claims the `sokol`
namespace, and its submodules are `sokol.gfx`, `sokol.time` and
`sokol.imgui`. There is nothing to compile on the consuming side.

Depending on the git repository instead (`github:vladbelousoff/sokol-oak`)
gets you the sources without a library, which Oak reports as a missing
binary for your platform. That form only works if you build the library
yourself, as below.

Requires Oak 1.0.0 or newer.

## Two things the language shapes

Both of these look odd until you know why, and both are consequences of Oak
rather than of sokol.

**Descriptors are built by a constructor, not a literal.** An Oak record
literal has to name every field, so writing `new gfx.PipelineDesc { ... }`
would mean spelling out a dozen of them to change one. Every descriptor
therefore has a matching constructor — `gfx.pipeline_desc()`,
`gfx.buffer_desc()`, `sokol.desc()` — that returns it filled with the same
defaults sokol uses for a zero-initialised struct. Set only what you care
about:

```oak
let pd = gfx.pipeline_desc();
pd.shader = shd;
pd.cull_mode = gfx.CullMode.Back;
```

**State travels in the `Desc`.** Oak has no closures and no module-level
mutable state: a function sees its parameters and nothing else. So a frame
callback cannot reach a pipeline that `init` created — unless the state
travels through the arguments. Every callback receives the same `Desc`, which
carries typed scratch arrays for exactly this:

```oak
fn init(mut d : Desc) {
  d.pipelines = [ gfx.make_pipeline(pd) ];
  d.numbers = [ 0.0 ];              /* a clock, an angle, a counter */
}

fn frame(mut d : Desc) {
  d.numbers[0] = d.numbers[0] + 1.0;
  gfx.apply_pipeline(d.pipelines[0]);
}
```

Declare the callbacks `mut` so they may write those fields. The arrays are
`numbers`, `flags`, `strings`, and one per gfx handle type: `buffers`,
`images`, `samplers`, `shaders`, `pipelines`, `views`, `bindings`.

## The frame

The loop is not yours to drive. `run` does not return until the application
quits, and on the web backend it does not return at all — the browser drives
the frame callback. Anything that has to happen after the last frame belongs
in `cleanup`.

A frame is the same shape as in a C sokol program:

```oak
fn frame(mut d : Desc) {
  gfx.begin_pass(action);
  gfx.apply_pipeline(d.pipelines[0]);
  gfx.apply_bindings(d.bindings[0]);
  gfx.apply_uniforms_f32(0, [ angle, scale ]);
  gfx.draw(0, 3, 1);
  gfx.end_pass();
  gfx.commit();
}
```

Resource handles (`Buffer`, `Image`, `Sampler`, `Shader`, `Pipeline`, `View`)
are distinct types, so a `Buffer` cannot be passed where a `Pipeline` is
expected. Each is the resource id and nothing more — copying one copies the
handle, not the resource.

## Shaders

sokol does not translate shaders; it takes the language the running backend
speaks. `gfx.query_backend()` says which one that is. The examples are GLSL
because the default build targets desktop GL.

```oak
let sd = gfx.shader_desc();
sd.attrs = [ 'position', 'color0' ];   /* GL binds attributes by name */
sd.vertex_source = '#version 410
in vec4 position;
in vec4 color0;
out vec4 color;
void main() { gl_Position = position; color = color0; }
';
```

Oak string literals may span lines, which is what makes shader source
readable inline.

## Dear ImGui

ImGui is immediate mode: each frame you describe the UI you want, and each
widget returns what the user did with it. Widgets that edit a value take the
current one and return the new one, because Oak has no out-parameters:

```oak
fn frame(mut d : Desc) {
  ui.new_frame();
  if ui.begin('controls') {
    d.numbers[0] = ui.slider_float('red', d.numbers[0], 0.0, 1.0);
    d.flags[0] = ui.checkbox('wireframe', d.flags[0]);
    if ui.button('reset') { d.numbers[0] = 0.0; }
    ui.value('frames', d.numbers[1]);
  }
  ui.end();

  gfx.begin_pass(action);
  ui.render();                  /* inside the pass, over your scene */
  gfx.end_pass();
  gfx.commit();
}

fn event(mut d : Desc, e : Event) {
  ui.handle_event(e);
}
```

`ui.available()` reports whether this build has ImGui compiled in. When it is
false every function here is a no-op and the widgets return their input
unchanged, so a program can call them unconditionally.

## Examples

```sh
oak examples/clear.oak      # a window and an animated clear colour
oak examples/triangle.oak   # a vertex buffer, a shader, a pipeline
oak examples/imgui.oak      # an ImGui panel driving the clear colour
```

## Building the native library

Needed to work on the package itself, or to use it straight from a git
checkout — a checkout has no `native/` directory until you produce one.

```sh
git submodule update --init --recursive
meson setup build --buildtype=release --strip
meson compile -C build
meson install -C build          # writes native/<os>-<arch>/
```

`native/` is gitignored: `meson install` puts the library exactly where Oak
looks for it, and it stays a local build artifact. CI builds the same layout
on each platform and it is the release archive, not the repository, that
carries the result.

Oak's public headers and `acorn` are found through `pkg-config`, so an Oak
installed somewhere unusual is reached with
`meson setup build -Dpkg_config_path=<prefix>/lib/pkgconfig`.

| Option | Default | |
| --- | --- | --- |
| `-Dimgui=` | `auto` | build against cimgui. `auto` uses it when the submodule is present |
| `-Dgfx_backend=` | `auto` | `glcore`, `gles3`, `d3d11`, `metal` or `wgpu`. `auto` picks the platform default |

The three headers that must agree on a backend — `sokol_gfx.h`,
`sokol_app.h`, `sokol_imgui.h` — are all given the same define by the build,
which is why the backend is chosen there and not in the sources.

## What is bound

`sokol.gfx` covers the render path: resource creation and destruction, vertex
and index buffers, images and samplers, shaders with uniform blocks and
texture/sampler pairs, render pipelines with full depth and blend state, the
swapchain pass, and instanced draws. Not bound: compute passes, offscreen
render targets, storage buffers and images, and the resource-state query API.

`sokol.imgui` covers the widgets a debug or tool UI is made of — windows,
child regions, trees, text, buttons, checkboxes, radio buttons, sliders,
drags, number inputs, progress bars, and the layout calls between them. Not
bound: tables, menus, plots, text input, and drag-and-drop.

`sokol_audio.h`, `sokol_fetch.h`, `sokol_args.h` and the utility headers
(`sokol_gl.h`, `sokol_debugtext.h`, `sokol_shape.h`) are not bound.

## Licence

zlib, matching sokol. Bundles sokol (zlib), cimgui (MIT) and Dear ImGui (MIT)
as submodules — see [LICENSE](LICENSE).
