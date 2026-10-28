// T6 cold-start trace collection (NFR-CORE-004 / NFR-CORE-012, AC-CORE-033).
//
// collectStartupTrace runs the six T1 bootstrap phases with default
// config, copies each attributed PhaseTiming into the trace in fixed
// order, and derives firstFrameMs as the end-to-end span (last phase end
// minus first phase start, truncated to whole milliseconds). Ticks are
// steady-clock nanoseconds, so the span is monotonic by construction;
// the subtraction saturates at zero rather than wrapping if a clock
// ever reports an inverted window. No allocation, no I/O, no
// thread/handle creation, no exceptions.

#include "core/startup/trace.h"

#include "core/startup/bootstrap.h"

namespace muse {
namespace startup {

StartupTrace collectStartupTrace() noexcept {
  StartupTrace trace;
  const BootstrapResult result = runBootstrap(BootstrapConfig{});
  for (int i = 0; i < kBootstrapPhaseCount; ++i) {
    trace.phases[i] = result.timings[i];
  }

  // End-to-end total: last phase end minus first phase start. The T1
  // runner clamps windows forward, so end >= start on every phase and
  // this span covers exactly the six attributed phases — no
  // unattributed startup work is folded in or left out.
  const std::uint64_t startNs = result.timings[0].startNs;
  const std::uint64_t endNs =
      result.timings[kBootstrapPhaseCount - 1].endNs;
  const std::uint64_t spanNs = (endNs >= startNs) ? (endNs - startNs) : 0;
  trace.firstFrameMs = spanNs / 1000000ULL;
  return trace;
}

}  // namespace startup
}  // namespace muse
