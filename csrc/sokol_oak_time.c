/*
 * sokol_time.h -- a monotonic clock.
 *
 * Ticks are uint64_t in C but reach Oak as numbers, which are 32-bit. That is
 * not a problem for what the clock is used for: a tick count is only ever
 * meaningful as a difference, and the conversions below turn a difference
 * into seconds before it can grow large. Passing raw tick values around for
 * hours would eventually lose precision, which is why `since` exists and the
 * documentation points at it.
 */
#include "sokol_oak.h"

#include "sokol_time.h"

static oak_fn_call_result_t oaks_time_setup(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           const usize argc, oak_value_t* out)
{
  (void)call; (void)args; (void)argc; (void)out;
  stm_setup();
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_time_now(oak_native_call_t* call,
                                         const oak_value_t* args,
                                         const usize argc, oak_value_t* out)
{
  (void)call; (void)args; (void)argc;
  *out = OAK_VALUE_I32((int)stm_now());
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_time_since(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           const usize argc, oak_value_t* out)
{
  int start;
  if (!oak_arg_i32(call, args, argc, 0, &start))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_I32((int)stm_since((uint64_t)(uint32_t)start));
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t oaks_time_diff(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc, oak_value_t* out)
{
  int new_ticks, old_ticks;
  if (!oak_arg_i32(call, args, argc, 0, &new_ticks) ||
      !oak_arg_i32(call, args, argc, 1, &old_ticks))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_I32(
      (int)stm_diff((uint64_t)(uint32_t)new_ticks, (uint64_t)(uint32_t)old_ticks));
  return OAK_FN_CALL_OK;
}

/* The four conversions differ only in the sokol function they call. */
#define OAKS_TIME_CONV(fn_name, stm_call)                                       \
  static oak_fn_call_result_t fn_name(oak_native_call_t* call,                 \
                                      const oak_value_t* args,                 \
                                      const usize argc, oak_value_t* out)      \
  {                                                                            \
    int ticks;                                                                 \
    if (!oak_arg_i32(call, args, argc, 0, &ticks))                             \
      return OAK_FN_CALL_RUNTIME_ERROR;                                        \
    *out = OAK_VALUE_F32((float)stm_call((uint64_t)(uint32_t)ticks));          \
    return OAK_FN_CALL_OK;                                                     \
  }

OAKS_TIME_CONV(oaks_time_sec, stm_sec)
OAKS_TIME_CONV(oaks_time_ms, stm_ms)
OAKS_TIME_CONV(oaks_time_us, stm_us)
OAKS_TIME_CONV(oaks_time_ns, stm_ns)

int oaks_bind_time(oak_compile_options_t* opts, oak_bind_module_t* m)
{
  const oak_bind_type_ref_t p_num[] = { OAKS_NUM };
  const oak_bind_type_ref_t p_num2[] = { OAKS_NUM, OAKS_NUM };

  if (oaks_fn(opts, m, "setup", oaks_time_setup, OAKS_VOID, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "now", oaks_time_now, OAKS_NUM, OAK_NULL, 0) != 0 ||
      oaks_fn(opts, m, "since", oaks_time_since, OAKS_NUM, p_num, 1) != 0 ||
      oaks_fn(opts, m, "diff", oaks_time_diff, OAKS_NUM, p_num2, 2) != 0 ||
      oaks_fn(opts, m, "sec", oaks_time_sec, OAKS_NUM, p_num, 1) != 0 ||
      oaks_fn(opts, m, "ms", oaks_time_ms, OAKS_NUM, p_num, 1) != 0 ||
      oaks_fn(opts, m, "us", oaks_time_us, OAKS_NUM, p_num, 1) != 0 ||
      oaks_fn(opts, m, "ns", oaks_time_ns, OAKS_NUM, p_num, 1) != 0)
    return -1;
  return 0;
}
