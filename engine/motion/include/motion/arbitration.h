#ifndef MOTION_ARBITRATION_H_
#define MOTION_ARBITRATION_H_

#include <cstdint>
#include <vector>

#include "motion/graph.h"

namespace motion {

// One animation claiming ownership of a single property.
struct Claim {
  NodeId id;
  int priority = 0;
  float startMs = 0.0f;
};

// Arbitration result: the winning claim's id, or `none == true` when no
// claims were offered (empty input).
struct Winner {
  NodeId id;
  bool none = false;
};

// Deterministic arbitration for concurrent animations on one property
// (FR-MOTION-008 / AC-MOTION-020).
//
// Documented rule: LAST-WRITE-WINS on tie of (priority desc, startMs desc,
// id asc). That is: the highest priority wins; on equal priority the latest
// startMs wins; on equal priority and startMs the smallest id wins. The
// comparison is a strict total order over the input set, so the winner is
// independent of input order and repeated runs with the same input return
// the same winner.
Winner arbitrate(std::vector<Claim> claims) noexcept;

}  // namespace motion

// Deterministic overlap-arbitration rule for one property
// (FR-MOTION-001 / AC-MOTION-002; FR-MOTION-008 / AC-MOTION-020 consumed
// contract; answers Q-MOTION-004 for this selection).
//
// Pure total order: higher priority wins; equal priority resolves to recency
// (newer/larger start frame wins). The observable result names only which
// rule decided, so unequal priority yields kHighestPriority and equal
// priority yields kNewest regardless of incumbent/challenger roles. Exact
// ties (equal priority and equal start frame) are deterministic but the
// chosen enumerant is unspecified; callers must not depend on it beyond
// repeat-determinism. Header-only; covered FR-MOTION-008 behavior
// (motion::arbitrate) is unchanged.
enum class ArbitrationWinner { kNewest, kHighestPriority };

inline ArbitrationWinner motion_arbitrate(uint64_t incumbentStartFrame,
                                          uint64_t challengerStartFrame,
                                          int incumbentPriority,
                                          int challengerPriority) noexcept {
  (void)incumbentStartFrame;
  (void)challengerStartFrame;
  if (incumbentPriority != challengerPriority) {
    return ArbitrationWinner::kHighestPriority;
  }
  return ArbitrationWinner::kNewest;
}

#endif  // MOTION_ARBITRATION_H_
