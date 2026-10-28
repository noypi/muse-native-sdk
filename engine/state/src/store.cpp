#include "muse/state/store.hpp"

namespace muse {
namespace state {

Store::Store() : slot_(atoms_.create(TypedValue(0))) {}

void Store::setDurableQueue(DurableQueue* q) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_ = q;
}

IdempotencyKey Store::keyFor(const Action& a) const {
  // Stable in the Action's value: equal Actions map to equal keys, and the
  // failing fixture maps to a distinct key. No clock or randomness involved.
  IdempotencyKey key;
  const std::uint64_t tag_bits =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(a.tag()));
  key.value = tag_bits ^ (a.isFailing() ? 0x9E3779B97F4A7C15ULL : 0ULL);
  return key;
}

Snapshot Store::snapshot() const {
  auto got = atoms_.read(slot_, 0);
  Snapshot s;
  if (got.ok()) {
    s.value = got.value();
  }
  return s;
}

Batch Store::flushBatch() { return atoms_.flush(); }

Status Store::dispatch(Action a) {
  auto got = atoms_.read(slot_, 0);
  TypedValue prior = got.ok() ? got.value() : TypedValue(0);

  // Optimistic apply: advance the single state slot by one.
  TypedValue next(prior.get() + 1);
  atoms_.write(slot_, next, Provenance::kJs);

  if (a.isFailing()) {
    // Failure path (AC-STATE-012): restore the exact prior value; the two
    // writes coalesce into a single batch. Never enqueues a failed mutation.
    atoms_.write(slot_, prior, Provenance::kJs);
    return Status::kNotFound;
  }

  Mutation m;
  m.inverse.prior = prior;
  m.key = keyFor(a);

  DurableQueue* q = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    q = queue_;
  }
  if (q != nullptr) {
    Status st = q->enqueue(m);
    if (st != Status::kOk) {
      // Enqueue-failure path (mirrors the failing-action path above):
      // restore the exact prior value; the two writes coalesce into a
      // single batch. Never leaks optimistic state on durable failure.
      atoms_.write(slot_, prior, Provenance::kJs);
      return st;
    }
  }
  return Status::kOk;
}

Status Store::rollback(const Mutation& m) {
  atoms_.write(slot_, m.inverse.prior, Provenance::kJs);
  return Status::kOk;
}

}  // namespace state
}  // namespace muse
