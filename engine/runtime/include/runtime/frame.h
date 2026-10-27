#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace runtime {

class RuntimeTree;

// Ownership id for explicit frame requests (FR-RUNTIME-006).
using ComponentId = std::uint32_t;

// Per-frame timing + ownership report (FR-RUNTIME-006, AC-RUNTIME-016).
struct FrameInfo {
  ComponentId owner = 0;
  std::uint64_t budgetNs = 0;
  std::uint64_t actualNs = 0;
  bool overBudget = false;
};

// Frame callback: invoked once per presented frame, synchronously in
// pumpFrame, i.e. on the calling (JS) thread.
using FrameCallback = std::function<void(FrameInfo)>;

// Monotonic clock seam for hermetic frame-timing tests. Null (unset) means
// real timing is unavailable: actualNs reports 0 and never crashes.
struct Clock {
  virtual ~Clock() = default;
  virtual std::uint64_t nowNs() const = 0;
};

// Over-budget telemetry sink seam. Null (unset) means reports are dropped.
struct TelemetrySink {
  virtual ~TelemetrySink() = default;
  virtual void report(const FrameInfo& info) = 0;
};

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
  // When a frame is presented, invokes the frame callback (if set)
  // synchronously on the calling thread and reports over-budget frames
  // (actualNs > budgetNs) to the telemetry sink (if set).
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

  // Frame callback + telemetry seams (FR-RUNTIME-006, AC-RUNTIME-016).
  // All setters accept nullptr (clears the seam) and never throw.
  // Default budget is 8ms (8,000,000ns) until setBudgetNs overrides it.
  void setFrameCallback(FrameCallback cb);
  void setClock(Clock* clock);
  void setTelemetrySink(TelemetrySink* sink);
  void setBudgetNs(std::uint64_t budgetNs);

 private:
  RuntimeTree* tree_;
  std::uint64_t frames_presented_ = 0;
  std::unordered_map<ComponentId, int> explicit_requests_;
  std::unordered_set<ComponentId> continuous_drivers_;
  FrameCallback frame_callback_;
  Clock* clock_ = nullptr;
  TelemetrySink* sink_ = nullptr;
  std::uint64_t budget_ns_ = 8000000ULL;
};

}  // namespace runtime
