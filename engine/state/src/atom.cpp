#include "muse/state/atom.hpp"

namespace muse {
namespace state {

AtomId AtomStore::create(TypedValue v) {
  std::lock_guard<std::mutex> lock(mutex_);
  while (atoms_.find(next_id_) != atoms_.end()) {
    ++next_id_;
  }
  const AtomId id = next_id_++;
  atoms_.emplace(id, Entry(v));
  return id;
}

void AtomStore::write(AtomId id, TypedValue v, Provenance p) {
  (void)p;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = atoms_.find(id);
  if (it == atoms_.end()) {
    return;  // Unknown id: ignore, never UB.
  }
  it->second.value = v;
  // Visit only this atom's own dependent observers: O(dependents), not
  // O(atoms). One invalidation folds into the pending coalesced batch.
  pending_visits_ += 1 + it->second.observers.size();
  pending_ = true;
}

StatusOr<TypedValue> AtomStore::read(AtomId id, ObserverId obs) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = atoms_.find(id);
  if (it == atoms_.end()) {
    return Status::kNotFound;
  }
  it->second.observers.insert(obs);
  return it->second.value;
}

Batch AtomStore::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!pending_) {
    return Batch();
  }
  Batch batch(pending_visits_);
  pending_ = false;
  pending_visits_ = 0;
  return batch;
}

}  // namespace state
}  // namespace muse
