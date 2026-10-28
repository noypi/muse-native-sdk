#include "muse/state/delta.hpp"

namespace muse {
namespace state {

StatusOr<Delta> diff(const SnapshotState& base, const SnapshotState& next) {
  Delta d;
  d.target_size_ = next.fields.size();
  const std::size_t common =
      base.fields.size() < next.fields.size() ? base.fields.size()
                                              : next.fields.size();
  for (std::size_t i = 0; i < common; ++i) {
    if (base.fields[i] != next.fields[i]) {
      d.entries_.emplace_back(i, next.fields[i]);
    }
  }
  // Appended tail fields are all transferred.
  for (std::size_t i = common; i < next.fields.size(); ++i) {
    d.entries_.emplace_back(i, next.fields[i]);
  }
  // Shrinking to a smaller target needs no per-field entries: the resize
  // in apply drops the tail implicitly.
  return d;
}

Status apply(SnapshotState* s, const Delta& d) {
  if (s == nullptr) {
    return Status::kNotFound;
  }
  s->fields.resize(d.target_size_);
  for (const auto& entry : d.entries_) {
    if (entry.first >= s->fields.size()) {
      return Status::kCorrupt;
    }
    s->fields[entry.first] = entry.second;
  }
  return Status::kOk;
}

}  // namespace state
}  // namespace muse
