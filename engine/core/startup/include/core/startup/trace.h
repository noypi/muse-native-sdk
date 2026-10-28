// Cold-start trace attribution (NFR-CORE-004 / NFR-CORE-012, AC-CORE-033).
//
// Aggregates the six T1 bootstrap phase timings (memory/allocator ->
// thread pools -> GPU device -> JsRuntime -> UI tree -> first frame) plus
// the total first-frame time into a single StartupTrace. Every phase is
// attributed: phases[i] carries the T1 PhaseTiming (index, name,
// monotonic [startNs, endNs]) in kBootstrapPhaseNames order, and
// firstFrameMs carries the end-to-end cold-start total consumed by the
// perf cold-start budget check (NFR-PERFORMANCE-009, 50-80 ms envelope
// reference). There is no unattributed startup work: no phase slot is
// left empty and no thread/handle creation precedes its declared phase
// (see creationAllowedForPhase in core/startup/pool_warmup.h).
//
// The trace is observability-only over already-gated phases: collecting
// it performs no heap allocation, no I/O, and no thread/handle creation,
// and it never throws.
#pragma once

#include <cstdint>

#include "core/startup/bootstrap.h"

namespace muse {
namespace startup {

// Cold-start trace: per-phase timings from runBootstrap plus the total
// first-frame time in milliseconds. phases has exactly
// kBootstrapPhaseCount entries in fixed bootstrap order; firstFrameMs is
// the end-to-end total (last phase end minus first phase start,
// truncated to whole milliseconds).
struct StartupTrace {
  PhaseTiming phases[kBootstrapPhaseCount];
  std::uint64_t firstFrameMs = 0;
};

// Collect a cold-start trace: runs the six bootstrap phases via
// runBootstrap with default config, attributes every phase timing in
// order, and derives the firstFrameMs total. Never throws. Repeated
// calls attribute the same six phases in the same order (determinism,
// AC-CORE-005 carryover).
StartupTrace collectStartupTrace() noexcept;

}  // namespace startup
}  // namespace muse
