#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "muse/state/atom.hpp"

namespace muse {
namespace state {

// Ordered snapshot of equality-comparable fields (FR-STATE-005, T5).
// Aggregate-shaped wrapper around a field vector; default-, vector-,
// and initializer-list-constructible for test-fixture convenience.
struct SnapshotState {
  std::vector<TypedValue> fields;

  SnapshotState() = default;
  explicit SnapshotState(std::vector<TypedValue> f) : fields(std::move(f)) {}
  SnapshotState(std::initializer_list<TypedValue> init) : fields(init) {}

  bool operator==(const SnapshotState& other) const {
    return fields == other.fields;
  }
  bool operator!=(const SnapshotState& other) const {
    return !(*this == other);
  }
};

// Opaque field-level delta between two snapshots. fieldCount() reports
// the number of transferred (changed) fields; an empty delta is a no-op
// under apply. Never throws; errors surface as Status from diff/apply.
class Delta {
 public:
  Delta() = default;

  std::size_t fieldCount() const { return entries_.size(); }

 private:
  friend StatusOr<Delta> diff(const SnapshotState& base,
                              const SnapshotState& next);
  friend Status apply(SnapshotState* s, const Delta& d);

  std::size_t target_size_ = 0;
  std::vector<std::pair<std::size_t, TypedValue>> entries_;
};

// Field-level diff: records every index whose value differs plus any
// appended tail fields, and the target size so apply reproduces next
// byte-identical. Always succeeds; errors are Status, never exceptions.
StatusOr<Delta> diff(const SnapshotState& base, const SnapshotState& next);

// Applies a diff produced by diff(). Null snapshots are rejected with
// Status::kNotFound and never crash; empty deltas are a no-op kOk.
Status apply(SnapshotState* s, const Delta& d);

}  // namespace state
}  // namespace muse
