#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "muse/state/atom.hpp"

namespace muse {
namespace state {
namespace persist {

// Raw payload bytes carried by a versioned snapshot.
using Bytes = std::vector<std::uint8_t>;

// Payload transform for one schema hop (from -> to). The version bump is
// expressed in the returned bytes: payload[0] must carry the new schema tag.
using MigrateFn = std::function<StatusOr<Bytes>(const Bytes&)>;

// Versioned snapshot envelope (FR-STATE-007). checksum is FNV-1a-32 over
// payload (see checksumPayload); version is the schema version of payload.
struct Snapshot {
  std::uint32_t version;
  std::uint32_t checksum;
  Bytes payload;
};

// FR-LIFECYCLE-002/LIFE-SAVE seam: plain callback invoked with the restored
// snapshot. Tests substitute a fake lambda; restoreHook() is the default.
using LifecycleHook = std::function<void(const Snapshot&)>;

// Ordered hop table for forward schema migration. Keyed by `from` version;
// each hop records its `to` version and transform.
class MigrationRegistry {
 public:
  MigrationRegistry() = default;

  void add(std::uint32_t from, std::uint32_t to, MigrateFn fn) {
    hops_[from] = std::make_pair(to, std::move(fn));
  }

  bool find(std::uint32_t from, std::uint32_t* to, MigrateFn* fn) const {
    auto it = hops_.find(from);
    if (it == hops_.end()) return false;
    if (to != nullptr) *to = it->second.first;
    if (fn != nullptr) *fn = it->second.second;
    return true;
  }

 private:
  std::map<std::uint32_t, std::pair<std::uint32_t, MigrateFn>> hops_;
};

void registerMigration(MigrationRegistry& registry, std::uint32_t from,
                        std::uint32_t to, MigrateFn fn);

// Migrates s forward through every registered hop starting at s.version.
// Already-current snapshots (no hop from s.version) are returned untouched.
// A missing hop, a failing MigrateFn, or a migrated payload whose schema tag
// does not match the new version fails closed with a non-ok Status; the
// caller's snapshot is never modified in place (s is taken by value).
StatusOr<Snapshot> migrate(Snapshot s, const MigrationRegistry& registry);

// Default restore lifecycle hook (no-op plain callback).
LifecycleHook restoreHook();

// FNV-1a-32 over payload bytes; used for Snapshot.checksum.
std::uint32_t checksumPayload(const Bytes& payload);

}  // namespace persist
}  // namespace state
}  // namespace muse
