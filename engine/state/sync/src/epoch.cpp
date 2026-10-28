#include "muse/state/epoch.hpp"

namespace muse {
namespace state {

StatusOr<Epoch> EpochStore::current(const char* account) const {
  if (account == nullptr || account[0] == '\0') {
    return Status::kNotFound;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = epochs_.find(std::string(account));
  if (it == epochs_.end()) {
    return Status::kNotFound;
  }
  return it->second;
}

Status EpochStore::advance(const char* account, Epoch e) {
  if (account == nullptr || account[0] == '\0') {
    return Status::kNotFound;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = epochs_.find(std::string(account));
  if (it != epochs_.end() && !e.supersedes(it->second)) {
    // Stale (or same-generation / cross-account-tag) write: deny without
    // touching the stored epoch.
    return Status::kNotFound;
  }
  epochs_[std::string(account)] = e;
  return Status::kOk;
}

}  // namespace state
}  // namespace muse
