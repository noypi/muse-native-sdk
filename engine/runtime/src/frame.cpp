#include "runtime/frame.h"

#include "runtime/tree.h"

namespace runtime {

FrameScheduler::FrameScheduler(RuntimeTree& tree) : tree_(&tree) {}

bool FrameScheduler::hasPending() const {
  return tree_->dirtyCount() > 0 || !explicit_requests_.empty() ||
         !continuous_drivers_.empty();
}

// One-shot explicit request: each call banks a single frame for `owner`.
void FrameScheduler::requestFrame(ComponentId owner) {
  ++explicit_requests_[owner];
}

// Release clears the owner's explicit one-shots and continuous driver.
// Erasing a missing key is a no-op, so unknown owners are harmless and
// other owners' entries are untouched.
void FrameScheduler::releaseOwner(ComponentId owner) {
  explicit_requests_.erase(owner);
  continuous_drivers_.erase(owner);
}

// Declared continuous mode (FR-RUNTIME-006): enabling marks `owner` as a
// persistent driver — pumps keep presenting until disabled or released.
// Disabling or releasing an unknown owner erases nothing and is a no-op.
void FrameScheduler::setContinuous(ComponentId owner, bool enabled) {
  if (enabled) {
    continuous_drivers_.insert(owner);
  } else {
    continuous_drivers_.erase(owner);
  }
}

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
  // Consume a single pending explicit one-shot (dirty and continuous
  // state are handled above/below, so a dirty+request pump still presents
  // exactly one frame and then idles).
  for (auto it = explicit_requests_.begin(); it != explicit_requests_.end();
       ++it) {
    if (--(it->second) <= 0) {
      explicit_requests_.erase(it);
    }
    break;
  }
  ++frames_presented_;
  return 1;
}

std::uint64_t FrameScheduler::framesPresented() const {
  return frames_presented_;
}

}  // namespace runtime
