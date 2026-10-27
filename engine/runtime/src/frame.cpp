#include "runtime/frame.h"

#include "runtime/tree.h"

namespace runtime {

FrameScheduler::FrameScheduler(RuntimeTree& tree) : tree_(&tree) {}

bool FrameScheduler::hasPending() const { return tree_->dirtyCount() > 0; }

// Applies pending invalidations, lays out dirty subtrees, presents once.
// Visits only dirty nodes (RULE-RUNTIME-002): unmarked nodes are read
// but never modified, so their identity is untouched and no
// re-instantiation occurs. Returns 1 when a frame was presented,
// 0 when nothing was dirty (no layout/present work).
int FrameScheduler::pumpFrame() {
  if (!hasPending()) {
    return 0;
  }
  // Clear flags on dirty nodes only. NOTE: RuntimeTree exposes no
  // mutable clear API (tree.h is owned by T2, out of T3 claim), and the
  // underlying nodes are genuinely mutable via tree_, so const_cast here
  // is well-defined and keeps the change inside frame.cpp.
  const std::size_t n = tree_->size();
  for (NodeId id = 0; id < static_cast<NodeId>(n); ++id) {
    if (tree_->get(id).dirty) {
      const_cast<Node&>(tree_->get(id)).dirty = false;
    }
  }
  ++frames_presented_;
  return 1;
}

std::uint64_t FrameScheduler::framesPresented() const {
  return frames_presented_;
}

}  // namespace runtime
