#pragma once

#include <cstdint>

namespace runtime {

class RuntimeTree;

// Invalidation-driven frame loop (RULE-RUNTIME-002): work is driven by
// dirty nodes; an idle app presents no frames. No continuous-driver API
// (FR-RUNTIME-006 out of scope).
class FrameScheduler {
 public:
  explicit FrameScheduler(RuntimeTree& tree);

  // True when at least one invalidation is pending.
  bool hasPending() const;

  // Applies pending invalidations, lays out dirty subtrees, presents.
  // Returns presented frame count (0 or 1); a no-op returning 0 when
  // nothing is dirty (no layout/present work).
  int pumpFrame();

  std::uint64_t framesPresented() const;

 private:
  RuntimeTree* tree_;
  std::uint64_t frames_presented_ = 0;
};

}  // namespace runtime
