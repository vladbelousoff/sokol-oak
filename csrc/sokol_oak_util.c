/*
 * Reading Oak values from C.
 *
 * The descriptors in this package are plain Oak records rather than native
 * ones, which is what lets the .oak stubs give every field a documented type
 * and a default. The cost is here: a record arrives as a name/value table and
 * has to be read field by field.
 */
#include "sokol_oak.h"

#include <string.h>

oak_value_t oaks_field(const oak_value_t rec, const char* name)
{
  if (!oak_is_record(rec))
    return OAK_VALUE_NONE;
  const oak_obj_record_t* r = oak_as_record(rec);
  for (int i = 0; i < r->field_count; ++i)
  {
    if (r->field_name_ptrs[i] && strcmp(r->field_name_ptrs[i], name) == 0)
      return r->fields[i];
  }
  return OAK_VALUE_NONE;
}

/* Oak keeps whole numbers as i32 and fractional ones as f32, and a literal
 * like `1` in a float field arrives as the former. Reading both is therefore
 * the rule rather than a tolerance: `sample_count : 1` and `min_lod : 0.0`
 * are the same kind of field to the caller. */
float oaks_num(const oak_value_t v, const float fallback)
{
  if (oak_is_i32(v))
    return (float)oak_as_i32(v);
  if (oak_is_f32(v))
    return oak_as_f32(v);
  return fallback;
}

int oaks_int(const oak_value_t v, const int fallback)
{
  if (oak_is_i32(v))
    return oak_as_i32(v);
  if (oak_is_f32(v))
    return (int)oak_as_f32(v);
  return fallback;
}

int oaks_field_int(const oak_value_t rec, const char* name, const int fallback)
{
  return oaks_int(oaks_field(rec, name), fallback);
}

float oaks_field_num(const oak_value_t rec, const char* name, const float fallback)
{
  return oaks_num(oaks_field(rec, name), fallback);
}

int oaks_field_bool(const oak_value_t rec, const char* name, const int fallback)
{
  const oak_value_t v = oaks_field(rec, name);
  return oak_is_bool(v) ? oak_as_bool(v) : fallback;
}

/* An empty Oak string and an absent field both mean "unset" for every string
 * a descriptor carries -- labels, entry points, GLSL names -- and sokol reads
 * NULL as unset. Collapsing the two here keeps that check out of every
 * caller. */
const char* oaks_field_str(const oak_value_t rec, const char* name)
{
  const oak_value_t v = oaks_field(rec, name);
  if (!oak_is_string(v))
    return OAK_NULL;
  const oak_obj_string_t* s = oak_as_string(v);
  return s->length ? s->chars : OAK_NULL;
}

oak_value_t oaks_field_obj(const oak_value_t rec, const char* name)
{
  return oaks_field(rec, name);
}

usize oaks_len(const oak_value_t arr)
{
  return oak_is_array(arr) ? oak_as_array(arr)->length : 0u;
}

oak_value_t oaks_at(const oak_value_t arr, const usize i)
{
  if (!oak_is_array(arr))
    return OAK_VALUE_NONE;
  const oak_obj_array_t* a = oak_as_array(arr);
  return i < a->length ? a->items[i] : OAK_VALUE_NONE;
}

/* The four copies below differ only in the destination type. Each stops at
 * `max` rather than reporting an overflow: the caller sized the destination
 * from a sokol limit, and silently binding the first N is the same thing
 * sokol does with an over-long array. */
#define OAKS_COPY_BODY(CTYPE, CONV)                                             \
  do                                                                           \
  {                                                                            \
    const usize n = oaks_len(arr) < max ? oaks_len(arr) : max;                    \
    for (usize i = 0; i < n; ++i)                                              \
      out[i] = (CTYPE)(CONV);                                                  \
    return n;                                                                  \
  } while (0)

usize oaks_floats(const oak_value_t arr, float* out, const usize max)
{
  OAKS_COPY_BODY(float, oaks_num(oaks_at(arr, i), 0.0f));
}

usize oaks_u16s(const oak_value_t arr, uint16_t* out, const usize max)
{
  OAKS_COPY_BODY(uint16_t, oaks_int(oaks_at(arr, i), 0));
}

usize oaks_u32s(const oak_value_t arr, uint32_t* out, const usize max)
{
  OAKS_COPY_BODY(uint32_t, oaks_int(oaks_at(arr, i), 0));
}

usize oaks_u8s(const oak_value_t arr, uint8_t* out, const usize max)
{
  OAKS_COPY_BODY(uint8_t, oaks_int(oaks_at(arr, i), 0));
}

#undef OAKS_COPY_BODY

int oaks_is_handle(const oak_value_t v)
{
  return oak_is_native_value(v);
}

int oaks_fn(oak_compile_options_t* opts,
           oak_bind_module_t* m,
           const char* name,
           oak_native_fn_t impl,
           const oak_bind_type_ref_t ret,
           const oak_bind_type_ref_t* params,
           const usize nparams)
{
  oak_bind_fn_t fn;
  memset(&fn, 0, sizeof(fn));
  fn.name = name;
  fn.impl = impl;
  fn.return_type = ret;
  fn.param_types = params;
  fn.param_count = nparams;
  return oak_bind_fn(opts, m, &fn);
}

int oaks_enum(oak_compile_options_t* opts,
             oak_bind_module_t* m,
             const char* name,
             const oak_bind_enum_variant_t* variants,
             const int count)
{
  oak_bind_enum_t* e = oak_bind_enum(opts, m, name);
  if (!e)
    return -1;
  for (int i = 0; i < count; ++i)
  {
    if (oak_bind_enum_variant(e, variants[i].name, variants[i].value) != 0)
      return -1;
  }
  return 0;
}
