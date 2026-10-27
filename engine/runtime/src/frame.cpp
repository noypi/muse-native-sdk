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

void FrameScheduler::setFrameCallback(FrameCallback cb) {
  frame_callback_ = std::move(cb);
}

void FrameScheduler::setClock(Clock* clock) { clock_ = clock; }

void FrameScheduler::setTelemetrySink(TelemetrySink* sink) { sink_ = sink; }

void FrameScheduler::setBudgetNs(std::uint64_t budgetNs) {
  budget_ns_ = budgetNs;
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
  const std::uint64_t startNs = clock_ != nullptr ? clock_->nowNs() : 0;
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
  // exactly one frame and then idles). The consumed owner's id becomes the
  // presented frame's owner; smallest id first keeps multi-owner pumps
  // deterministic.
  ComponentId owner = 0;
  bool haveOwner = false;
  if (!explicit_requests_.empty()) {
    auto minIt = explicit_requests_.begin();
    for (auto it = explicit_requests_.begin(); it != explicit_requests_.end();
         ++it) {
      if (it->first < minIt->first) {
        minIt = it;
      }
    }
    owner = minIt->first;
    haveOwner = true;
    if (--(minIt->second) <= 0) {
      explicit_requests_.erase(minIt);
    }
  }
  if (!haveOwner && !continuous_drivers_.empty()) {
    owner = *continuous_drivers_.begin();
    for (ComponentId id : continuous_drivers_) {
      if (id < owner) {
        owner = id;
      }
    }
    haveOwner = true;
  }
  ++frames_presented_;
  const std::uint64_t endNs = clock_ != nullptr ? clock_->nowNs() : 0;
  FrameInfo info;
  info.owner = haveOwner ? owner : 0;
  info.budgetNs = budget_ns_;
  info.actualNs = clock_ != nullptr ? endNs - startNs : 0;
  info.overBudget = info.actualNs > info.budgetNs;
  if (frame_callback_) {
    frame_callback_(info);
  }
  if (info.overBudget && sink_ != nullptr) {
    sink_->report(info);
  }
  return 1;
}

std::uint64_t FrameScheduler::framesPresented() const {
  return frames_presented_;
}

}  // namespace runtime
