/*
 * sokol-oak -- shared internals of the plugin.
 *
 * Nothing here is part of the package's Oak-facing surface; that is defined
 * by the .oak stubs in src/ and by what bind() registers. This header is only
 * what the four binding translation units need from each other.
 */
#ifndef SOKOL_OAK_H
#define SOKOL_OAK_H

#include "oak_bind.h"
#include "oak_native.h"
#include "oak_value.h"
#include "oak_vm.h"

#include <stdint.h>

/* Registration entry points, one per module. Each is handed the compile
 * options and the module handle its bindings belong to, and returns 0 on
 * success -- the same contract as oak_plugin_t::bind, so a failure anywhere
 * refuses the whole plugin rather than leaving a half-bound module. */
int oaks_bind_app(oak_compile_options_t* opts, oak_bind_module_t* m);
int oaks_bind_gfx(oak_compile_options_t* opts, oak_bind_module_t* m);
int oaks_bind_time(oak_compile_options_t* opts, oak_bind_module_t* m);
int oaks_bind_imgui(oak_compile_options_t* opts, oak_bind_module_t* m);

/* ------------------------------------------------------- reading records -- */

/*
 * Descriptors cross the boundary as ordinary Oak records, so reading one is
 * looking a field up by name. The accessors below all take a default and
 * return it when the field is absent or holds the wrong type, which is what
 * makes a partially-filled descriptor safe: the Oak-side constructors supply
 * every field, but a record built by hand need not, and a missing field
 * should mean "the default" rather than a crash.
 */

/* The named field of `rec`, or none when `rec` is not a record or has no such
 * field. */
oak_value_t oaks_field(oak_value_t rec, const char* name);

/* Numbers reach C as either an i32 or an f32 value; these read both. */
float oaks_num(oak_value_t v, float fallback);
int oaks_int(oak_value_t v, int fallback);

int oaks_field_int(oak_value_t rec, const char* name, int fallback);
float oaks_field_num(oak_value_t rec, const char* name, float fallback);
int oaks_field_bool(oak_value_t rec, const char* name, int fallback);
/* Returns NULL for a missing field, and never an empty string, so a caller
 * can pass the result straight to a sokol `label` field. */
const char* oaks_field_str(oak_value_t rec, const char* name);
/* The field as a record/array value, or none. */
oak_value_t oaks_field_obj(oak_value_t rec, const char* name);

/* ------------------------------------------------------- reading arrays -- */

/* Element count of an array value; 0 for anything else. */
usize oaks_len(oak_value_t arr);
/* Element `i`, or none when out of range. */
oak_value_t oaks_at(oak_value_t arr, usize i);

/* Copy an Oak number array into a C array, converting as it goes. Each writes
 * at most `max` elements and returns how many it wrote. */
usize oaks_floats(oak_value_t arr, float* out, usize max);
usize oaks_u16s(oak_value_t arr, uint16_t* out, usize max);
usize oaks_u32s(oak_value_t arr, uint32_t* out, usize max);
usize oaks_u8s(oak_value_t arr, uint8_t* out, usize max);

/* ------------------------------------------------------ handles as values -- */

/*
 * Every sokol_gfx resource handle is a struct wrapping a single uint32_t id,
 * and every one of them is bound to Oak as an inline value type whose payload
 * is that id. So the conversion is the same two lines for all six, and these
 * macros keep the six pairs of accessors from being six pairs of functions.
 *
 * Packing the id into the payload rather than a pointer is what makes a
 * handle copyable and free of ownership: an Oak `Buffer` is the id, exactly
 * as an `sg_buffer` is.
 */
#define OAKS_HANDLE_TO_VALUE(h) oak_native_value_new((void*)(uintptr_t)(h).id)
#define OAKS_VALUE_TO_HANDLE(T, v)                                              \
  ((T){ .id = (uint32_t)(uintptr_t)oak_native_value(v) })

/* True when `v` is an inline native value, i.e. one of our handles. Guards
 * OAKS_VALUE_TO_HANDLE, whose oak_native_value asserts on anything else. */
int oaks_is_handle(oak_value_t v);

/* ------------------------------------------------------------- binding -- */

/* Register one free function on `m`. `params` may be NULL when `nparams` is
 * 0. Returns 0 on success. */
int oaks_fn(oak_compile_options_t* opts,
           oak_bind_module_t* m,
           const char* name,
           oak_native_fn_t impl,
           oak_bind_type_ref_t ret,
           const oak_bind_type_ref_t* params,
           usize nparams);

/* Register an enum and its variants.
 *
 * Every variant carries sokol's own constant rather than its position. The
 * .oak stub can only name variants -- Oak has no syntax for their values --
 * so the binding is where the value comes from, and a stub that disagrees
 * about the names is reported at compile time. Spelling the constants out
 * means sokol renumbering an enum cannot silently change what an Oak program
 * means. */
int oaks_enum(oak_compile_options_t* opts,
             oak_bind_module_t* m,
             const char* name,
             const oak_bind_enum_variant_t* variants,
             int count);

/* Declare a variant table and register it, so the two stay adjacent. */
#define OAKS_ENUM(opts, m, name, table)                                         \
  oaks_enum((opts), (m), (name), (table),                                       \
           (int)(sizeof(table) / sizeof((table)[0])))

/* A record parameter. The stub declares the precise Oak record type, which is
 * what call sites are checked against; the binding cannot name a type that
 * only exists in Oak source, so it accepts anything and the implementation
 * reports a bad argument itself. */
#define OAKS_ANY OAK_BIND_SCALAR(OAK_TYPE_VOID)
#define OAKS_NUM OAK_BIND_SCALAR(OAK_TYPE_NUMBER)
#define OAKS_STR OAK_BIND_SCALAR(OAK_TYPE_STRING)
#define OAKS_BOOL OAK_BIND_SCALAR(OAK_TYPE_BOOL)
#define OAKS_VOID OAK_BIND_SCALAR(OAK_TYPE_VOID)
#define OAKS_FN OAK_BIND_SCALAR(OAK_TYPE_FN)

#endif /* SOKOL_OAK_H */
