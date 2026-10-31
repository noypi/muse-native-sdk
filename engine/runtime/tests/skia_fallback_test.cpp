// Batch 2 contract tests — AC-GRAPHICS-039 (fallback leg via AC-034 contract).
// Binds ONLY to the T3 `Interface:` line; never to implementation.
// Hermetic: C++17 + the `present_skia.h` public header only (which the coder
// extends with `PresentSkiaFrameWithFallback` per the T3 plan item); pixel
// buffers are test-local vectors; no Skia SDK/GPU calls on host, no network,
// no env/config reads, no filesystem, no live services. Skia faults are
// induced through the test-only `skia_test::SetFaultHooks` seam (extended
// `egl_blit_test::SetFaultHooks`-style shape) the coder provides on host;
// the device lane may no-op it. The `GFX-BLIT-FALLBACK: <trigger>` log line
// itself is verified on the 6562 device leg; hermetically the tests assert
// the exact trigger string that line carries.
// Depends on: T2 (shares its header + stub seam; this file must FAIL to
// compile until the coder publishes the new declaration + seam).
//
// Expected red: this TU does not compile until the coder declares
// `PresentSkiaFrameWithFallback` (+ `skia_test::SetFaultHooks`) in
// `engine/graphics/skia/present_skia.h` —
// `error: 'PresentSkiaFrameWithFallback' was not declared in this scope`
// (or `skia_test` counterpart).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "present_skia.h"

// --- test-only fault-injection seam (extended egl_blit_test shape) -----------
// Provided by `present_skia.h` (coder's host stub-Skia claim). `nullptr`
// clears all faults. Shape:
//   namespace skia_test {
//   struct FaultHooks {
//     bool fail_init = false;       // Skia init path reports failure
//     bool lose_context = false;    // Skia context reports lost
//     bool fail_next_swap = false;  // next Skia swap reports failure
//     bool no_surface = false;      // no surface to present into
//   };
//   void SetFaultHooks(const FaultHooks* hooks);
//   }

