/*
 * sokol_gfx.h -- resource creation and the draw loop.
 *
 * The descriptors sokol_gfx takes are large nested structs with meaningful
 * zero-defaults. They reach this file as Oak records built by the
 * constructors in src/sokol/gfx.oak, so every function here follows the same
 * shape: zero a C struct, then overwrite the fields the record actually
 * carries. A field the Oak side left alone keeps sokol's default, which is
 * why the constructors and this file agree without either restating the
 * other's values.
 *
 * Resource handles cross as inline value types carrying the id, so a Buffer
 * in Oak is the same 32 bits as an sg_buffer in C, with no allocation and no
 * ownership.
 */
#include "sokol_oak.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"

#include <stdlib.h>
#include <string.h>

/* The six handle types, filled in by oaks_bind_gfx and used to type the
 * make_/destroy_ signatures. */
static oak_bind_type_t* g_t_buffer;
static oak_bind_type_t* g_t_image;
static oak_bind_type_t* g_t_sampler;
static oak_bind_type_t* g_t_shader;
static oak_bind_type_t* g_t_pipeline;
static oak_bind_type_t* g_t_view;

/* ------------------------------------------------------------- helpers -- */

/* Read a handle argument. A missing or wrongly-typed one becomes the invalid
 * handle rather than an error: sokol_gfx already treats an invalid handle as
 * "nothing bound", and a descriptor field the Oak side never set should mean
 * the same thing here. */
#define OAKS_HANDLE_FIELD(T, rec, name)                                         \
  (oaks_is_handle(oaks_field((rec), (name)))                                     \
       ? OAKS_VALUE_TO_HANDLE(T, oaks_field((rec), (name)))                      \
       : (T){ .id = SG_INVALID_ID })

static sg_color oaks_color(const oak_value_t rec, const sg_color fallback)
{
  if (!oak_is_record(rec))
    return fallback;
  sg_color c;
  c.r = oaks_field_num(rec, "r", fallback.r);
  c.g = oaks_field_num(rec, "g", fallback.g);
  c.b = oaks_field_num(rec, "b", fallback.b);
  c.a = oaks_field_num(rec, "a", fallback.a);
  return c;
}

/*
 * Vertex and index data arrive as Oak number arrays and have to become a
 * packed C buffer of the right element type. The buffer is temporary -- sokol
 * copies immutable buffer contents during creation and uniform data during
 * apply_uniforms -- so it is allocated, filled, used and freed within the one
 * call. `sg_range` never outlives it.
 */
typedef struct oaks_scratch
{
  void* ptr;
  size_t size;
} oaks_scratch_t;

static void oaks_scratch_free(oaks_scratch_t* s)
{
  free(s->ptr);
  s->ptr = OAK_NULL;
  s->size = 0;
}

/* Pack whichever of the three typed data fields the descriptor carries.
 * Returns 0 on success (including "no data at all"), and -1 when more than
 * one is set -- which is a genuine ambiguity about the element type rather
 * than something to guess at. */
static int oaks_pack_buffer_data(const oak_value_t desc, oaks_scratch_t* out)
{
  const oak_value_t f = oaks_field(desc, "float_data");
  const oak_value_t u16 = oaks_field(desc, "uint16_data");
  const oak_value_t u32 = oaks_field(desc, "uint32_data");
  const usize nf = oaks_len(f), n16 = oaks_len(u16), n32 = oaks_len(u32);

  out->ptr = OAK_NULL;
  out->size = 0;
  if ((nf > 0) + (n16 > 0) + (n32 > 0) > 1)
    return -1;

  if (nf)
  {
    out->size = nf * sizeof(float);
    out->ptr = malloc(out->size);
    if (out->ptr)
      oaks_floats(f, (float*)out->ptr, nf);
  }
  else if (n16)
  {
    out->size = n16 * sizeof(uint16_t);
    out->ptr = malloc(out->size);
    if (out->ptr)
      oaks_u16s(u16, (uint16_t*)out->ptr, n16);
  }
  else if (n32)
  {
    out->size = n32 * sizeof(uint32_t);
    out->ptr = malloc(out->size);
    if (out->ptr)
      oaks_u32s(u32, (uint32_t*)out->ptr, n32);
  }
  return (out->size && !out->ptr) ? -1 : 0;
}

/* --------------------------------------------------------- setup/teardown -- */

