#include "muse/state/query_client.hpp"

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace muse {
namespace state {
namespace {

// Process-wide single-flight table keyed on (client, key): the first fetcher
// for a key becomes the leader and issues the one transport call with no
// client mutex held; concurrent fetchers on the same key wait on the entry
// and then share the leader's result. Entries are reference-counted so the
// leader can drop the table slot as soon as it publishes while late waiters
// still read the result. Never call out to Transport under either mutex.
struct FlightEntry {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  Response response{Value(0), 0};
};

std::mutex& flightMutex() {
  static std::mutex m;
  return m;
}

std::map<std::pair<const void*, std::string>, std::shared_ptr<FlightEntry>>&
flights() {
  static std::map<std::pair<const void*, std::string>,
                  std::shared_ptr<FlightEntry>>
      f;
  return f;
}

}  // namespace

QueryClient::QueryClient(Transport* t) : transport_(t) {}

Future<Value> QueryClient::fetch(QueryKey k, ObserverId /*obs*/) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto cached = cache_.find(k.str());
    if (cached != cache_.end()) {
      return Future<Value>(StatusOr<Value>(cached->second));
    }
  }
  // Single-flight election: at most one leader per (client, key). The
  // client mutex is not held here (nor during the transport call below),
  // so a re-entrant transport cannot deadlock and distinct keys never
  // serialize behind one another.
  const auto flightKey = std::make_pair(static_cast<const void*>(this), k.str());
  std::shared_ptr<FlightEntry> entry;
  bool leader = false;
  {
    std::lock_guard<std::mutex> lock(flightMutex());
    auto it = flights().find(flightKey);
    if (it == flights().end()) {
      // Re-check the cache before leading: a finished leader seeds the
      // cache before erasing its table slot, so a lookup miss may mean the
      // value is already cached (avoids a duplicate transport call).
      std::lock_guard<std::mutex> clock(mutex_);
      auto cached = cache_.find(k.str());
      if (cached != cache_.end()) {
        return Future<Value>(StatusOr<Value>(cached->second));
      }
      entry = std::make_shared<FlightEntry>();
      flights().emplace(flightKey, entry);
      leader = true;
    } else {
      entry = it->second;
    }
  }
  Response res{Value(0), 0};
  if (leader) {
    // Leader: the single transport call for this key — no mutex held.
    res = transport_->get(k, transport_->idempotency());
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cache_.emplace(k.str(), res.value);
    }
    {
      std::lock_guard<std::mutex> lock(entry->mutex);
      entry->response = res;
      entry->done = true;
    }
    entry->cv.notify_all();
    // Drop the table slot; waiters still holding the shared entry read the
    // published result, and later fetchers hit the seeded client cache.
    std::lock_guard<std::mutex> lock(flightMutex());
    flights().erase(flightKey);
  } else {
    // Waiter: block on the entry (no client mutex held) until published.
    std::unique_lock<std::mutex> lock(entry->mutex);
    entry->cv.wait(lock, [&] { return entry->done; });
    res = entry->response;
  }
  return Future<Value>(StatusOr<Value>(res.value));
}

Status QueryClient::invalidateQueries(Tag /*t*/) {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();
  return Status::kOk;
}

Status QueryClient::setQueryData(QueryKey k, Value v) {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.insert_or_assign(k.str(), v);
  return Status::kOk;
}

Status QueryClient::cancelQueries(QueryKey k) {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.erase(k.str());
  return Status::kOk;
}

}  // namespace state
}  // namespace muse
