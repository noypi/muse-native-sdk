// Batch 2 contract tests — FR-GRAPHICS-012 / AC-GRAPHICS-034.
// Binds ONLY to the T3 `Interface:` line; never to implementation.
// Hermetic: C++17 + the `runtime/egl_blit.h` public header only (which
// declares `PresentFrameWithFallback` per the T3 plan item); opaque window
// handles are test-local fakes; no NDK/EGL/ANativeWindow calls on host, no
// network, no env/config reads, no filesystem, no live services. EGL faults
// are induced through the same test-only `egl_blit_test::SetFaultHooks`
// seam as T2 (coder's host stub-EGL claim; see ASSUMPTION B1 in
// unit-tests/T3.md). The `GFX-BLIT-FALLBACK` log line itself is verified on
// the 6562 device leg (TC-GRAPHICS-034 exact check); hermetically the tests
// assert the trigger string that line carries.
// Depends on: T2 (shares its header + stub seam; this file must FAIL to
// compile until the coder publishes them).
//
// Expected red: this TU does not compile until the coder publishes
// `engine/runtime/include/runtime/egl_blit.h` (+ fallback branch + CMake
// registration) — `runtime/egl_blit.h: No such file or directory`.
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime/egl_blit.h"

// --- test-only fault-injection seam (shared with T2) ------------------------
// Provided by `runtime/egl_blit.h` (coder's host stub-EGL claim; same header
// serves both test executables — they link separately, so no ODR clash).
// Device lane may no-op it. `nullptr` clears all faults. Shape (also stated
// in the T2 test file):
//   namespace egl_blit_test {
//   struct FaultHooks {
//     bool fail_init = false;       // EGL init path reports failure
//     bool lose_context = false;    // context reports lost
//     bool fail_next_swap = false;  // next eglSwapBuffers reports failure
//     bool no_surface = false;      // backgrounded/destroyed surface
//   };
//   void SetFaultHooks(const FaultHooks* hooks);
//   }

