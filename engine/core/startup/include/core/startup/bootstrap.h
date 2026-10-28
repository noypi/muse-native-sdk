// Numbered bootstrap phase list with per-phase timing (NFR-CORE-004 / NFR-CORE-012).
//
// Fixed ordered phases (CORE-012 notes):
//   memory/allocator -> thread pools -> GPU device -> JsRuntime -> UI tree
//   -> first frame.
//
// Each phase records a monotonic start/end tick into PhaseTiming, exposed
// to telemetry via BootstrapResult.timings. Eager-only work runs only
// when BootstrapConfig.allowEager is set. Oversized or out-of-order work
// is a typed denial (StartupError naming the phase), never silent —
// mirroring the muse::core::Arena fixed-store/no-silent-fallback style.
// No partial execution beyond a failed phase: timings past the failed
// phase are left zeroed.
//
// NOTE: shared startup-path types for the T2 dynamic-initializer check
// live in core/startup/dynamic_init.h (T2-owned); this header defines
// only the T1 surface.
#pragma once

#include <cstdint>

namespace muse {
namespace startup {

// Number of bootstrap phases. Kept as a named constant next to the
// fixed-size timings array so telemetry width stays in sync.
inline constexpr int kBootstrapPhaseCount = 6;

// Ordered phase names, index-aligned with BootstrapResult.timings.
inline constexpr const char* kBootstrapPhaseNames[kBootstrapPhaseCount] = {
    "memory/allocator",
    "thread pools",
    "GPU device",
    "JsRuntime",
    "UI tree",
    "first frame",
};

// Bootstrap configuration. Default-constructible; eager-only work is
// disabled unless the caller explicitly opts in.
struct BootstrapConfig {
  bool allowEager = false;
};

// Per-phase monotonic timing ([startNs, endNs] in steady-clock
// nanoseconds). Exposed to telemetry via BootstrapResult.timings.
struct PhaseTiming {
  int phase = -1;
  const char* name = nullptr;
  std::uint64_t startNs = 0;
  std::uint64_t endNs = 0;
};

// Typed bootstrap denial. `code` is nonzero on failure, `phase` names
// the failing phase index, and `message` is always non-empty on failure.
struct StartupError {
  int code = 0;
  int phase = -1;
  const char* message = "";
};

// Bootstrap outcome. On failure ok=false with a named StartupError and
// no executed timing beyond the failed phase.
struct BootstrapResult {
  bool ok = false;
  PhaseTiming timings[kBootstrapPhaseCount];
  StartupError error;
};

// Run the six bootstrap phases in fixed order, recording per-phase
// monotonic timings. Never throws. Deterministic: repeated runs yield
// the identical phase order.
BootstrapResult runBootstrap(const BootstrapConfig& config) noexcept;

}  // namespace startup
}  // namespace muse