static oak_fn_call_result_t oaks_gfx_setup(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc, oak_value_t* out)
{
  (void)argc;
  (void)out;
  const oak_value_t d = args[0];

  sg_desc desc;
  memset(&desc, 0, sizeof(desc));
  desc.buffer_pool_size = oaks_field_int(d, "buffer_pool_size", 0);
  desc.image_pool_size = oaks_field_int(d, "image_pool_size", 0);
  desc.sampler_pool_size = oaks_field_int(d, "sampler_pool_size", 0);
  desc.shader_pool_size = oaks_field_int(d, "shader_pool_size", 0);
  desc.pipeline_pool_size = oaks_field_int(d, "pipeline_pool_size", 0);
  desc.view_pool_size = oaks_field_int(d, "view_pool_size", 0);
  if (oaks_field_bool(d, "logger", 1))
    desc.logger.func = slog_func;
  /* Where sokol_glue.h earns its place in a C program: the environment ties
   * sokol_gfx to the window sokol_app opened. Doing it here rather than
   * exposing it is why the Oak side never mentions an environment. */
  desc.environment = sglue_environment();

  sg_setup(&desc);
  if (!sg_isvalid())
    return oak_native_error(call, "sokol_gfx failed to initialise");
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_gfx_shutdown(oak_native_call_t* call,
                                             const oak_value_t* args,
                                             const usize argc, oak_value_t* out)
{
  (void)call; (void)args; (void)argc; (void)out;
  sg_shutdown();
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_gfx_is_valid(oak_native_call_t* call,
                                             const oak_value_t* args,
                                             const usize argc, oak_value_t* out)
{
  (void)call; (void)args; (void)argc;
  *out = OAK_VALUE_BOOL(sg_isvalid());
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_gfx_backend(oak_native_call_t* call,
                                            const oak_value_t* args,
                                            const usize argc, oak_value_t* out)
{
  (void)call; (void)args; (void)argc;
  *out = OAK_VALUE_I32((int)sg_query_backend());
  return OAK_FN_CALL_OK;
}

/* ----------------------------------------------------------- resources -- */

static oak_fn_call_result_t oaks_make_buffer(oak_native_call_t* call,
                                            const oak_value_t* args,
                                            const usize argc, oak_value_t* out)
{
  (void)argc;
  const oak_value_t d = args[0];
  if (!oak_is_record(d))
    return oak_native_error(call, "make_buffer expects a BufferDesc record");

  oaks_scratch_t data;
  if (oaks_pack_buffer_data(d, &data) != 0)
  {
    oaks_scratch_free(&data);
    return oak_native_error(
        call, "make_buffer: set exactly one of float_data, uint16_data or "
              "uint32_data");
  }

  sg_buffer_desc desc;
  memset(&desc, 0, sizeof(desc));
  const oak_value_t usage = oaks_field(d, "usage");
  desc.usage.vertex_buffer = oaks_field_bool(usage, "vertex_buffer", 1);
  desc.usage.index_buffer = oaks_field_bool(usage, "index_buffer", 0);
  desc.usage.storage_buffer = oaks_field_bool(usage, "storage_buffer", 0);
  desc.usage.immutable = oaks_field_bool(usage, "immutable", 1);
  desc.usage.dynamic_update = oaks_field_bool(usage, "dynamic_update", 0);
  desc.usage.stream_update = oaks_field_bool(usage, "stream_update", 0);
  /* A buffer that is filled later has no data to size it, so `size` is the
   * reservation; one created with data takes its size from the data. */
  desc.size = data.size ? data.size
                        : (size_t)oaks_field_int(d, "size", 0);
  desc.data.ptr = data.ptr;
  desc.data.size = data.size;
  desc.label = oaks_field_str(d, "label");

  const sg_buffer buf = sg_make_buffer(&desc);
  oaks_scratch_free(&data);
  *out = OAKS_HANDLE_TO_VALUE(buf);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_make_image(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           const usize argc, oak_value_t* out)
{
  (void)argc;
  const oak_value_t d = args[0];
  if (!oak_is_record(d))
    return oak_native_error(call, "make_image expects an ImageDesc record");

  sg_image_desc desc;
  memset(&desc, 0, sizeof(desc));
  desc.type = (sg_image_type)oaks_field_int(d, "type", SG_IMAGETYPE_2D);
  desc.width = oaks_field_int(d, "width", 0);
  desc.height = oaks_field_int(d, "height", 0);
  desc.num_mipmaps = oaks_field_int(d, "num_mipmaps", 1);
  desc.pixel_format =
      (sg_pixel_format)oaks_field_int(d, "pixel_format", SG_PIXELFORMAT_RGBA8);
  desc.sample_count = oaks_field_int(d, "sample_count", 1);
  const oak_value_t usage = oaks_field(d, "usage");
  desc.usage.color_attachment = oaks_field_bool(usage, "color_attachment", 0);
  desc.usage.resolve_attachment = oaks_field_bool(usage, "resolve_attachment", 0);
  desc.usage.depth_stencil_attachment =
      oaks_field_bool(usage, "depth_stencil_attachment", 0);
  desc.usage.storage_image = oaks_field_bool(usage, "storage_image", 0);
  desc.usage.immutable = oaks_field_bool(usage, "immutable", 1);
  desc.usage.dynamic_update = oaks_field_bool(usage, "dynamic_update", 0);
  desc.usage.stream_update = oaks_field_bool(usage, "stream_update", 0);
  desc.label = oaks_field_str(d, "label");

  const oak_value_t pixels = oaks_field(d, "pixels");
  const usize n = oaks_len(pixels);
  uint8_t* bytes = OAK_NULL;
  if (n)
  {
    bytes = (uint8_t*)malloc(n);
    if (!bytes)
      return oak_native_error(call, "make_image: out of memory");
    oaks_u8s(pixels, bytes, n);
    /* Only the base mip level is expressible here, which is what an
     * ImageDesc with a flat `pixels` array can describe. */
    desc.data.mip_levels[0].ptr = bytes;
    desc.data.mip_levels[0].size = n;
  }

  const sg_image img = sg_make_image(&desc);
  free(bytes);
  *out = OAKS_HANDLE_TO_VALUE(img);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_make_sampler(oak_native_call_t* call,
                                             const oak_value_t* args,
                                             const usize argc, oak_value_t* out)
{
  (void)argc;
  const oak_value_t d = args[0];
  if (!oak_is_record(d))
    return oak_native_error(call, "make_sampler expects a SamplerDesc record");

  sg_sampler_desc desc;
  memset(&desc, 0, sizeof(desc));
  desc.min_filter = (sg_filter)oaks_field_int(d, "min_filter", SG_FILTER_NEAREST);
  desc.mag_filter = (sg_filter)oaks_field_int(d, "mag_filter", SG_FILTER_NEAREST);
  desc.mipmap_filter =
      (sg_filter)oaks_field_int(d, "mipmap_filter", SG_FILTER_NEAREST);
  desc.wrap_u = (sg_wrap)oaks_field_int(d, "wrap_u", SG_WRAP_REPEAT);
  desc.wrap_v = (sg_wrap)oaks_field_int(d, "wrap_v", SG_WRAP_REPEAT);
  desc.wrap_w = (sg_wrap)oaks_field_int(d, "wrap_w", SG_WRAP_REPEAT);
  desc.min_lod = oaks_field_num(d, "min_lod", 0.0f);
  desc.max_lod = oaks_field_num(d, "max_lod", 1000.0f);
  desc.compare = (sg_compare_func)oaks_field_int(d, "compare", SG_COMPAREFUNC_NEVER);
  desc.max_anisotropy = (uint32_t)oaks_field_int(d, "max_anisotropy", 1);
  desc.label = oaks_field_str(d, "label");

  const sg_sampler smp = sg_make_sampler(&desc);
  *out = OAKS_HANDLE_TO_VALUE(smp);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_make_view(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc, oak_value_t* out)
{
  (void)argc;
  const oak_value_t d = args[0];
  if (!oak_is_record(d))
    return oak_native_error(call, "make_view expects a ViewDesc record");

  sg_view_desc desc;
  memset(&desc, 0, sizeof(desc));
  desc.texture.image = OAKS_HANDLE_FIELD(sg_image, d, "texture");
  /* A count of 0 means "all of them", which is what a view over a whole
   * texture wants and what leaving these unset should mean. */
  desc.texture.mip_levels.base = oaks_field_int(d, "base_mip_level", 0);
  desc.texture.mip_levels.count = oaks_field_int(d, "mip_level_count", 0);
  desc.texture.slices.base = oaks_field_int(d, "base_slice", 0);
  desc.texture.slices.count = oaks_field_int(d, "slice_count", 0);
  desc.label = oaks_field_str(d, "label");

  const sg_view v = sg_make_view(&desc);
  *out = OAKS_HANDLE_TO_VALUE(v);
  return OAK_FN_CALL_OK;
}

/* Shader creation is the one descriptor with four parallel arrays in it, so
 * it is the one that reads as a loop per array rather than a run of
 * assignments. */
static oak_fn_call_result_t oaks_make_shader(oak_native_call_t* call,
                                            const oak_value_t* args,
                                            const usize argc, oak_value_t* out)
{
  (void)argc;
  const oak_value_t d = args[0];
  if (!oak_is_record(d))
    return oak_native_error(call, "make_shader expects a ShaderDesc record");

  sg_shader_desc desc;
  memset(&desc, 0, sizeof(desc));
  desc.vertex_func.source = oaks_field_str(d, "vertex_source");
  desc.fragment_func.source = oaks_field_str(d, "fragment_source");
  desc.vertex_func.entry = oaks_field_str(d, "vertex_entry");
  desc.fragment_func.entry = oaks_field_str(d, "fragment_entry");
  desc.label = oaks_field_str(d, "label");

  const oak_value_t attrs = oaks_field(d, "attrs");
  usize n = oaks_len(attrs);
  if (n > SG_MAX_VERTEX_ATTRIBUTES)
    n = SG_MAX_VERTEX_ATTRIBUTES;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t a = oaks_at(attrs, i);
    if (oak_is_string(a))
      desc.attrs[i].glsl_name = oak_as_string(a)->chars;
  }

  const oak_value_t ubs = oaks_field(d, "uniform_blocks");
  n = oaks_len(ubs);
  if (n > SG_MAX_UNIFORMBLOCK_BINDSLOTS)
    n = SG_MAX_UNIFORMBLOCK_BINDSLOTS;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t ub = oaks_at(ubs, i);
    desc.uniform_blocks[i].stage =
        (sg_shader_stage)oaks_field_int(ub, "stage", SG_SHADERSTAGE_VERTEX);
    desc.uniform_blocks[i].size = (uint32_t)oaks_field_int(ub, "size", 0);
    /* The GL backends set uniforms by name, so a uniform block that lists
     * its members is the only kind they can bind. The other backends ignore
     * this and address the block by offset. */
    desc.uniform_blocks[i].layout = SG_UNIFORMLAYOUT_STD140;
    const oak_value_t us = oaks_field(ub, "glsl_uniforms");
    usize un = oaks_len(us);
    if (un > SG_MAX_UNIFORMBLOCK_MEMBERS)
      un = SG_MAX_UNIFORMBLOCK_MEMBERS;
    for (usize j = 0; j < un; ++j)
    {
      const oak_value_t u = oaks_at(us, j);
      desc.uniform_blocks[i].glsl_uniforms[j].type =
          (sg_uniform_type)oaks_field_int(u, "type", SG_UNIFORMTYPE_FLOAT4);
      desc.uniform_blocks[i].glsl_uniforms[j].array_count =
          (uint16_t)oaks_field_int(u, "array_count", 0);
      desc.uniform_blocks[i].glsl_uniforms[j].glsl_name =
          oaks_field_str(u, "glsl_name");
    }
  }

  const oak_value_t views = oaks_field(d, "views");
  n = oaks_len(views);
  if (n > SG_MAX_VIEW_BINDSLOTS)
    n = SG_MAX_VIEW_BINDSLOTS;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t v = oaks_at(views, i);
    desc.views[i].texture.stage =
        (sg_shader_stage)oaks_field_int(v, "stage", SG_SHADERSTAGE_FRAGMENT);
    desc.views[i].texture.image_type =
        (sg_image_type)oaks_field_int(v, "image_type", SG_IMAGETYPE_2D);
    desc.views[i].texture.sample_type = (sg_image_sample_type)oaks_field_int(
        v, "sample_type", SG_IMAGESAMPLETYPE_FLOAT);
    desc.views[i].texture.multisampled = oaks_field_bool(v, "multisampled", 0);
  }

  const oak_value_t smps = oaks_field(d, "samplers");
  n = oaks_len(smps);
  if (n > SG_MAX_SAMPLER_BINDSLOTS)
    n = SG_MAX_SAMPLER_BINDSLOTS;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t s = oaks_at(smps, i);
    desc.samplers[i].stage =
        (sg_shader_stage)oaks_field_int(s, "stage", SG_SHADERSTAGE_FRAGMENT);
    desc.samplers[i].sampler_type =
        (sg_sampler_type)oaks_field_int(s, "sampler_type", SG_SAMPLERTYPE_FILTERING);
  }

  const oak_value_t pairs = oaks_field(d, "texture_sampler_pairs");
  n = oaks_len(pairs);
  if (n > SG_MAX_TEXTURE_SAMPLER_PAIRS)
    n = SG_MAX_TEXTURE_SAMPLER_PAIRS;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t p = oaks_at(pairs, i);
    desc.texture_sampler_pairs[i].stage =
        (sg_shader_stage)oaks_field_int(p, "stage", SG_SHADERSTAGE_FRAGMENT);
    desc.texture_sampler_pairs[i].view_slot = oaks_field_int(p, "view_slot", 0);
    desc.texture_sampler_pairs[i].sampler_slot =
        oaks_field_int(p, "sampler_slot", 0);
    desc.texture_sampler_pairs[i].glsl_name = oaks_field_str(p, "glsl_name");
  }

  const sg_shader shd = sg_make_shader(&desc);
  *out = OAKS_HANDLE_TO_VALUE(shd);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_make_pipeline(oak_native_call_t* call,
                                              const oak_value_t* args,
                                              const usize argc,
                                              oak_value_t* out)
{
  (void)argc;
  const oak_value_t d = args[0];
  if (!oak_is_record(d))
    return oak_native_error(call, "make_pipeline expects a PipelineDesc record");

  sg_pipeline_desc desc;
  memset(&desc, 0, sizeof(desc));
  desc.shader = OAKS_HANDLE_FIELD(sg_shader, d, "shader");
  desc.primitive_type =
      (sg_primitive_type)oaks_field_int(d, "primitive_type", SG_PRIMITIVETYPE_TRIANGLES);
  desc.index_type = (sg_index_type)oaks_field_int(d, "index_type", SG_INDEXTYPE_NONE);
  desc.cull_mode = (sg_cull_mode)oaks_field_int(d, "cull_mode", SG_CULLMODE_NONE);
  desc.face_winding =
      (sg_face_winding)oaks_field_int(d, "face_winding", SG_FACEWINDING_CW);
  desc.sample_count = oaks_field_int(d, "sample_count", 0);
  desc.alpha_to_coverage_enabled =
      oaks_field_bool(d, "alpha_to_coverage_enabled", 0);
  desc.blend_color = oaks_color(oaks_field(d, "blend_color"), (sg_color){ 0, 0, 0, 0 });
  desc.label = oaks_field_str(d, "label");

  const oak_value_t depth = oaks_field(d, "depth");
  desc.depth.pixel_format =
      (sg_pixel_format)oaks_field_int(depth, "pixel_format", SG_PIXELFORMAT_NONE);
  desc.depth.compare =
      (sg_compare_func)oaks_field_int(depth, "compare", SG_COMPAREFUNC_ALWAYS);
  desc.depth.write_enabled = oaks_field_bool(depth, "write_enabled", 0);
  desc.depth.bias = oaks_field_num(depth, "bias", 0.0f);
  desc.depth.bias_slope_scale = oaks_field_num(depth, "bias_slope_scale", 0.0f);
  desc.depth.bias_clamp = oaks_field_num(depth, "bias_clamp", 0.0f);

  const oak_value_t layout = oaks_field(d, "layout");
  const oak_value_t bufs = oaks_field(layout, "buffers");
  usize n = oaks_len(bufs);
  if (n > SG_MAX_VERTEXBUFFER_BINDSLOTS)
    n = SG_MAX_VERTEXBUFFER_BINDSLOTS;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t b = oaks_at(bufs, i);
    desc.layout.buffers[i].stride = oaks_field_int(b, "stride", 0);
    desc.layout.buffers[i].step_func =
        (sg_vertex_step)oaks_field_int(b, "step_func", SG_VERTEXSTEP_PER_VERTEX);
    desc.layout.buffers[i].step_rate = oaks_field_int(b, "step_rate", 1);
  }

  const oak_value_t vattrs = oaks_field(layout, "attrs");
  n = oaks_len(vattrs);
  if (n > SG_MAX_VERTEX_ATTRIBUTES)
    n = SG_MAX_VERTEX_ATTRIBUTES;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t a = oaks_at(vattrs, i);
    desc.layout.attrs[i].buffer_index = oaks_field_int(a, "buffer_index", 0);
    desc.layout.attrs[i].offset = oaks_field_int(a, "offset", 0);
    desc.layout.attrs[i].format =
        (sg_vertex_format)oaks_field_int(a, "format", SG_VERTEXFORMAT_INVALID);
  }

  const oak_value_t colors = oaks_field(d, "colors");
  n = oaks_len(colors);
  if (n > SG_MAX_COLOR_ATTACHMENTS)
    n = SG_MAX_COLOR_ATTACHMENTS;
  desc.color_count = (int)n;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t c = oaks_at(colors, i);
    desc.colors[i].pixel_format =
        (sg_pixel_format)oaks_field_int(c, "pixel_format", SG_PIXELFORMAT_NONE);
    /* sokol packs the four channel write flags into one mask. */
    int mask = 0;
    if (oaks_field_bool(c, "write_mask_r", 1)) mask |= SG_COLORMASK_R;
    if (oaks_field_bool(c, "write_mask_g", 1)) mask |= SG_COLORMASK_G;
    if (oaks_field_bool(c, "write_mask_b", 1)) mask |= SG_COLORMASK_B;
    if (oaks_field_bool(c, "write_mask_a", 1)) mask |= SG_COLORMASK_A;
    desc.colors[i].write_mask = (sg_color_mask)mask;

    const oak_value_t bl = oaks_field(c, "blend");
    desc.colors[i].blend.enabled = oaks_field_bool(bl, "enabled", 0);
    desc.colors[i].blend.src_factor_rgb =
        (sg_blend_factor)oaks_field_int(bl, "src_factor_rgb", SG_BLENDFACTOR_ONE);
    desc.colors[i].blend.dst_factor_rgb =
        (sg_blend_factor)oaks_field_int(bl, "dst_factor_rgb", SG_BLENDFACTOR_ZERO);
    desc.colors[i].blend.op_rgb =
        (sg_blend_op)oaks_field_int(bl, "op_rgb", SG_BLENDOP_ADD);
    desc.colors[i].blend.src_factor_alpha = (sg_blend_factor)oaks_field_int(
        bl, "src_factor_alpha", SG_BLENDFACTOR_ONE);
    desc.colors[i].blend.dst_factor_alpha = (sg_blend_factor)oaks_field_int(
        bl, "dst_factor_alpha", SG_BLENDFACTOR_ZERO);
    desc.colors[i].blend.op_alpha =
        (sg_blend_op)oaks_field_int(bl, "op_alpha", SG_BLENDOP_ADD);
  }

  const sg_pipeline pip = sg_make_pipeline(&desc);
  *out = OAKS_HANDLE_TO_VALUE(pip);
  return OAK_FN_CALL_OK;
}

