#include "runtime/frame.h"

#include "runtime/tree.h"

namespace runtime {

FrameScheduler::FrameScheduler(RuntimeTree& tree) : tree_(&tree) {}

bool FrameScheduler::hasPending() const { return tree_->dirtyCount() > 0; }

// T1 stub: real invalidation/present pass lands in T3; idle behavior
// (return 0, no work) in T4.
int FrameScheduler::pumpFrame() { return 0; }

std::uint64_t FrameScheduler::framesPresented() const {
  return frames_presented_;
}

}  // namespace runtime
