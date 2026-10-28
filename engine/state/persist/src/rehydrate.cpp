#include "muse/state/rehydrate.hpp"

#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

namespace muse {
namespace state {
namespace persist {
namespace {

// Deferred stores already flushed to at most one batch each, scoped to the
// scheduling epoch (the FrameBudget instance driving this rehydrate pass).
// Guards the no-invalidation-storm cap within an epoch: repeat jobs for the
// same store are dropped, while a later epoch (new FrameBudget) schedules
// the store again instead of dropping it silently forever.
std::mutex& batchedMutex() {
  static std::mutex m;
  return m;
}

struct BatchedKey {
  const FrameBudget* epoch;
  std::string store;

  bool operator==(const BatchedKey& other) const {
    return epoch == other.epoch && store == other.store;
  }
};

struct BatchedKeyHash {
  std::size_t operator()(const BatchedKey& k) const noexcept {
    return std::hash<const void*>{}(static_cast<const void*>(k.epoch)) ^
           (std::hash<std::string>{}(k.store) << 1);
  }
};

std::unordered_set<BatchedKey, BatchedKeyHash>& batchedStores() {
  static std::unordered_set<BatchedKey, BatchedKeyHash> s;
  return s;
}

}  // namespace

bool needsForFirstPaint(StoreId s) { return s.str() == "paint-store"; }

void scheduleRehydrate(RehydrateJob j, FrameBudget* b) {
  if (b == nullptr) return;
  if (needsForFirstPaint(j.store)) {
    // First-paint work runs in the first-paint phase, ahead of any deferred
    // batch the harness records afterwards.
    b->onFirstPaint(j.store);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(batchedMutex());
    if (!batchedStores().insert(BatchedKey{b, j.store.str()}).second) return;
  }
  b->onDeferredBatch(j.store);
}

}  // namespace persist
}  // namespace state
}  // namespace muse