/* destroy_* and invalid_* are the same shape six times over. */
#define OAKS_DESTROY(fn_name, T, sg_call)                                       \
  static oak_fn_call_result_t fn_name(oak_native_call_t* call,                 \
                                      const oak_value_t* args,                 \
                                      const usize argc, oak_value_t* out)      \
  {                                                                            \
    (void)argc;                                                                \
    (void)out;                                                                 \
    if (!oaks_is_handle(args[0]))                                               \
      return oak_native_error(call, #sg_call " expects a resource handle");    \
    sg_call(OAKS_VALUE_TO_HANDLE(T, args[0]));                                  \
    return OAK_FN_CALL_OK;                                                     \
  }

OAKS_DESTROY(oaks_destroy_buffer, sg_buffer, sg_destroy_buffer)
OAKS_DESTROY(oaks_destroy_image, sg_image, sg_destroy_image)
OAKS_DESTROY(oaks_destroy_sampler, sg_sampler, sg_destroy_sampler)
OAKS_DESTROY(oaks_destroy_shader, sg_shader, sg_destroy_shader)
OAKS_DESTROY(oaks_destroy_pipeline, sg_pipeline, sg_destroy_pipeline)
OAKS_DESTROY(oaks_destroy_view, sg_view, sg_destroy_view)

#define OAKS_INVALID(fn_name)                                                   \
  static oak_fn_call_result_t fn_name(oak_native_call_t* call,                 \
                                      const oak_value_t* args,                 \
                                      const usize argc, oak_value_t* out)      \
  {                                                                            \
    (void)call;                                                                \
    (void)args;                                                                \
    (void)argc;                                                                \
    *out = oak_native_value_new((void*)(uintptr_t)SG_INVALID_ID);              \
    return OAK_FN_CALL_OK;                                                     \
  }

OAKS_INVALID(oaks_invalid_buffer)
OAKS_INVALID(oaks_invalid_image)
OAKS_INVALID(oaks_invalid_sampler)
OAKS_INVALID(oaks_invalid_shader)
OAKS_INVALID(oaks_invalid_pipeline)
OAKS_INVALID(oaks_invalid_view)

static oak_fn_call_result_t oaks_update_buffer(oak_native_call_t* call,
                                              const oak_value_t* args,
                                              const usize argc,
                                              oak_value_t* out)
{
  (void)argc;
  (void)out;
  if (!oaks_is_handle(args[0]))
    return oak_native_error(call, "update_buffer_f32 expects a Buffer");
  const usize n = oaks_len(args[1]);
  if (!n)
    return OAK_FN_CALL_OK;
  float* tmp = (float*)malloc(n * sizeof(float));
  if (!tmp)
    return oak_native_error(call, "update_buffer_f32: out of memory");
  oaks_floats(args[1], tmp, n);
  const sg_range r = { tmp, n * sizeof(float) };
  sg_update_buffer(OAKS_VALUE_TO_HANDLE(sg_buffer, args[0]), &r);
  free(tmp);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_append_buffer(oak_native_call_t* call,
                                              const oak_value_t* args,
                                              const usize argc,
                                              oak_value_t* out)
{
  (void)argc;
  if (!oaks_is_handle(args[0]))
    return oak_native_error(call, "append_buffer_f32 expects a Buffer");
  const usize n = oaks_len(args[1]);
  if (!n)
  {
    *out = OAK_VALUE_I32(0);
    return OAK_FN_CALL_OK;
  }
  float* tmp = (float*)malloc(n * sizeof(float));
  if (!tmp)
    return oak_native_error(call, "append_buffer_f32: out of memory");
  oaks_floats(args[1], tmp, n);
  const sg_range r = { tmp, n * sizeof(float) };
  const int offset = sg_append_buffer(OAKS_VALUE_TO_HANDLE(sg_buffer, args[0]), &r);
  free(tmp);
  *out = OAK_VALUE_I32(offset);
  return OAK_FN_CALL_OK;
}

/* ------------------------------------------------------------ the frame -- */

static oak_fn_call_result_t oaks_begin_pass(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           const usize argc, oak_value_t* out)
{
  (void)argc;
  (void)out;
  const oak_value_t a = args[0];
  if (!oak_is_record(a))
    return oak_native_error(call, "begin_pass expects a PassAction record");

  sg_pass pass;
  memset(&pass, 0, sizeof(pass));

  const oak_value_t colors = oaks_field(a, "colors");
  usize n = oaks_len(colors);
  if (n > SG_MAX_COLOR_ATTACHMENTS)
    n = SG_MAX_COLOR_ATTACHMENTS;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t c = oaks_at(colors, i);
    pass.action.colors[i].load_action =
        (sg_load_action)oaks_field_int(c, "load_action", SG_LOADACTION_CLEAR);
    pass.action.colors[i].store_action =
        (sg_store_action)oaks_field_int(c, "store_action", SG_STOREACTION_STORE);
    pass.action.colors[i].clear_value = oaks_color(
        oaks_field(c, "clear_value"), (sg_color){ 0.5f, 0.5f, 0.5f, 1.0f });
  }

  const oak_value_t depth = oaks_field(a, "depth");
  if (oak_is_record(depth))
  {
    pass.action.depth.load_action =
        (sg_load_action)oaks_field_int(depth, "load_action", SG_LOADACTION_CLEAR);
    pass.action.depth.store_action = (sg_store_action)oaks_field_int(
        depth, "store_action", SG_STOREACTION_DONTCARE);
    pass.action.depth.clear_value = oaks_field_num(depth, "clear_value", 1.0f);
  }

  /* As in setup: the swapchain comes from sokol_app through sokol_glue, so an
   * Oak program renders to its window without naming one. */
  pass.swapchain = sglue_swapchain();
  sg_begin_pass(&pass);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_end_pass(oak_native_call_t* call,
                                         const oak_value_t* args,
                                         const usize argc, oak_value_t* out)
{
  (void)call; (void)args; (void)argc; (void)out;
  sg_end_pass();
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_commit(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       const usize argc, oak_value_t* out)
{
  (void)call; (void)args; (void)argc; (void)out;
  sg_commit();
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_apply_pipeline(oak_native_call_t* call,
                                               const oak_value_t* args,
                                               const usize argc,
                                               oak_value_t* out)
{
  (void)argc;
  (void)out;
  if (!oaks_is_handle(args[0]))
    return oak_native_error(call, "apply_pipeline expects a Pipeline");
  sg_apply_pipeline(OAKS_VALUE_TO_HANDLE(sg_pipeline, args[0]));
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_apply_bindings(oak_native_call_t* call,
                                               const oak_value_t* args,
                                               const usize argc,
                                               oak_value_t* out)
{
  (void)argc;
  (void)out;
  const oak_value_t b = args[0];
  if (!oak_is_record(b))
    return oak_native_error(call, "apply_bindings expects a Bindings record");

  sg_bindings bind;
  memset(&bind, 0, sizeof(bind));

  const oak_value_t vbufs = oaks_field(b, "vertex_buffers");
  const oak_value_t voffs = oaks_field(b, "vertex_buffer_offsets");
  usize n = oaks_len(vbufs);
  if (n > SG_MAX_VERTEXBUFFER_BINDSLOTS)
    n = SG_MAX_VERTEXBUFFER_BINDSLOTS;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t v = oaks_at(vbufs, i);
    if (oaks_is_handle(v))
      bind.vertex_buffers[i] = OAKS_VALUE_TO_HANDLE(sg_buffer, v);
    bind.vertex_buffer_offsets[i] = oaks_int(oaks_at(voffs, i), 0);
  }

  bind.index_buffer = OAKS_HANDLE_FIELD(sg_buffer, b, "index_buffer");
  bind.index_buffer_offset = oaks_field_int(b, "index_buffer_offset", 0);

  const oak_value_t views = oaks_field(b, "views");
  n = oaks_len(views);
  if (n > SG_MAX_VIEW_BINDSLOTS)
    n = SG_MAX_VIEW_BINDSLOTS;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t v = oaks_at(views, i);
    if (oaks_is_handle(v))
      bind.views[i] = OAKS_VALUE_TO_HANDLE(sg_view, v);
  }

  const oak_value_t smps = oaks_field(b, "samplers");
  n = oaks_len(smps);
  if (n > SG_MAX_SAMPLER_BINDSLOTS)
    n = SG_MAX_SAMPLER_BINDSLOTS;
  for (usize i = 0; i < n; ++i)
  {
    const oak_value_t s = oaks_at(smps, i);
    if (oaks_is_handle(s))
      bind.samplers[i] = OAKS_VALUE_TO_HANDLE(sg_sampler, s);
  }

  sg_apply_bindings(&bind);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_apply_uniforms(oak_native_call_t* call,
                                               const oak_value_t* args,
                                               const usize argc,
                                               oak_value_t* out)
{
  (void)out;
  int slot;
  if (!oak_arg_i32(call, args, argc, 0, &slot))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const usize n = oaks_len(args[1]);
  if (!n)
    return oak_native_error(call, "apply_uniforms_f32: no uniform data");
  float* tmp = (float*)malloc(n * sizeof(float));
  if (!tmp)
    return oak_native_error(call, "apply_uniforms_f32: out of memory");
  oaks_floats(args[1], tmp, n);
  const sg_range r = { tmp, n * sizeof(float) };
  sg_apply_uniforms(slot, &r);
  free(tmp);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_apply_viewport(oak_native_call_t* call,
                                               const oak_value_t* args,
                                               const usize argc,
                                               oak_value_t* out)
{
  (void)out;
  int x, y, w, h, top_left;
  if (!oak_arg_i32(call, args, argc, 0, &x) ||
      !oak_arg_i32(call, args, argc, 1, &y) ||
      !oak_arg_i32(call, args, argc, 2, &w) ||
      !oak_arg_i32(call, args, argc, 3, &h) ||
      !oak_arg_bool(call, args, argc, 4, &top_left))
    return OAK_FN_CALL_RUNTIME_ERROR;
  sg_apply_viewport(x, y, w, h, top_left);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_apply_scissor(oak_native_call_t* call,
                                              const oak_value_t* args,
                                              const usize argc,
                                              oak_value_t* out)
{
  (void)out;
  int x, y, w, h, top_left;
  if (!oak_arg_i32(call, args, argc, 0, &x) ||
      !oak_arg_i32(call, args, argc, 1, &y) ||
      !oak_arg_i32(call, args, argc, 2, &w) ||
      !oak_arg_i32(call, args, argc, 3, &h) ||
      !oak_arg_bool(call, args, argc, 4, &top_left))
    return OAK_FN_CALL_RUNTIME_ERROR;
  sg_apply_scissor_rect(x, y, w, h, top_left);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_draw(oak_native_call_t* call,
                                     const oak_value_t* args,
                                     const usize argc, oak_value_t* out)
{
  (void)out;
  int base, count, instances;
  if (!oak_arg_i32(call, args, argc, 0, &base) ||
      !oak_arg_i32(call, args, argc, 1, &count) ||
      !oak_arg_i32(call, args, argc, 2, &instances))
    return OAK_FN_CALL_RUNTIME_ERROR;
  sg_draw(base, count, instances);
  return OAK_FN_CALL_OK;
}

/* ------------------------------------------------------------- binding -- */

int oaks_bind_gfx(oak_compile_options_t* opts, oak_bind_module_t* m)
{
  static const oak_bind_enum_variant_t backends[] = {
    { "GLCore", SG_BACKEND_GLCORE },
    { "GLES3", SG_BACKEND_GLES3 },
    { "D3D11", SG_BACKEND_D3D11 },
    { "MetalIOS", SG_BACKEND_METAL_IOS },
    { "MetalMacOS", SG_BACKEND_METAL_MACOS },
    { "MetalSimulator", SG_BACKEND_METAL_SIMULATOR },
    { "Wgpu", SG_BACKEND_WGPU },
    { "Vulkan", SG_BACKEND_VULKAN },
    { "Dummy", SG_BACKEND_DUMMY },
  };
  static const oak_bind_enum_variant_t pixel_formats[] = {
    { "Default", _SG_PIXELFORMAT_DEFAULT }, { "None", SG_PIXELFORMAT_NONE },
    { "R8", SG_PIXELFORMAT_R8 }, { "R8SN", SG_PIXELFORMAT_R8SN },
    { "R8UI", SG_PIXELFORMAT_R8UI }, { "R8SI", SG_PIXELFORMAT_R8SI },
    { "R16", SG_PIXELFORMAT_R16 }, { "R16SN", SG_PIXELFORMAT_R16SN },
    { "R16UI", SG_PIXELFORMAT_R16UI }, { "R16SI", SG_PIXELFORMAT_R16SI },
    { "R16F", SG_PIXELFORMAT_R16F }, { "RG8", SG_PIXELFORMAT_RG8 },
    { "RG8SN", SG_PIXELFORMAT_RG8SN }, { "RG8UI", SG_PIXELFORMAT_RG8UI },
    { "RG8SI", SG_PIXELFORMAT_RG8SI }, { "R32UI", SG_PIXELFORMAT_R32UI },
    { "R32SI", SG_PIXELFORMAT_R32SI }, { "R32F", SG_PIXELFORMAT_R32F },
    { "RG16", SG_PIXELFORMAT_RG16 }, { "RG16SN", SG_PIXELFORMAT_RG16SN },
    { "RG16UI", SG_PIXELFORMAT_RG16UI }, { "RG16SI", SG_PIXELFORMAT_RG16SI },
    { "RG16F", SG_PIXELFORMAT_RG16F }, { "RGBA8", SG_PIXELFORMAT_RGBA8 },
    { "SRGB8A8", SG_PIXELFORMAT_SRGB8A8 }, { "RGBA8SN", SG_PIXELFORMAT_RGBA8SN },
    { "RGBA8UI", SG_PIXELFORMAT_RGBA8UI }, { "RGBA8SI", SG_PIXELFORMAT_RGBA8SI },
    { "BGRA8", SG_PIXELFORMAT_BGRA8 }, { "RGB10A2", SG_PIXELFORMAT_RGB10A2 },
    { "RG11B10F", SG_PIXELFORMAT_RG11B10F }, { "RGB9E5", SG_PIXELFORMAT_RGB9E5 },
    { "RG32UI", SG_PIXELFORMAT_RG32UI }, { "RG32SI", SG_PIXELFORMAT_RG32SI },
    { "RG32F", SG_PIXELFORMAT_RG32F }, { "RGBA16", SG_PIXELFORMAT_RGBA16 },
    { "RGBA16SN", SG_PIXELFORMAT_RGBA16SN },
    { "RGBA16UI", SG_PIXELFORMAT_RGBA16UI },
    { "RGBA16SI", SG_PIXELFORMAT_RGBA16SI },
    { "RGBA16F", SG_PIXELFORMAT_RGBA16F },
    { "RGBA32UI", SG_PIXELFORMAT_RGBA32UI },
    { "RGBA32SI", SG_PIXELFORMAT_RGBA32SI },
    { "RGBA32F", SG_PIXELFORMAT_RGBA32F },
    { "Depth", SG_PIXELFORMAT_DEPTH },
    { "DepthStencil", SG_PIXELFORMAT_DEPTH_STENCIL },
  };
  static const oak_bind_enum_variant_t primitive_types[] = {
    { "Default", _SG_PRIMITIVETYPE_DEFAULT },
    { "Points", SG_PRIMITIVETYPE_POINTS },
    { "Lines", SG_PRIMITIVETYPE_LINES },
    { "LineStrip", SG_PRIMITIVETYPE_LINE_STRIP },
    { "Triangles", SG_PRIMITIVETYPE_TRIANGLES },
    { "TriangleStrip", SG_PRIMITIVETYPE_TRIANGLE_STRIP },
  };
  static const oak_bind_enum_variant_t index_types[] = {
    { "Default", _SG_INDEXTYPE_DEFAULT },
    { "None", SG_INDEXTYPE_NONE },
    { "UInt16", SG_INDEXTYPE_UINT16 },
    { "UInt32", SG_INDEXTYPE_UINT32 },
  };
  static const oak_bind_enum_variant_t vertex_formats[] = {
    { "Invalid", SG_VERTEXFORMAT_INVALID },
    { "Float", SG_VERTEXFORMAT_FLOAT }, { "Float2", SG_VERTEXFORMAT_FLOAT2 },
    { "Float3", SG_VERTEXFORMAT_FLOAT3 }, { "Float4", SG_VERTEXFORMAT_FLOAT4 },
    { "Int", SG_VERTEXFORMAT_INT }, { "Int2", SG_VERTEXFORMAT_INT2 },
    { "Int3", SG_VERTEXFORMAT_INT3 }, { "Int4", SG_VERTEXFORMAT_INT4 },
    { "UInt", SG_VERTEXFORMAT_UINT }, { "UInt2", SG_VERTEXFORMAT_UINT2 },
    { "UInt3", SG_VERTEXFORMAT_UINT3 }, { "UInt4", SG_VERTEXFORMAT_UINT4 },
    { "Byte4", SG_VERTEXFORMAT_BYTE4 }, { "Byte4N", SG_VERTEXFORMAT_BYTE4N },
    { "UByte4", SG_VERTEXFORMAT_UBYTE4 }, { "UByte4N", SG_VERTEXFORMAT_UBYTE4N },
    { "Short2", SG_VERTEXFORMAT_SHORT2 }, { "Short2N", SG_VERTEXFORMAT_SHORT2N },
    { "UShort2", SG_VERTEXFORMAT_USHORT2 },
    { "UShort2N", SG_VERTEXFORMAT_USHORT2N },
    { "Short4", SG_VERTEXFORMAT_SHORT4 }, { "Short4N", SG_VERTEXFORMAT_SHORT4N },
    { "UShort4", SG_VERTEXFORMAT_USHORT4 },
    { "UShort4N", SG_VERTEXFORMAT_USHORT4N },
    { "UInt10N2", SG_VERTEXFORMAT_UINT10_N2 },
    { "Half2", SG_VERTEXFORMAT_HALF2 }, { "Half4", SG_VERTEXFORMAT_HALF4 },
  };
  static const oak_bind_enum_variant_t vertex_steps[] = {
    { "Default", _SG_VERTEXSTEP_DEFAULT },
    { "PerVertex", SG_VERTEXSTEP_PER_VERTEX },
    { "PerInstance", SG_VERTEXSTEP_PER_INSTANCE },
  };
  static const oak_bind_enum_variant_t load_actions[] = {
    { "Default", _SG_LOADACTION_DEFAULT },
    { "Clear", SG_LOADACTION_CLEAR },
    { "Load", SG_LOADACTION_LOAD },
    { "DontCare", SG_LOADACTION_DONTCARE },
  };
  static const oak_bind_enum_variant_t store_actions[] = {
    { "Default", _SG_STOREACTION_DEFAULT },
    { "Store", SG_STOREACTION_STORE },
    { "DontCare", SG_STOREACTION_DONTCARE },
  };
  static const oak_bind_enum_variant_t filters[] = {
    { "Default", _SG_FILTER_DEFAULT },
    { "Nearest", SG_FILTER_NEAREST },
    { "Linear", SG_FILTER_LINEAR },
  };
  static const oak_bind_enum_variant_t wraps[] = {
    { "Default", _SG_WRAP_DEFAULT },
    { "Repeat", SG_WRAP_REPEAT },
    { "ClampToEdge", SG_WRAP_CLAMP_TO_EDGE },
    { "ClampToBorder", SG_WRAP_CLAMP_TO_BORDER },
    { "MirroredRepeat", SG_WRAP_MIRRORED_REPEAT },
  };
  static const oak_bind_enum_variant_t cull_modes[] = {
    { "Default", _SG_CULLMODE_DEFAULT },
    { "None", SG_CULLMODE_NONE },
    { "Front", SG_CULLMODE_FRONT },
    { "Back", SG_CULLMODE_BACK },
  };
  static const oak_bind_enum_variant_t face_windings[] = {
    { "Default", _SG_FACEWINDING_DEFAULT },
    { "CCW", SG_FACEWINDING_CCW },
    { "CW", SG_FACEWINDING_CW },
  };
  static const oak_bind_enum_variant_t compare_funcs[] = {
    { "Default", _SG_COMPAREFUNC_DEFAULT },
    { "Never", SG_COMPAREFUNC_NEVER },
    { "Less", SG_COMPAREFUNC_LESS },
    { "Equal", SG_COMPAREFUNC_EQUAL },
    { "LessEqual", SG_COMPAREFUNC_LESS_EQUAL },
    { "Greater", SG_COMPAREFUNC_GREATER },
    { "NotEqual", SG_COMPAREFUNC_NOT_EQUAL },
    { "GreaterEqual", SG_COMPAREFUNC_GREATER_EQUAL },
    { "Always", SG_COMPAREFUNC_ALWAYS },
  };
  static const oak_bind_enum_variant_t blend_factors[] = {
    { "Default", _SG_BLENDFACTOR_DEFAULT },
    { "Zero", SG_BLENDFACTOR_ZERO },
    { "One", SG_BLENDFACTOR_ONE },
    { "SrcColor", SG_BLENDFACTOR_SRC_COLOR },
    { "OneMinusSrcColor", SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR },
    { "SrcAlpha", SG_BLENDFACTOR_SRC_ALPHA },
    { "OneMinusSrcAlpha", SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA },
    { "DstColor", SG_BLENDFACTOR_DST_COLOR },
    { "OneMinusDstColor", SG_BLENDFACTOR_ONE_MINUS_DST_COLOR },
    { "DstAlpha", SG_BLENDFACTOR_DST_ALPHA },
    { "OneMinusDstAlpha", SG_BLENDFACTOR_ONE_MINUS_DST_ALPHA },
    { "SrcAlphaSaturated", SG_BLENDFACTOR_SRC_ALPHA_SATURATED },
    { "BlendColor", SG_BLENDFACTOR_BLEND_COLOR },
    { "OneMinusBlendColor", SG_BLENDFACTOR_ONE_MINUS_BLEND_COLOR },
    { "BlendAlpha", SG_BLENDFACTOR_BLEND_ALPHA },
    { "OneMinusBlendAlpha", SG_BLENDFACTOR_ONE_MINUS_BLEND_ALPHA },
  };
  static const oak_bind_enum_variant_t blend_ops[] = {
    { "Default", _SG_BLENDOP_DEFAULT },
    { "Add", SG_BLENDOP_ADD },
    { "Subtract", SG_BLENDOP_SUBTRACT },
    { "ReverseSubtract", SG_BLENDOP_REVERSE_SUBTRACT },
    { "Min", SG_BLENDOP_MIN },
    { "Max", SG_BLENDOP_MAX },
  };
  static const oak_bind_enum_variant_t shader_stages[] = {
    { "None", SG_SHADERSTAGE_NONE },
    { "Vertex", SG_SHADERSTAGE_VERTEX },
    { "Fragment", SG_SHADERSTAGE_FRAGMENT },
    { "Compute", SG_SHADERSTAGE_COMPUTE },
  };
  static const oak_bind_enum_variant_t uniform_types[] = {
    { "Invalid", SG_UNIFORMTYPE_INVALID },
    { "Float", SG_UNIFORMTYPE_FLOAT }, { "Float2", SG_UNIFORMTYPE_FLOAT2 },
    { "Float3", SG_UNIFORMTYPE_FLOAT3 }, { "Float4", SG_UNIFORMTYPE_FLOAT4 },
    { "Int", SG_UNIFORMTYPE_INT }, { "Int2", SG_UNIFORMTYPE_INT2 },
    { "Int3", SG_UNIFORMTYPE_INT3 }, { "Int4", SG_UNIFORMTYPE_INT4 },
    { "Mat4", SG_UNIFORMTYPE_MAT4 },
  };
  static const oak_bind_enum_variant_t image_types[] = {
    { "Default", _SG_IMAGETYPE_DEFAULT },
    { "Image2D", SG_IMAGETYPE_2D },
    { "Cube", SG_IMAGETYPE_CUBE },
    { "Image3D", SG_IMAGETYPE_3D },
    { "Array", SG_IMAGETYPE_ARRAY },
  };
  static const oak_bind_enum_variant_t sample_types[] = {
    { "Default", _SG_IMAGESAMPLETYPE_DEFAULT },
    { "Float", SG_IMAGESAMPLETYPE_FLOAT },
    { "Depth", SG_IMAGESAMPLETYPE_DEPTH },
    { "SInt", SG_IMAGESAMPLETYPE_SINT },
    { "UInt", SG_IMAGESAMPLETYPE_UINT },
    { "UnfilterableFloat", SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT },
  };
  static const oak_bind_enum_variant_t sampler_types[] = {
    { "Default", _SG_SAMPLERTYPE_DEFAULT },
    { "Filtering", SG_SAMPLERTYPE_FILTERING },
    { "Comparison", SG_SAMPLERTYPE_COMPARISON },
    { "NonFiltering", SG_SAMPLERTYPE_NONFILTERING },
  };

  if (OAKS_ENUM(opts, m, "Backend", backends) != 0 ||
      OAKS_ENUM(opts, m, "PixelFormat", pixel_formats) != 0 ||
      OAKS_ENUM(opts, m, "PrimitiveType", primitive_types) != 0 ||
      OAKS_ENUM(opts, m, "IndexType", index_types) != 0 ||
      OAKS_ENUM(opts, m, "VertexFormat", vertex_formats) != 0 ||
      OAKS_ENUM(opts, m, "VertexStep", vertex_steps) != 0 ||
      OAKS_ENUM(opts, m, "LoadAction", load_actions) != 0 ||
      OAKS_ENUM(opts, m, "StoreAction", store_actions) != 0 ||
      OAKS_ENUM(opts, m, "Filter", filters) != 0 ||
      OAKS_ENUM(opts, m, "Wrap", wraps) != 0 ||
      OAKS_ENUM(opts, m, "CullMode", cull_modes) != 0 ||
      OAKS_ENUM(opts, m, "FaceWinding", face_windings) != 0 ||
      OAKS_ENUM(opts, m, "CompareFunc", compare_funcs) != 0 ||
      OAKS_ENUM(opts, m, "BlendFactor", blend_factors) != 0 ||
      OAKS_ENUM(opts, m, "BlendOp", blend_ops) != 0 ||
      OAKS_ENUM(opts, m, "ShaderStage", shader_stages) != 0 ||
      OAKS_ENUM(opts, m, "UniformType", uniform_types) != 0 ||
      OAKS_ENUM(opts, m, "ImageType", image_types) != 0 ||
      OAKS_ENUM(opts, m, "ImageSampleType", sample_types) != 0 ||
      OAKS_ENUM(opts, m, "SamplerType", sampler_types) != 0)
    return -1;

  g_t_buffer = oak_bind_type(opts, m, OAK_BIND_TYPE_VALUE, "Buffer");
  g_t_image = oak_bind_type(opts, m, OAK_BIND_TYPE_VALUE, "Image");
  g_t_sampler = oak_bind_type(opts, m, OAK_BIND_TYPE_VALUE, "Sampler");
  g_t_shader = oak_bind_type(opts, m, OAK_BIND_TYPE_VALUE, "Shader");
  g_t_pipeline = oak_bind_type(opts, m, OAK_BIND_TYPE_VALUE, "Pipeline");
  g_t_view = oak_bind_type(opts, m, OAK_BIND_TYPE_VALUE, "View");
  if (!g_t_buffer || !g_t_image || !g_t_sampler || !g_t_shader ||
      !g_t_pipeline || !g_t_view)
    return -1;

  const oak_bind_type_ref_t r_buffer = OAK_BIND_NATIVE(g_t_buffer);
  const oak_bind_type_ref_t r_image = OAK_BIND_NATIVE(g_t_image);
  const oak_bind_type_ref_t r_sampler = OAK_BIND_NATIVE(g_t_sampler);
  const oak_bind_type_ref_t r_shader = OAK_BIND_NATIVE(g_t_shader);
  const oak_bind_type_ref_t r_pipeline = OAK_BIND_NATIVE(g_t_pipeline);
  const oak_bind_type_ref_t r_view = OAK_BIND_NATIVE(g_t_view);

  const oak_bind_type_ref_t p_any[] = { OAKS_ANY };
  const oak_bind_type_ref_t p_buffer[] = { r_buffer };
  const oak_bind_type_ref_t p_image[] = { r_image };
  const oak_bind_type_ref_t p_sampler[] = { r_sampler };
  const oak_bind_type_ref_t p_shader[] = { r_shader };
  const oak_bind_type_ref_t p_pipeline[] = { r_pipeline };
  const oak_bind_type_ref_t p_view[] = { r_view };
  const oak_bind_type_ref_t p_buffer_arr[] = { r_buffer, OAKS_ANY };
  const oak_bind_type_ref_t p_num_arr[] = { OAKS_NUM, OAKS_ANY };
  const oak_bind_type_ref_t p_num3[] = { OAKS_NUM, OAKS_NUM, OAKS_NUM };
  const oak_bind_type_ref_t p_rect[] = { OAKS_NUM, OAKS_NUM, OAKS_NUM,
                                                OAKS_NUM, OAKS_BOOL };

  if (oaks_fn(opts, m, "setup", oaks_gfx_setup, OAKS_VOID, p_any, 1) != 0 ||
      oaks_fn(opts, m, "shutdown", oaks_gfx_shutdown, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "is_valid", oaks_gfx_is_valid, OAKS_BOOL, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "query_backend", oaks_gfx_backend, OAKS_NUM, OAK_NULL, 0) != 0)
    return -1;

  if (oaks_fn(opts, m, "make_buffer", oaks_make_buffer, r_buffer, p_any, 1) != 0 ||
      oaks_fn(opts, m, "make_image", oaks_make_image, r_image, p_any, 1) != 0 ||
      oaks_fn(opts, m, "make_sampler", oaks_make_sampler, r_sampler, p_any, 1) != 0 ||
      oaks_fn(opts, m, "make_shader", oaks_make_shader, r_shader, p_any, 1) != 0 ||
      oaks_fn(opts, m, "make_pipeline", oaks_make_pipeline, r_pipeline, p_any, 1) != 0 ||
      oaks_fn(opts, m, "make_view", oaks_make_view, r_view, p_any, 1) != 0)
    return -1;

  if (oaks_fn(opts, m, "destroy_buffer", oaks_destroy_buffer, OAKS_VOID, p_buffer, 1) != 0 ||
      oaks_fn(opts, m, "destroy_image", oaks_destroy_image, OAKS_VOID, p_image, 1) != 0 ||
      oaks_fn(opts, m, "destroy_sampler", oaks_destroy_sampler, OAKS_VOID, p_sampler, 1) != 0 ||
      oaks_fn(opts, m, "destroy_shader", oaks_destroy_shader, OAKS_VOID, p_shader, 1) != 0 ||
      oaks_fn(opts, m, "destroy_pipeline", oaks_destroy_pipeline, OAKS_VOID, p_pipeline, 1) != 0 ||
      oaks_fn(opts, m, "destroy_view", oaks_destroy_view, OAKS_VOID, p_view, 1) != 0)
    return -1;

  if (oaks_fn(opts, m, "invalid_buffer", oaks_invalid_buffer, r_buffer, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "invalid_image", oaks_invalid_image, r_image, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "invalid_sampler", oaks_invalid_sampler, r_sampler, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "invalid_shader", oaks_invalid_shader, r_shader, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "invalid_pipeline", oaks_invalid_pipeline, r_pipeline, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "invalid_view", oaks_invalid_view, r_view, OAK_NULL, 0) != 0)
    return -1;

  if (oaks_fn(opts, m, "update_buffer_f32", oaks_update_buffer, OAKS_VOID, p_buffer_arr, 2) != 0 ||
      oaks_fn(opts, m, "append_buffer_f32", oaks_append_buffer, OAKS_NUM, p_buffer_arr, 2) != 0)
    return -1;

  if (oaks_fn(opts, m, "begin_pass", oaks_begin_pass, OAKS_VOID, p_any, 1) != 0 ||
      oaks_fn(opts, m, "end_pass", oaks_end_pass, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "apply_pipeline", oaks_apply_pipeline, OAKS_VOID, p_pipeline, 1) != 0 ||
      oaks_fn(opts, m, "apply_bindings", oaks_apply_bindings, OAKS_VOID, p_any, 1) != 0 ||
      oaks_fn(opts, m, "apply_uniforms_f32", oaks_apply_uniforms, OAKS_VOID, p_num_arr, 2) != 0 ||
      oaks_fn(opts, m, "apply_viewport", oaks_apply_viewport, OAKS_VOID, p_rect, 5) != 0 ||
      oaks_fn(opts, m, "apply_scissor_rect", oaks_apply_scissor, OAKS_VOID, p_rect, 5) != 0 ||
      oaks_fn(opts, m, "draw", oaks_draw, OAKS_VOID, p_num3, 3) != 0 ||
      oaks_fn(opts, m, "commit", oaks_commit, OAKS_VOID, OAK_NULL, 0) != 0)
    return -1;

  return 0;
}
