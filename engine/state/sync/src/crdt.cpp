#include "muse/state/crdt.hpp"

namespace muse {
namespace state {

StatusOr<FieldState> merge(CrdtPolicy p, const FieldState& a,
                           const FieldState& b) {
  // A1 mapping, resolved per policy on the single-field FieldState
  // projection. Higher version (dot) always wins; the policy tag selects
  // the tie-break for concurrent (equal-version) dots:
  //   LWW-Register: higher value wins (last-writer-wins by value order).
  //   RGA-Sequence: lower value wins (earlier sequence position wins the
  //     tie; the full sequence interleave needs element identities the
  //     single-field projection does not carry, so ties reduce to this
  //     deterministic order instead of an interleave).
  //   OR-Set: higher value wins (concurrent adds union to the dominant
  //     element — add-wins; remove handling needs per-element tombstones
  //     the single-field projection does not carry, so removals are not
  //     representable here).
  // Each branch is a max under a total order, hence commutative,
  // associative, and idempotent, so every policy converges regardless of
  // merge order. Projection limit: RGA interleave and OR-Set remove
  // semantics need multi-element state; on this projection only the
  // tie-break differs (RGA vs LWW/OR-Set).
  if (a.version() != b.version()) {
    return a.version() > b.version() ? a : b;
  }
  switch (p) {
    case CrdtPolicy::kRgaSequence:
      return a.value().get() <= b.value().get() ? a : b;
    case CrdtPolicy::kOrSet:
    case CrdtPolicy::kLwwRegister:
    default:
      return a.value().get() >= b.value().get() ? a : b;
  }
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