namespace {

struct FaultGuard {
  FaultGuard() { egl_blit_test::SetFaultHooks(nullptr); }
  ~FaultGuard() {
    egl_blit_test::SetFaultHooks(nullptr);
    EglBlitShutdown();
  }
};

bool trigger_mentions(const std::string& trigger, const char* needle) {
  if (trigger.empty() || needle == nullptr) return false;
  std::string hay = trigger;
  std::string ndl = needle;
  for (char& c : hay) c = static_cast<char>(std::tolower(c));
  for (char& c : ndl) c = static_cast<char>(std::tolower(c));
  return hay.find(ndl) != std::string::npos;
}

std::vector<uint32_t> two_by_two() {
  return {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
}

}  // namespace

// --- harness cases (convention copied from present_frame_test.cpp) -----------

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

// Named case: healthy EGL path pushes the frame and reports no fallback
// (empty trigger). A frame is always pushed: returns true.
static bool check_healthy_path_returns_true_no_fallback() {
  FaultGuard g;
  std::vector<uint32_t> px = two_by_two();
  std::string trigger = "untouched";
  bool ok = PresentFrameWithFallback(px.data(), 2, 2, &trigger);
  return ok && trigger.empty();
}

// Named case: EGL init failure still pushes a frame via the in-process CPU
// SurfaceView path — returns true, trigger names the EGL init failure
// (the string the `GFX-BLIT-FALLBACK: <trigger>` log line carries).
static bool check_init_fail_falls_back_in_process() {
  FaultGuard g;
  egl_blit_test::FaultHooks hooks;
  hooks.fail_init = true;
  egl_blit_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  std::string trigger;
  bool ok = PresentFrameWithFallback(px.data(), 2, 2, &trigger);
  return ok && !trigger.empty() &&
         (trigger_mentions(trigger, "init") || trigger_mentions(trigger, "egl"));
}

// Named case: EGL context loss falls back in-process — returns true, trigger
// names the context failure.
static bool check_context_lost_falls_back_in_process() {
  FaultGuard g;
  egl_blit_test::FaultHooks hooks;
  hooks.lose_context = true;
  egl_blit_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  std::string trigger;
  bool ok = PresentFrameWithFallback(px.data(), 2, 2, &trigger);
  return ok && !trigger.empty() &&
         (trigger_mentions(trigger, "context") ||
          trigger_mentions(trigger, "current") ||
          trigger_mentions(trigger, "lost"));
}

// Named case: EGL swap failure falls back in-process — returns true, trigger
// names eglSwapBuffers.
static bool check_swap_fail_falls_back_in_process() {
  FaultGuard g;
  egl_blit_test::FaultHooks hooks;
  hooks.fail_next_swap = true;
  egl_blit_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  std::string trigger;
  bool ok = PresentFrameWithFallback(px.data(), 2, 2, &trigger);
  return ok && !trigger.empty() && trigger_mentions(trigger, "swap");
}

// Named case: backgrounded/destroyed surface falls back in-process — returns
// true (never blank, never crash), trigger names the surface failure.
static bool check_destroyed_surface_falls_back_in_process() {
  FaultGuard g;
  egl_blit_test::FaultHooks hooks;
  hooks.no_surface = true;
  egl_blit_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  std::string trigger;
  bool ok = PresentFrameWithFallback(px.data(), 2, 2, &trigger);
  return ok && !trigger.empty() &&
         (trigger_mentions(trigger, "surface") ||
          trigger_mentions(trigger, "window") ||
          trigger_mentions(trigger, "native"));
}

// Named case: fallback_trigger == nullptr is tolerated — still returns true,
// no crash.
static bool check_null_trigger_tolerated() {
  FaultGuard g;
  egl_blit_test::FaultHooks hooks;
  hooks.fail_next_swap = true;
  egl_blit_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  return PresentFrameWithFallback(px.data(), 2, 2, nullptr);
}

// Named case: null pixels never crash and never report a blank push —
// returns true on both the healthy and the fallback lanes.
static bool check_null_pixels_never_blank_no_crash() {
  FaultGuard g;
  std::string t1 = "untouched";
  bool ok_healthy = PresentFrameWithFallback(nullptr, 2, 2, &t1);
  egl_blit_test::FaultHooks hooks;
  hooks.fail_init = true;
  egl_blit_test::SetFaultHooks(&hooks);
  bool ok_fallback = PresentFrameWithFallback(nullptr, 2, 2, nullptr);
  return ok_healthy && ok_fallback;
}

// Named case: zero dimensions never crash — returns true on the fallback
// lane (a frame is always pushed, never a second process).
static bool check_zero_dims_no_crash() {
  FaultGuard g;
  std::vector<uint32_t> px = two_by_two();
  std::string t1, t2;
  bool ok1 = PresentFrameWithFallback(px.data(), 0, 2, &t1);
  bool ok2 = PresentFrameWithFallback(px.data(), 2, 0, &t2);
  return ok1 && ok2;
}

// Named case: repeated fallback frames keep succeeding (no wedged state) and
// each reports its trigger.
static bool check_repeated_fallbacks_stay_live() {
  FaultGuard g;
  egl_blit_test::FaultHooks hooks;
  hooks.fail_next_swap = true;
  egl_blit_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  for (int i = 0; i < 3; ++i) {
    std::string trigger;
    if (!PresentFrameWithFallback(px.data(), 2, 2, &trigger) ||
        trigger.empty())
      return false;
  }
  return true;
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
  RUN_CASE(check_init_fail_falls_back_in_process);
  RUN_CASE(check_context_lost_falls_back_in_process);
  RUN_CASE(check_swap_fail_falls_back_in_process);
  RUN_CASE(check_destroyed_surface_falls_back_in_process);
  RUN_CASE(check_null_trigger_tolerated);
  RUN_CASE(check_null_pixels_never_blank_no_crash);
  RUN_CASE(check_zero_dims_no_crash);
  RUN_CASE(check_repeated_fallbacks_stay_live);
  if (failures > 0) {
    std::printf("present_fallback_test: %d failing\n", failures);
    return 1;
  }
  std::printf("present_fallback_test: all passing\n");
  return 0;
}