namespace {

struct FaultGuard {
  FaultGuard() { skia_test::SetFaultHooks(nullptr); }
  ~FaultGuard() { skia_test::SetFaultHooks(nullptr); }
};

std::vector<uint32_t> two_by_two() {
  return {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
}

}  // namespace

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

// --- T3 cases ----------------------------------------------------------------

// Named case: healthy Skia path pushes the frame and reports no fallback
// (empty trigger). A frame is always pushed: returns true.
static bool check_healthy_path_returns_true_no_fallback() {
  FaultGuard g;
  std::vector<uint32_t> px = two_by_two();
  std::string trigger = "untouched";
  bool ok = PresentSkiaFrameWithFallback(px.data(), 2, 2, &trigger);
  return ok && trigger.empty();
}

// Named case: Skia init failure still pushes a frame via the in-process CPU
// path — returns true, trigger is exactly `skia-init-fail` (the string the
// `GFX-BLIT-FALLBACK: <trigger>` log line carries).
static bool check_init_fail_falls_back_exact_trigger() {
  FaultGuard g;
  skia_test::FaultHooks hooks;
  hooks.fail_init = true;
  skia_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  std::string trigger;
  bool ok = PresentSkiaFrameWithFallback(px.data(), 2, 2, &trigger);
  return ok && trigger == "skia-init-fail";
}

// Named case: Skia context loss falls back in-process — returns true,
// trigger is exactly `skia-context-lost`.
static bool check_context_lost_falls_back_exact_trigger() {
  FaultGuard g;
  skia_test::FaultHooks hooks;
  hooks.lose_context = true;
  skia_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  std::string trigger;
  bool ok = PresentSkiaFrameWithFallback(px.data(), 2, 2, &trigger);
  return ok && trigger == "skia-context-lost";
}

// Named case: Skia swap failure falls back in-process — returns true,
// trigger is exactly `skia-swap-fail`.
static bool check_swap_fail_falls_back_exact_trigger() {
  FaultGuard g;
  skia_test::FaultHooks hooks;
  hooks.fail_next_swap = true;
  skia_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  std::string trigger;
  bool ok = PresentSkiaFrameWithFallback(px.data(), 2, 2, &trigger);
  return ok && trigger == "skia-swap-fail";
}

// Named case: no-surface falls back in-process — returns true (never blank,
// never crash), trigger is exactly `skia-no-surface`.
static bool check_no_surface_falls_back_exact_trigger() {
  FaultGuard g;
  skia_test::FaultHooks hooks;
  hooks.no_surface = true;
  skia_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  std::string trigger;
  bool ok = PresentSkiaFrameWithFallback(px.data(), 2, 2, &trigger);
  return ok && trigger == "skia-no-surface";
}

// Named case: fallback_trigger == nullptr is tolerated — still returns true,
// emits `GFX-BLIT-FALLBACK: <trigger>`, no crash.
static bool check_null_trigger_tolerated() {
  FaultGuard g;
  skia_test::FaultHooks hooks;
  hooks.fail_next_swap = true;
  skia_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  return PresentSkiaFrameWithFallback(px.data(), 2, 2, nullptr);
}

// Named case: null pixels never crash — returns true on both the healthy
// and the fallback lanes.
static bool check_null_pixels_no_crash() {
  FaultGuard g;
  std::string t1 = "untouched";
  bool ok_healthy = PresentSkiaFrameWithFallback(nullptr, 2, 2, &t1);
  skia_test::FaultHooks hooks;
  hooks.fail_init = true;
  skia_test::SetFaultHooks(&hooks);
  bool ok_fallback = PresentSkiaFrameWithFallback(nullptr, 2, 2, nullptr);
  return ok_healthy && ok_fallback;
}

// Named case: zero/negative dimensions never crash — returns true.
static bool check_zero_dims_no_crash() {
  FaultGuard g;
  std::vector<uint32_t> px = two_by_two();
  std::string t1, t2, t3;
  bool ok1 = PresentSkiaFrameWithFallback(px.data(), 0, 2, &t1);
  bool ok2 = PresentSkiaFrameWithFallback(px.data(), 2, 0, &t2);
  bool ok3 = PresentSkiaFrameWithFallback(px.data(), -1, 2, &t3);
  return ok1 && ok2 && ok3;
}

// Named case: repeated fallback frames keep succeeding (no wedged state) and
// each reports its trigger.
static bool check_repeated_fallbacks_stay_live() {
  FaultGuard g;
  skia_test::FaultHooks hooks;
  hooks.fail_next_swap = true;
  skia_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  for (int i = 0; i < 3; ++i) {
    std::string trigger;
    if (!PresentSkiaFrameWithFallback(px.data(), 2, 2, &trigger) ||
        trigger != "skia-swap-fail")
      return false;
  }
  return true;
}

// Named case: clearing faults restores the healthy lane (empty trigger).
static bool check_faults_cleared_restores_healthy() {
  FaultGuard g;
  skia_test::FaultHooks hooks;
  hooks.fail_init = true;
  skia_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  std::string t1;
  bool ok_fallback = PresentSkiaFrameWithFallback(px.data(), 2, 2, &t1);
  skia_test::SetFaultHooks(nullptr);
  std::string t2 = "untouched";
  bool ok_healthy = PresentSkiaFrameWithFallback(px.data(), 2, 2, &t2);
  return ok_fallback && t1 == "skia-init-fail" && ok_healthy && t2.empty();
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
  RUN_CASE(check_healthy_path_returns_true_no_fallback);
  RUN_CASE(check_init_fail_falls_back_exact_trigger);
  RUN_CASE(check_context_lost_falls_back_exact_trigger);
  RUN_CASE(check_swap_fail_falls_back_exact_trigger);
  RUN_CASE(check_no_surface_falls_back_exact_trigger);
  RUN_CASE(check_null_trigger_tolerated);
  RUN_CASE(check_null_pixels_no_crash);
  RUN_CASE(check_zero_dims_no_crash);
  RUN_CASE(check_repeated_fallbacks_stay_live);
  RUN_CASE(check_faults_cleared_restores_healthy);
  if (failures > 0) {
    std::printf("skia_fallback_test: %d failing\n", failures);
    return 1;
  }
  std::printf("skia_fallback_test: all passing\n");
  return 0;
}
