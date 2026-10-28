#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "muse/state/atom.hpp"

#if defined(MUSE_STATE_DEVTOOLS) && MUSE_STATE_DEVTOOLS

namespace muse {
namespace state {

// Deep-equal point-in-time view of atom values. Backed by the latest
// after-value of every in-scope delta; atoms with no in-scope delta are
// reported as Status::kNotFound (never UB, never a default value).
class Snapshot {
 public:
  Snapshot() = default;

  StatusOr<TypedValue> get(AtomId id) const {
    auto it = values_.find(id);
    if (it == values_.end()) return Status::kNotFound;
    return it->second;
  }

  bool operator==(const Snapshot& other) const { return values_ == other.values_; }
  bool operator!=(const Snapshot& other) const { return !(*this == other); }

 private:
  friend class DevTimeline;
  std::unordered_map<AtomId, TypedValue> values_;
};

// Causal chain of recorded invalidations: the distinct atom set touched by
// the timeline, in first-touch order.
class Dag {
 public:
  Dag() = default;

  bool empty() const { return order_.empty(); }
  std::size_t nodeCount() const { return order_.size(); }
  bool contains(AtomId id) const { return nodes_.count(id) != 0; }

 private:
  friend class DevTimeline;
  std::vector<AtomId> order_;
  std::unordered_set<AtomId> nodes_;
};

// Dev-only time-travel ring buffer of atom deltas with structural sharing.
// record() appends; rewind(n)/forward(n) move a cursor over the delta log;
// present() replays the in-scope prefix. Out-of-range navigation returns a
// non-kOk Status and leaves present() bit-identical; nothing here throws.
// Bounded: at most kMaxEntries_ deltas are retained; when a record lands on
// a full buffer the oldest entry is evicted (overwrite-oldest), so long dev
// sessions grow memory O(cap), not O(records).
class DevTimeline {
 public:
  DevTimeline() = default;

  void record(AtomId id, TypedValue before, TypedValue after);
  Status rewind(std::size_t n);
  Status forward(std::size_t n);
  StatusOr<Snapshot> present() const;
  StatusOr<Dag> exportDag() const;

 private:
  struct Delta {
    AtomId id;
    TypedValue before;
    TypedValue after;
  };
  // Fixed capacity: large enough that T6-scale navigation never evicts,
  // small enough that a runaway dev session cannot grow without bound.
  static constexpr std::size_t kMaxEntries_ = 4096;
  std::vector<Delta> entries_;
  std::size_t cursor_ = 0;
};

}  // namespace state
}  // namespace muse

#endif  // defined(MUSE_STATE_DEVTOOLS) && MUSE_STATE_DEVTOOLS
