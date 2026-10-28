#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "muse/state/atom.hpp"

namespace muse {
namespace state {

// Per-entity conflict-resolution policy (FR-STATE-005, assumption A1).
// LWW-Register is the default; RGA backs ordered collections (chat);
// OR-Set backs tag-like sets. On the single-field FieldState projection
// below all three policies resolve to the same convergent join
// (higher dot/version wins, higher value breaks ties); the policy tag
// records which entity family the merge was invoked for.
enum class CrdtPolicy {
  kLwwRegister,
  kOrSet,
  kRgaSequence,
};

// Single mergeable field: a value plus its dot/version pair.
// Equality-comparable; constructible as FieldState(TypedValue, uint64_t).
class FieldState {
 public:
  FieldState() : value_(), version_(0) {}
  FieldState(TypedValue value, std::uint64_t version)
      : value_(value), version_(version) {}

  TypedValue value() const { return value_; }
  std::uint64_t version() const { return version_; }

  bool operator==(const FieldState& other) const {
    return value_ == other.value_ && version_ == other.version_;
  }
  bool operator!=(const FieldState& other) const {
    return !(*this == other);
  }

 private:
  TypedValue value_;
  std::uint64_t version_;
};

// Convergent merge: commutative, associative, and idempotent for every
// policy. Higher version wins; equal versions resolve to the higher
// value so the join stays deterministic regardless of argument order.
StatusOr<FieldState> merge(CrdtPolicy p, const FieldState& a,
                           const FieldState& b);

// Per-entity policy registry backing assumption A1. Unknown entities
// report Status::kNotFound (loud failure, never silent divergence).
// Null or empty entity names are rejected. Thread-safe via one
// short-held member mutex, mirroring MutationQueue.
class CrdtRegistry {
 public:
  CrdtRegistry() = default;
  CrdtRegistry(const CrdtRegistry&) = delete;
  CrdtRegistry& operator=(const CrdtRegistry&) = delete;

  Status registerPolicy(const char* entity, CrdtPolicy p);
  StatusOr<CrdtPolicy> policyFor(const char* entity) const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, CrdtPolicy> policies_;
};

}  // namespace state
}  // namespace muse
