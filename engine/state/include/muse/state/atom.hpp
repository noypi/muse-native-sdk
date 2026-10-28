#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace muse {
namespace state {

// Stable integer id assigned once at create; never reassigned on write.
using AtomId = std::uint32_t;
using ObserverId = std::uint32_t;

// Error shape for fallible atom access. Unknown AtomId surfaces as
// Status::kNotFound; behavior is never UB.
enum class Status {
  kOk = 0,
  kNotFound = 1,
};

// Origin of a write. Only the JS origin exists at this layer; later batches
// add further origins which funnel through the same AtomStore::write path.
enum class Provenance {
  kJs,
};

// Minimal value carried by an atom (T1 structural subset). Constructible from
// int and equality-comparable for test-fixture convenience.
class TypedValue {
 public:
  TypedValue() : value_(0) {}
  explicit TypedValue(int value) : value_(value) {}

  int get() const { return value_; }

  bool operator==(const TypedValue& other) const {
    return value_ == other.value_;
  }
  bool operator!=(const TypedValue& other) const {
    return !(*this == other);
  }

 private:
  int value_;
};

// Fallible read result: carries either a value (ok() == true) or a Status
// explaining the failure (e.g. Status::kNotFound for an unknown AtomId).
template <typename T>
class StatusOr {
 public:
  StatusOr(const T& value) : status_(Status::kOk), value_(value), has_value_(true) {}
  StatusOr(T&& value)  // NOLINTNEXTLINE(google-explicit-constructor)
      : status_(Status::kOk), value_(static_cast<T&&>(value)), has_value_(true) {}
  StatusOr(Status status)  // NOLINTNEXTLINE(google-explicit-constructor)
      : status_(status), value_(), has_value_(false) {}

  bool ok() const { return has_value_; }
  const T& value() const { return value_; }
  Status status() const { return status_; }

 private:
  Status status_;
  T value_;
  bool has_value_;
};

// Single coalesced invalidation batch produced by AtomStore::flush.
// size() is the batch count (1 when a batch is present, 0 when drained);
// nodeVisits() reports how many atom/observer visits the writes since the
// last flush performed, so the O(depth) bound stays observable.
class Batch {
 public:
  Batch() : coalesced_(false), node_visits_(0) {}
  explicit Batch(std::size_t node_visits)
      : coalesced_(true), node_visits_(node_visits) {}

  std::size_t size() const { return coalesced_ ? 1 : 0; }
  bool empty() const { return !coalesced_; }
  std::size_t nodeVisits() const { return node_visits_; }

 private:
  bool coalesced_;
  std::size_t node_visits_;
};

// Thread-safe atom container (T1 foundation for Store and later batches).
// Dependency tracking happens on read; each write updates the value and folds
// one invalidation into the pending coalesced batch; flush returns that
// single batch and drains. Synchronization uses one internal member mutex
// held only for short map/flag updates — never a global lock, and never held
// across observer notification (there are no out-calls under the lock).
class AtomStore {
 public:
  AtomStore() = default;
  AtomStore(const AtomStore&) = delete;
  AtomStore& operator=(const AtomStore&) = delete;

  AtomId create(TypedValue v);
  void write(AtomId id, TypedValue v, Provenance p);
  StatusOr<TypedValue> read(AtomId id, ObserverId obs) const;
  Batch flush();

 private:
  struct Entry {
    explicit Entry(const TypedValue& v) : value(v) {}
    TypedValue value;
    std::unordered_set<ObserverId> observers;
  };

  mutable std::mutex mutex_;
  mutable std::unordered_map<AtomId, Entry> atoms_;
  mutable AtomId next_id_ = 0;
  mutable bool pending_ = false;
  mutable std::size_t pending_visits_ = 0;
};

}  // namespace state
}  // namespace muse
