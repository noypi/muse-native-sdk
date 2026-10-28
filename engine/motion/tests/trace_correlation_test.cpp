// Batch 3 contract tests — T5 (FR-MOTION-009 / AC-MOTION-028, TC-MOTION-028).
// Hermetic: C++17 + motion headers only; fixed literal inputs, no wall-clock
// reads, no env/config, no filesystem, no network, no randomness. Follows the
// engine/motion convention (plain main(), RUN_CASE macro, printf PASS/FAIL,
// nonzero exit on failure; cf. tests/virtual_clock_test.cpp).
//
// Binds to the plan item's `Interface:` line (reproduced verbatim in
// unit-tests/T5.md):
//   struct TraceFrame { uint64_t frameId; int64_t nanos; uint64_t stateHash; };
//   TraceFrame motion_emitFrame(uint64_t frameId, int64_t nanos,
//                               const SpringState& s);
//   — infallible pure function, no error shape.
//
// ASSUMPTION (hash provenance): T6 (motion_stateHash) is a LATER item in this
// batch and T5 depends only on T3, so these tests assert T5's hash
// self-consistency (same state -> same hash; distinct states -> distinct
// hashes; hash stable across repeated calls) WITHOUT including
// motion/determinism.h. Cross-module hash equality (trace hash == replay
// hash) is a coder-wiring property for the `tester` layer (UNVERIFIED here).
// ASSUMPTION (join shape): "frameId joins 1:1 with a synthetic profiler
// timeline" is asserted at the emit boundary — N emitted frames carry the
// exact N caller-supplied ids/nanos in order with no drops or reordering.
// End-to-end profiler joining belongs to the `tester` layer.
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "motion/spring.h"
#include "motion/trace.h"

