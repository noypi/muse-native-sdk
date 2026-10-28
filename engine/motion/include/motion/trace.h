#ifndef MOTION_TRACE_H_
#define MOTION_TRACE_H_

// Per-frame trace emitter for profiler correlation
// (FR-MOTION-009 / AC-MOTION-028).
//
// Every evaluated frame emits one TraceFrame row carrying the caller's
// frameId + nanos join keys plus a deterministic hash of the full spring
// state. The profiler joins N emitted rows 1:1 against its timeline on the
// exact (frameId, nanos) keys.
//
// Error channel: none — the function is infallible and pure. Every input,
// including boundary ids/nanos and extreme/NaN states, yields a row and
// nothing throws.

#include <cstdint>

#include "motion/spring.h"

struct TraceFrame {
  uint64_t frameId = 0;
  int64_t nanos = 0;
  uint64_t stateHash = 0;
};

// Emits one trace row echoing `frameId` + `nanos` exactly with a
// deterministic hash of the full state (x, v, stiffness, damping).
// Pure: no global state, same inputs always yield the same row.
// Never throws.
TraceFrame motion_emitFrame(uint64_t frameId, int64_t nanos,
                            const SpringState& s) noexcept;

#endif  // MOTION_TRACE_H_
