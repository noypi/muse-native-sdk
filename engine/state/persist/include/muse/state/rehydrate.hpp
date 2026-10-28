#pragma once

#include <string>

namespace muse {
namespace state {
namespace persist {

// String-like store identifier.
class StoreId {
 public:
  StoreId() = default;
  StoreId(const char* s) : id_(s != nullptr ? s : "") {}  // NOLINTNEXTLINE
  explicit StoreId(std::string s) : id_(std::move(s)) {}

  const std::string& str() const { return id_; }

  bool operator==(const StoreId& other) const { return id_ == other.id_; }
  bool operator!=(const StoreId& other) const { return !(*this == other); }

 private:
  std::string id_;
};

// Rehydrate work item for one store.
struct RehydrateJob {
  StoreId store = StoreId();
};

// Scheduling seam for off-critical-path rehydration (FR-STATE-007).
// Harness fakes implement the two callbacks to record ordering + batches.
class FrameBudget {
 public:
  virtual ~FrameBudget() = default;
  virtual void onFirstPaint(StoreId) = 0;
  virtual void onDeferredBatch(StoreId) = 0;
};

// True when the store is flagged for first paint; all other stores are
// deferred past the first-frame budget.
bool needsForFirstPaint(StoreId s);

// Schedules one rehydrate job. First-paint stores run in the first-paint
// phase; deferred stores coalesce to at most one batch each (no
// invalidation storm). Synchronous in the harness fake; a real async
// scheduler must preserve the same observable order + batch caps.
void scheduleRehydrate(RehydrateJob j, FrameBudget* b);

}  // namespace persist
}  // namespace state
}  // namespace muse
