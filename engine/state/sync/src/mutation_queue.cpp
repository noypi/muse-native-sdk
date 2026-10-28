#include "muse/state/mutation_queue.hpp"

namespace muse {
namespace state {

MutationQueue::MutationQueue(DurableStore* store) : store_(store) {}

Status MutationQueue::enqueue(Mutation m) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (seen_.count(m.key.value) != 0) {
    return Status::kOk;
  }
  if (store_ != nullptr) {
    Status st = store_->persist(m);
    if (st != Status::kOk) {
      return st;
    }
  }
  seen_.insert(m.key.value);
  queue_.push_back(m);
  return Status::kOk;
}

StatusOr<Mutation> MutationQueue::next() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) {
    return Status::kNotFound;
  }
  return queue_.front();
}

Status MutationQueue::ack(IdempotencyKey k) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = queue_.begin(); it != queue_.end(); ++it) {
    if (it->key == k) {
      queue_.erase(it);
      return Status::kOk;
    }
  }
  return Status::kNotFound;
}

StatusOr<std::vector<Mutation>> MutationQueue::reload() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (store_ == nullptr) {
    return queue_;
  }
  StatusOr<std::vector<Mutation>> loaded = store_->load();
  if (!loaded.ok()) {
    return loaded.status();
  }
  const std::vector<Mutation> pending = queue_;
  queue_.clear();
  for (const auto& m : loaded.value()) {
    bool queued = false;
    for (const auto& q : queue_) {
      if (q.key == m.key) {
        queued = true;
        break;
      }
    }
    if (queued) {
      continue;
    }
    bool was_pending = false;
    for (const auto& q : pending) {
      if (q.key == m.key) {
        was_pending = true;
        break;
      }
    }
    if (was_pending) {
      queue_.push_back(m);
      continue;
    }
    if (seen_.count(m.key.value) != 0) {
      // Key seen before but not pending: it was acked in memory
      // (the seam has no delete path), so replay must not re-queue it.
      continue;
    }
    seen_.insert(m.key.value);
    queue_.push_back(m);
  }
  return queue_;
}

}  // namespace state
}  // namespace muse
