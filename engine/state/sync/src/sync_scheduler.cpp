#include "muse/state/sync_scheduler.hpp"

namespace muse {
namespace state {

Status FakeScheduler::schedule(SyncRequest r) {
  // No live process involved: record the request for the matrix fake and
  // report acceptance. Every constraint (kAny/kUnmetered/kIdle, with or
  // without Doze exemption) is accepted; deferral policy lives in the
  // services matrix, out of scope here.
  std::lock_guard<std::mutex> lock(mutex_);
  last_ = r;
  has_request_ = true;
  return Status::kOk;
}

StatusOr<SyncRequest> FakeScheduler::last() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!has_request_) {
    return Status::kNotFound;
  }
  return last_;
}

}  // namespace state
}  // namespace muse
