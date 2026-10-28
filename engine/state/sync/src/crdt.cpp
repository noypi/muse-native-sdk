#include "muse/state/crdt.hpp"

namespace muse {
namespace state {

StatusOr<FieldState> merge(CrdtPolicy p, const FieldState& a,
                           const FieldState& b) {
  (void)p;
  // Join on the (version, value) pair: higher version wins; ties fall to
  // the higher value. Max is commutative, associative, and idempotent, so
  // every policy converges regardless of merge order. OR-Set and RGA keep
  // the same join on this single-field projection (observed-set union and
  // sequence interleave both reduce to keeping the dominant dot here);
  // the policy tag selects the entity family, not a divergent outcome.
  if (a.version() != b.version()) {
    return a.version() > b.version() ? a : b;
  }
  return a.value().get() >= b.value().get() ? a : b;
}

Status CrdtRegistry::registerPolicy(const char* entity, CrdtPolicy p) {
  if (entity == nullptr || entity[0] == '\0') {
    return Status::kCorrupt;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  policies_[std::string(entity)] = p;
  return Status::kOk;
}

StatusOr<CrdtPolicy> CrdtRegistry::policyFor(const char* entity) const {
  if (entity == nullptr || entity[0] == '\0') {
    return Status::kNotFound;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = policies_.find(std::string(entity));
  if (it == policies_.end()) {
    return Status::kNotFound;
  }
  return it->second;
}

}  // namespace state
}  // namespace muse
