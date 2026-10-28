#pragma once

#include <cstdint>
#include <mutex>

#include "muse/state/atom.hpp"

namespace muse {
namespace state {

// Stable idempotency key for a mutation (AC-STATE-013). Derived
// deterministically from the Action so equal redispatches carry the same key.
struct IdempotencyKey {
  std::uint64_t value = 0;

  bool operator==(const IdempotencyKey& other) const {
    return value == other.value;
  }
  bool operator!=(const IdempotencyKey& other) const {
    return !(*this == other);
  }
};

// Inverse of one optimistic mutation: the exact prior value to restore.
struct InversePatch {
  TypedValue prior;
};

// Unit of durable work (FR-STATE-005 seam payload).
struct Mutation {
  InversePatch inverse;
  IdempotencyKey key;
};

// Action applied optimistically by Store::dispatch. Default-constructible
// fixture; Action::failing() builds the failure fixture used to exercise the
// AC-STATE-012 rollback path.
struct Action {
  Action() : tag_(0), fail_(false) {}

  static Action failing() {
    Action a;
    a.fail_ = true;
    return a;
  }

  bool isFailing() const { return fail_; }
  int tag() const { return tag_; }

  bool operator==(const Action& other) const {
    return tag_ == other.tag_ && fail_ == other.fail_;
  }

 private:
  int tag_;
  bool fail_;
};

// Deep-equal observable state of a Store (ASSUMPTION A5).
struct Snapshot {
  TypedValue value;

  bool operator==(const Snapshot& other) const { return value == other.value; }
  bool operator!=(const Snapshot& other) const { return !(*this == other); }
};

// Durable-write seam (FR-STATE-005). Implemented for real in a later batch;
// tests substitute an in-memory fake. Nullable in Store.
class DurableQueue {
 public:
  virtual ~DurableQueue() = default;
  virtual Status enqueue(Mutation m) = 0;
};

// Optimistic store built on T1's AtomStore (no second substrate).
// dispatch applies optimistically then enqueues once; rollback restores the
// deep-equal prior value. Batch observation routes through the underlying
// AtomStore::flush so optimistic + restore writes coalesce into one batch.
class Store {
 public:
  Store();
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  Status dispatch(Action a);
  Status rollback(const Mutation& m);
  void setDurableQueue(DurableQueue* q);

  Snapshot snapshot() const;
  Batch flushBatch();

 private:
  IdempotencyKey keyFor(const Action& a) const;

  AtomStore atoms_;
  AtomId slot_;
  mutable std::mutex mutex_;
  DurableQueue* queue_ = nullptr;
};

}  // namespace state
}  // namespace muse
