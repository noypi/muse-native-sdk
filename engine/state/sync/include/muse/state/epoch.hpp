#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "muse/state/atom.hpp"

namespace muse {
namespace state {

// Multi-account/device epoch (FR-STATE-005, AC-STATE-024). Aggregate
// { account, generation }: the account tag identifies the owning account,
// generation is the monotonically increasing fence. Queued/cached state is
// namespaced by (account name, generation) so a stale-epoch mutation can
// never overwrite newer state, even when its payload is newer.
struct Epoch {
  std::uint64_t account = 0;
  std::uint64_t generation = 0;

  // True only for a newer generation of the same account. Equal epochs
  // never supersede, and cross-account epochs never supersede.
  bool supersedes(const Epoch& o) const {
    return account == o.account && generation > o.generation;
  }
};

// Per-account epoch fence. Unknown accounts report Status::kNotFound;
// stale (!supersedes) advances are rejected with Status::kNotFound and
// never applied, so the stored epoch stays bit-identical. Callers are
// expected to advance with an Epoch whose account tag matches the tag
// already stored for that account name; a mismatched (cross-account) tag
// never supersedes and is rejected the same way as a stale generation.
// Null or empty account names are rejected, never crash. Thread-safe via one short-held
// member mutex, mirroring MutationQueue/CrdtRegistry.
class EpochStore {
 public:
  EpochStore() = default;
  EpochStore(const EpochStore&) = delete;
  EpochStore& operator=(const EpochStore&) = delete;

  StatusOr<Epoch> current(const char* account) const;
  Status advance(const char* account, Epoch e);

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Epoch> epochs_;
};

}  // namespace state
}  // namespace muse
