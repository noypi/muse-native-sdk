#ifndef MOTION_ARBITRATION_H_
#define MOTION_ARBITRATION_H_

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

#endif  // MOTION_ARBITRATION_H_
