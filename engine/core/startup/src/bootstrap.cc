// T1 bootstrap phase runner (NFR-CORE-004 / NFR-CORE-012).
//
// Runs the six fixed phases strictly in kBootstrapPhaseNames order,
// stamping a monotonic [startNs, endNs] window per phase from
// steady_clock. Ticks are clamped forward (each start >= previous end,
// each end >= its start) so telemetry always observes a non-decreasing
// per-phase window even on a coarse clock. Eager-only work executes
// solely behind config.allowEager; it never reorders or renames phases.
// Any phase failure returns ok=false with a StartupError naming the
// phase, leaving later timings zeroed (no partial execution beyond the
// failed phase). The runner itself performs no heap allocation, no I/O,
// and no thread/handle creation — it only records the ordered phase
// windows the pool/GPU/runtime owners act on.

#include "core/startup/bootstrap.h"

#include <chrono>
#include <cstdint>

namespace muse {
namespace startup {
namespace {

// Monotonic tick source: steady-clock nanoseconds. Wrapped so the
// clamping policy below has a single call site.
std::uint64_t nowNs() noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// Eager-only pre-touch. Runs exclusively when the caller sets
// allowEager; deterministic and order-preserving by construction.
void eagerWork(int phase) noexcept {
  volatile unsigned sink = static_cast<unsigned>(phase);
  for (unsigned i = 0; i < 64; ++i) {
    sink += i;
  }
  (void)sink;
}

}  // namespace

BootstrapResult runBootstrap(const BootstrapConfig& config) noexcept {
  BootstrapResult result;
  result.ok = false;
  result.error = StartupError{};

  std::uint64_t prevEnd = 0;
  // Fixed order is enforced structurally: the loop visits each phase
  // index in kBootstrapPhaseNames order exactly once.
  for (int i = 0; i < kBootstrapPhaseCount; ++i) {
    // Diagnostic-only failure injection (see BootstrapConfig::failAtPhase).
    // Stops execution at the injected phase with a named StartupError;
    // timings at and beyond it stay zeroed (no partial execution beyond
    // the failed phase). No allocation; noexcept-safe.
    if (i == config.failAtPhase) {
      result.error = StartupError{1, i, kBootstrapPhaseNames[i]};
      return result;
    }

    std::uint64_t start = nowNs();
    if (start < prevEnd) {
      start = prevEnd;
    }

    // Eager-only work stays behind the explicit gate.
    if (config.allowEager) {
      eagerWork(i);
    }

    std::uint64_t end = nowNs();
    if (end < start) {
      end = start;
    }

    result.timings[i].phase = i;
    result.timings[i].name = kBootstrapPhaseNames[i];
    result.timings[i].startNs = start;
    result.timings[i].endNs = end;
    prevEnd = end;
  }

  result.ok = true;
  result.error = StartupError{0, -1, ""};
  return result;
}

}  // namespace startup
}  // namespace muse
