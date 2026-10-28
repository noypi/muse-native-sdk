#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/store.hpp"

namespace muse {
namespace state {

// Durable-write seam for the offline-first mutation queue (FR-STATE-005).
// The real SQLite WAL backend lives outside this run (SPEC-NETWORKING-DATA
// NET-DB); tests substitute an in-memory fake with identical FIFO/dedup
// semantics. Nullable wherever a MutationQueue holds it.
class DurableStore {
 public:
  virtual ~DurableStore() = default;
  virtual Status persist(Mutation m) = 0;
  virtual StatusOr<std::vector<Mutation>> load() = 0;
};

// FIFO durable mutation queue with idempotency-key exactly-once replay.
// Duplicate enqueue of a seen key returns kOk without duplicating; empty
// next() and unknown ack() return kNotFound. Errors are Status, never
// exceptions. Thread-safe via one short-held member mutex, mirroring Store.
class MutationQueue {
 public:
  explicit MutationQueue(DurableStore* store);
  MutationQueue(const MutationQueue&) = delete;
  MutationQueue& operator=(const MutationQueue&) = delete;

  Status enqueue(Mutation m);
  StatusOr<Mutation> next();
  Status ack(IdempotencyKey k);
  StatusOr<std::vector<Mutation>> reload();

 private:
  DurableStore* store_;
  mutable std::mutex mutex_;
  std::vector<Mutation> queue_;
  std::unordered_set<std::uint64_t> seen_;
};

}  // namespace state
}  // namespace muse