namespace {

// The emitted row echoes the caller's frameId and nanos exactly (the join
// keys the profiler consumes; AC-MOTION-028 schema: frameId + monotonic
// nanos + state hash join key).
bool check_emit_echoes_frame_id_and_nanos() {
  try {
    SpringState s{1.0, 0.5, 180.0, 12.0};
    TraceFrame f = motion_emitFrame(42u, 16666666LL, s);
    return f.frameId == 42u && f.nanos == 16666666LL;
  } catch (...) {
    return false;  // infallible: never throws
  }
}

// Same inputs always produce the same row: the function is pure and
// deterministic (AC-MOTION-028: the hash must match the replayed state, so
// it cannot vary call to call).
bool check_repeated_calls_are_identical() {
  try {
    SpringState s{3.25, -1.75, 200.0, 10.0};
    TraceFrame a = motion_emitFrame(7u, 8333333LL, s);
    TraceFrame b = motion_emitFrame(7u, 8333333LL, s);
    TraceFrame c = motion_emitFrame(7u, 8333333LL, s);
    return a.frameId == b.frameId && b.frameId == c.frameId &&
           a.nanos == b.nanos && b.nanos == c.nanos &&
           a.stateHash == b.stateHash && b.stateHash == c.stateHash;
  } catch (...) {
    return false;
  }
}

// Same state under different frame ids/nanos keeps the same stateHash: the
// hash is a function of state, not of the frame key.
bool check_same_state_yields_same_hash_across_frames() {
  try {
    SpringState s{10.0, 2.0, 150.0, 8.0};
    TraceFrame a = motion_emitFrame(0u, 0LL, s);
    TraceFrame b = motion_emitFrame(999u, 123456789LL, s);
    return a.stateHash == b.stateHash;
  } catch (...) {
    return false;
  }
}

// Clearly different positions hash differently: the hash must discriminate
// states or it cannot serve as a replay join key (AC-MOTION-028 acceptance:
// "stateHash matches the replayed state").
bool check_different_positions_yield_different_hashes() {
  try {
    SpringState a{0.0, 0.0, 180.0, 12.0};
    SpringState b{100.0, 0.0, 180.0, 12.0};
    return motion_emitFrame(1u, 100LL, a).stateHash !=
           motion_emitFrame(1u, 100LL, b).stateHash;
  } catch (...) {
    return false;
  }
}

// The hash covers the full state: varying v, stiffness, or damping alone
// changes the hash (otherwise two diverging replays could emit identical
// trace rows and the 1:1 correlation check would be vacuous).
bool check_hash_covers_full_state() {
  try {
    SpringState base{5.0, 1.0, 180.0, 12.0};
    const uint64_t h0 = motion_emitFrame(1u, 100LL, base).stateHash;
    SpringState dv = base;
    dv.v = 2.0;
    SpringState dk = base;
    dk.stiffness = 190.0;
    SpringState dd = base;
    dd.damping = 13.0;
    return motion_emitFrame(1u, 100LL, dv).stateHash != h0 &&
           motion_emitFrame(1u, 100LL, dk).stateHash != h0 &&
           motion_emitFrame(1u, 100LL, dd).stateHash != h0;
  } catch (...) {
    return false;
  }
}

// N evaluated frames emit N rows whose frameIds join 1:1 with a synthetic
// profiler timeline: exact ids in order, exact caller nanos, no drops, no
// reordering (AC-MOTION-028 core acceptance at the emit boundary).
bool check_frame_sequence_joins_one_to_one() {
  try {
    const int kFrames = 60;
    const int64_t kDt = 16666666LL;
    std::vector<TraceFrame> rows;
    SpringState s{0.0, 0.0, 180.0, 12.0};
    for (int i = 0; i < kFrames; ++i) {
      s = motion_retarget(s, 100.0, s.v, 1.0 / 60.0).state;
      rows.push_back(
          motion_emitFrame(static_cast<uint64_t>(i), i * kDt, s));
    }
    if (rows.size() != static_cast<size_t>(kFrames)) return false;
    for (int i = 0; i < kFrames; ++i) {
      if (rows[static_cast<size_t>(i)].frameId != static_cast<uint64_t>(i))
        return false;
      if (rows[static_cast<size_t>(i)].nanos != i * kDt) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

// Boundary keys pass through infallibly: frameId 0 and UINT64_MAX, nanos 0,
// negative, INT64_MAX/MIN. No error shape exists, so every input must
// produce a row (infallible pure function).
bool check_boundary_ids_and_nanos_pass_through() {
  try {
    SpringState s{1.0, 1.0, 180.0, 12.0};
    TraceFrame z = motion_emitFrame(0u, 0LL, s);
    if (z.frameId != 0u || z.nanos != 0LL) return false;
    TraceFrame mx = motion_emitFrame(UINT64_MAX, INT64_MAX, s);
    if (mx.frameId != UINT64_MAX || mx.nanos != INT64_MAX) return false;
    TraceFrame mn = motion_emitFrame(UINT64_MAX - 1u, INT64_MIN, s);
    if (mn.frameId != UINT64_MAX - 1u || mn.nanos != INT64_MIN) return false;
    TraceFrame neg = motion_emitFrame(3u, -1LL, s);
    if (neg.frameId != 3u || neg.nanos != -1LL) return false;
    return true;
  } catch (...) {
    return false;
  }
}

// Extreme-but-finite states (huge magnitudes, infinities) still emit a row
// without throwing: the function is infallible over the whole input range.
bool check_extreme_states_emit_without_throwing() {
  try {
    SpringState huge{1e15, -1e15, 1e6, 1e6};
    motion_emitFrame(1u, 100LL, huge);
    SpringState inf{std::numeric_limits<double>::infinity(), 0.0, 180.0,
                    12.0};
    motion_emitFrame(2u, 200LL, inf);
    SpringState tiny{std::numeric_limits<double>::min(),
                     -std::numeric_limits<double>::min(), 180.0,
                     12.0};
    motion_emitFrame(3u, 300LL, tiny);
  } catch (...) {
    return false;
  }
  return true;
}

// NaN state components must not trap either: infallible means infallible,
// and a NaN must still hash deterministically (same bits -> same row).
bool check_nan_state_emit_is_deterministic_and_safe() {
  try {
    const double qnan = std::numeric_limits<double>::quiet_NaN();
    SpringState n{qnan, 0.0, 180.0, 12.0};
    TraceFrame a = motion_emitFrame(1u, 100LL, n);
    TraceFrame b = motion_emitFrame(1u, 100LL, n);
    return a.stateHash == b.stateHash && a.frameId == 1u && a.nanos == 100LL;
  } catch (...) {
    return false;
  }
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_emit_echoes_frame_id_and_nanos);
  RUN_CASE(check_repeated_calls_are_identical);
  RUN_CASE(check_same_state_yields_same_hash_across_frames);
  RUN_CASE(check_different_positions_yield_different_hashes);
  RUN_CASE(check_hash_covers_full_state);
  RUN_CASE(check_frame_sequence_joins_one_to_one);
  RUN_CASE(check_boundary_ids_and_nanos_pass_through);
  RUN_CASE(check_extreme_states_emit_without_throwing);
  RUN_CASE(check_nan_state_emit_is_deterministic_and_safe);
  if (failures > 0) {
    std::printf("trace_correlation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("trace_correlation_test: all passing\n");
  return 0;
}
