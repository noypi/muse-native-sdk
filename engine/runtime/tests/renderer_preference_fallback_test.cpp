// T2 — engine-side `renderer_preference_fallback` gate (AC-GRAPHICS-041 /
// TC-GRAPHICS-043 engine half). Binds ONLY to the T2 `Interface:` line;
// never to implementation.
// Hermetic: C++17 + the `runtime/renderer_preference.h` public header only
// (which the coder publishes per the T2 plan item); no EGL/Skia/GPU calls on
// host, no network, no env/config reads, no filesystem, no live services.
// Availability is injected per-call via the `eglAvailable` / `skiaAvailable`
// arguments — there is no probe seam to fake.
//
// Depends on: T1 (requested-vs-effective reporting semantics).
//
// Expected red: this TU does not compile until the coder publishes
// `engine/runtime/include/runtime/renderer_preference.h` declaring
// `ResolveRendererFallback` —
// `runtime/renderer_preference.h: No such file or directory`.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/renderer_preference.h"

// --- harness cases (convention copied from present_fallback_test.cpp) --------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T2 cases ----------------------------------------------------------------

// Named case: null requested means "cpu" — resolves cpu, no trigger, true.
static bool check_null_requested_means_cpu() {
  char effective[16];
  std::memset(effective, 0x7F, sizeof(effective));
  std::string trigger = "untouched";
  bool ok = ResolveRendererFallback(nullptr, false, false, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "cpu") == 0 && trigger.empty();
}

// Named case: requested cpu resolves cpu with empty trigger even when
// nothing is available — a frame path is always resolved, never blank.
static bool check_cpu_request_always_cpu_no_trigger() {
  char effective[16] = {};
  std::string trigger = "untouched";
  bool ok = ResolveRendererFallback("cpu", false, false, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "cpu") == 0 && trigger.empty();
}

// Named case: requested egl with egl available keeps egl, empty trigger.
static bool check_egl_available_keeps_egl() {
  char effective[16] = {};
  std::string trigger = "untouched";
  bool ok = ResolveRendererFallback("egl", true, false, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "egl") == 0 && trigger.empty();
}

// Named case: requested skia with skia available keeps skia, empty trigger.
static bool check_skia_available_keeps_skia() {
  char effective[16] = {};
  std::string trigger = "untouched";
  bool ok = ResolveRendererFallback("skia", false, true, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "skia") == 0 && trigger.empty();
}

// Named case: unavailable egl downgrades to cpu with the named
// `renderer-downgrade:egl-unavailable` trigger; returns true so the caller
// takes the proven CPU push (never blank, never crash).
static bool check_egl_unavailable_downgrades_with_named_trigger() {
  char effective[16] = {};
  std::string trigger;
  bool ok = ResolveRendererFallback("egl", false, true, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "cpu") == 0 &&
         trigger == "renderer-downgrade:egl-unavailable";
}

// Named case: unavailable skia downgrades to cpu with the named
// `renderer-downgrade:skia-unavailable` trigger; returns true so the caller
// takes the proven CPU push.
static bool check_skia_unavailable_downgrades_with_named_trigger() {
  char effective[16] = {};
  std::string trigger;
  bool ok = ResolveRendererFallback("skia", true, false, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "cpu") == 0 &&
         trigger == "renderer-downgrade:skia-unavailable";
}

// Named case: null effectiveOut is tolerated — still returns true, no crash.
static bool check_null_effective_out_tolerated() {
  std::string trigger;
  bool ok = ResolveRendererFallback("egl", false, false, nullptr, 0, &trigger);
  return ok && !trigger.empty();
}

// Named case: null fallbackTrigger is tolerated — still returns true and
// still resolves the path, no crash.
static bool check_null_trigger_tolerated() {
  char effective[16] = {};
  bool ok = ResolveRendererFallback("skia", false, false, effective,
                                    sizeof(effective), nullptr);
  return ok && std::strcmp(effective, "cpu") == 0;
}

// Named case: both out-params null is tolerated — still returns true,
// no crash.
static bool check_both_out_params_null_tolerated() {
  return ResolveRendererFallback("cpu", true, true, nullptr, 0, nullptr);
}

// Named case: unknown requested value fails closed to cpu with a non-empty
// trigger — a frame path is always resolved, never blank.
// ASSUMPTION A-C1: the Interface line is silent on unknown input; fail-closed
// to the proven CPU push (rather than echoing the unknown value) is the most
// defensible behavior under AC-GRAPHICS-034's never-blank rule.
static bool check_unknown_requested_fails_closed_to_cpu() {
  char effective[16] = {};
  std::string trigger;
  bool ok = ResolveRendererFallback("vulkan", true, true, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "cpu") == 0 && !trigger.empty();
}

// Named case: zero-length effective buffer never overflows — returns true,
// no crash.
// ASSUMPTION A-C2: with no room to write, the resolve still reports success
// (the trigger carries the downgrade); the caller sizes the buffer.
static bool check_zero_length_buffer_no_crash() {
  char effective[16];
  std::memset(effective, 'X', sizeof(effective));
  std::string trigger;
  bool ok = ResolveRendererFallback("egl", false, false, effective, 0,
                                    &trigger);
  return ok && !trigger.empty();
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_null_requested_means_cpu);
  RUN_CASE(check_cpu_request_always_cpu_no_trigger);
  RUN_CASE(check_egl_available_keeps_egl);
  RUN_CASE(check_skia_available_keeps_skia);
  RUN_CASE(check_egl_unavailable_downgrades_with_named_trigger);
  RUN_CASE(check_skia_unavailable_downgrades_with_named_trigger);
  RUN_CASE(check_null_effective_out_tolerated);
  RUN_CASE(check_null_trigger_tolerated);
  RUN_CASE(check_both_out_params_null_tolerated);
  RUN_CASE(check_unknown_requested_fails_closed_to_cpu);
  RUN_CASE(check_zero_length_buffer_no_crash);
  if (failures > 0) {
    std::printf("renderer_preference_fallback_test: %d failing\n", failures);
    return 1;
  }
  std::printf("renderer_preference_fallback_test: all passing\n");
  return 0;
}
