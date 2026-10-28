#include "muse/state/query_client.hpp"

namespace muse {
namespace state {

QueryClient::QueryClient(Transport* t) : transport_(t) {}

Future<Value> QueryClient::fetch(QueryKey k, ObserverId /*obs*/) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto cached = cache_.find(k.str());
  if (cached != cache_.end()) {
    return Future<Value>(StatusOr<Value>(cached->second));
  }
  // Coalescing: the mutex is held across the single transport call, so the
  // N-1 concurrent fetches on this key serialize behind it and then hit the
  // seeded cache entry instead of issuing their own calls.
  Response res = transport_->get(k, transport_->idempotency());
  cache_.emplace(k.str(), res.value);
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
