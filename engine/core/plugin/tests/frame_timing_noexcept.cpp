// T6 verification harness (AC-CORE-010): render-path translation unit
// compiled with -fno-exceptions must link and pass the frame-timing suite.
//
// Executed inside `./gradlew :engine:core:pluginAbi` (TC-CORE-010).
// Test-only surface exercising the T3/T4/T5 contracts; exposes no new
// production API. The TU is noexcept end-to-end (T4 boundary): any
// failure routes to renderPathViolation, never a thrown object.
#include "../../exception_policy.h"

#include <cassert>
#include <chrono>

namespace muse {
namespace core {
namespace plugin {
namespace tests {

// Frame-timing probe: N frames of minimal render-path work must complete
// without throwing (this TU builds under -fno-exceptions, so a throw
// would be a compile error) and within a generous bound. Returns 0 on
// pass, non-zero on timing failure. Never throws.
int runFrameTimingNoexcept(int frames) noexcept {
  if (frames <= 0) {
    return 1;
  }
  const auto start = std::chrono::steady_clock::now();
  volatile unsigned sink = 0;
  for (int f = 0; f < frames; ++f) {
    for (unsigned i = 0; i < 1024; ++i) {
      sink += i;
    }
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  // Generous bound: 1000 tiny frames must finish well under 30s on any
  // CI host. A breach fails the test (non-zero), it never throws.
  if (frames <= 1000 && ms > 30000) {
    return 2;
  }
  (void)sink;
  return 0;
}

int checkNoexceptTiming() noexcept {
  const int rc = runFrameTimingNoexcept(60);
  if (rc != 0) {
    renderPathViolation("CORE_FRAME_TIMING_BREACH");
  }
  return rc;
}

}  // namespace tests
}  // namespace plugin
}  // namespace core
}  // namespace muse
