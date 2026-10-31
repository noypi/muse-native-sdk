// T1 skeleton — FR-PVIEWS-005 / AC-PVIEWS-007 / TC-PVIEWS-007.
// Binds ONLY to the plan T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + clipping/{surface_clipper.h, offscreen_fallback.h} only;
// no ASurfaceControl, no Vulkan, no device, no network, no env/config reads,
// no filesystem, no live services, no device touch.
//
// Skeleton is intentionally GREEN: the T2/T3 cases below are listed as
// pending (DISABLED-style) and always pass until T2/T3 implement behavior.
// T2/T3 will replace each pending body with a real hermetic assertion.
#include <cstdio>

#include "../clipping/offscreen_fallback.h"
#include "../clipping/surface_clipper.h"

// T2: rounded-rect clip covers the view bounds with zero bleed (PENDING).
static bool check_rounded_rect_clip_pending() {
  std::printf("  (PENDING: T2 implements rounded-rect clip)\n");
  return true;
}

// T2: arbitrary-path clip covers the view bounds with zero bleed (PENDING).
static bool check_arbitrary_path_clip_pending() {
  std::printf("  (PENDING: T2 implements arbitrary-path clip)\n");
  return true;
}

// T2: API < 31 routes to fallbackRequired:true (PENDING).
static bool check_api_below31_routing_pending() {
  std::printf("  (PENDING: T2 implements API<31 routing)\n");
  return true;
}

// T2: custom-shader hint routes to fallbackRequired:true (PENDING).
static bool check_custom_shader_routing_pending() {
  std::printf("  (PENDING: T2 implements custom-shader routing)\n");
  return true;
}

// T2: invalid path is rejected without touching the surface seam (PENDING).
static bool check_invalid_path_pending() {
  std::printf("  (PENDING: T2 implements invalid-path rejection)\n");
  return true;
}

// T3: fallback composite matches golden within tolerance (PENDING).
static bool check_fallback_golden_diff_pending() {
  std::printf("  (PENDING: T3 implements fallback golden diff)\n");
  return true;
}

// T3: fence timeout returns typed FenceTimeout error (PENDING).
static bool check_fence_timeout_pending() {
  std::printf("  (PENDING: T3 implements fence timeout)\n");
  return true;
}

// T3: import failure returns typed ImportFailed error (PENDING).
static bool check_import_failure_pending() {
  std::printf("  (PENDING: T3 implements import failure)\n");
  return true;
}

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_rounded_rect_clip_pending);
  RUN_CASE(check_arbitrary_path_clip_pending);
  RUN_CASE(check_api_below31_routing_pending);
  RUN_CASE(check_custom_shader_routing_pending);
  RUN_CASE(check_invalid_path_pending);
  RUN_CASE(check_fallback_golden_diff_pending);
  RUN_CASE(check_fence_timeout_pending);
  RUN_CASE(check_import_failure_pending);
  if (failures > 0) {
    std::printf("clipping_test: %d failing\n", failures);
    return 1;
  }
  std::printf("clipping_test: all passing (skeleton; T2/T3 cases pending)\n");
  return 0;
}
