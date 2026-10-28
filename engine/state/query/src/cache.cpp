#include "muse/state/cache.hpp"

#include <mutex>
#include <unordered_map>

namespace muse {
namespace state {
namespace {

// Process-global SWR table: rehydrate seeds it, readStaleWhileRevalidate
// reads it. It is intentionally separate from QueryClient::cache_ —
// rehydrated entries are stale by construction, so they must stay readable
// as stale without ever overwriting (or suppressing refetch of) live
// QueryClient state. Guarded by one short-held mutex; no out-calls under it.
std::mutex& SwrMutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<std::string, Value>& SwrEntries() {
  static std::unordered_map<std::string, Value> entries;
  return entries;
}

}  // namespace

Status rehydrate(Snapshot s, StalePolicy /*p*/) {
  // kStaleWithRefetch: every seeded entry lands fresh-but-stale and is
  // marked for refetch. The refetch itself is invalidation-driven (observed
  // on the next fetch), never issued here, so this stays a synchronous kOk
  // no-op on empty snapshots and can never inject live state.
  std::lock_guard<std::mutex> lock(SwrMutex());
  for (const auto& entry : s) {
    SwrEntries().insert_or_assign(entry.first.str(), entry.second);
  }
  return Status::kOk;
}

Value readStaleWhileRevalidate(QueryKey k) {
  std::lock_guard<std::mutex> lock(SwrMutex());
  auto it = SwrEntries().find(k.str());
  if (it == SwrEntries().end()) {
    return Value();
  }
  return it->second;
}

BackoffCurve backoffCurve(const RetryPolicy& p) { return p.backoff; }

}  // namespace state
}  // namespace muse
