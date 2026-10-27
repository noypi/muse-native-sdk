#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace runtime {

class RuntimeTree;

// Ownership id for explicit frame requests (FR-RUNTIME-006).
using ComponentId = std::uint32_t;

// Invalidation-driven frame loop (RULE-RUNTIME-002): work is driven by
// dirty nodes; an idle app presents no frames. Explicit one-shot requests
// (FR-RUNTIME-006) compose with dirty state: each request schedules exactly
// one frame, consumed by the next pump with no reschedule.
// Declared continuous drivers (FR-RUNTIME-006) reschedule every pump until
// disabled or released; release restores idle behavior.
class FrameScheduler {
 public:
  explicit FrameScheduler(RuntimeTree& tree);

  // True when at least one invalidation, explicit request, or continuous
  // driver is pending.
  bool hasPending() const;

  // Applies pending invalidations, lays out dirty subtrees, presents.
  // Returns presented frame count (0 or 1); a no-op returning 0 when
  // nothing is dirty, requested, or continuously driven (no
  // layout/present work). Consumes dirty flags plus a single pending
  // explicit one-shot; continuous drivers persist across pumps.
  int pumpFrame();

  std::uint64_t framesPresented() const;

  // Schedules exactly one frame for `owner`; returns void. Each call adds
  // one pending one-shot consumed by a later pumpFrame (no reschedule).
  void requestFrame(ComponentId owner);

  // Clears `owner`'s pending explicit requests and continuous driver, if
  // any. Unknown owners are a no-op; other owners are never disturbed.
  void releaseOwner(ComponentId owner);

  // Declared continuous mode (FR-RUNTIME-006): `enabled=true` marks `owner`
  // as continuously driven and every pump reschedules (presents one frame)
  // until `enabled=false` or `releaseOwner(owner)` clears it. Drivers are
  // per-owner; disabling or releasing an unknown owner is a no-op.

  void setContinuous(ComponentId owner, bool enabled);

 private:
  RuntimeTree* tree_;
  std::uint64_t frames_presented_ = 0;
  std::unordered_map<ComponentId, int> explicit_requests_;
  std::unordered_set<ComponentId> continuous_drivers_;
};

}  // namespace runtime
