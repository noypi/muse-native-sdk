#include "muse/state/rehydrate.hpp"

#include <mutex>
#include <string>
#include <unordered_set>

namespace muse {
namespace state {
namespace persist {
namespace {

// Deferred stores already flushed to at most one batch each. Guards the
// no-invalidation-storm cap: repeat jobs for the same store are dropped.
std::mutex& batchedMutex() {
  static std::mutex m;
  return m;
}

std::unordered_set<std::string>& batchedStores() {
  static std::unordered_set<std::string> s;
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
    if (!batchedStores().insert(j.store.str()).second) return;
  }
  b->onDeferredBatch(j.store);
}

}  // namespace persist
}  // namespace state
}  // namespace muse
