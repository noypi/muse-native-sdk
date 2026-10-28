#include "muse/state/snapshot.hpp"

namespace muse {
namespace state {
namespace persist {

void registerMigration(MigrationRegistry& registry, std::uint32_t from,
                        std::uint32_t to, MigrateFn fn) {
  registry.add(from, to, std::move(fn));
}

StatusOr<Snapshot> migrate(Snapshot s, const MigrationRegistry& registry) {
  // Bound on chained hops so a cyclic registry (e.g. 1->2->1) fails closed
  // instead of looping forever.
  constexpr std::uint32_t kMaxHops = 64;
  bool migrated = false;
  for (std::uint32_t hops = 0; hops < kMaxHops; ++hops) {
    std::uint32_t to = 0;
    MigrateFn fn;
    if (!registry.find(s.version, &to, &fn)) {
      // No hop from the current version: at the end of the chain (or
      // already current). Preserve the checksum untouched on a pure no-op.
      if (migrated) s.checksum = checksumPayload(s.payload);
      return s;
    }
    StatusOr<Bytes> next = fn(s.payload);
    if (!next.ok()) return next.status();
    s.payload = next.value();
    s.version = to;
    // Guard: a well-formed migration expresses the version bump in the
    // payload schema tag. A migrated payload that still carries the old
    // (or any wrong) tag is malformed — fail closed without writing.
    if (s.payload.empty() ||
        static_cast<std::uint32_t>(s.payload[0]) != to) {
      return Status::kNotFound;
    }
    migrated = true;
  }
  // Hop bound exceeded: treat as an incomplete/cyclic chain.
  return Status::kNotFound;
}

LifecycleHook restoreHook() {
  return LifecycleHook([](const Snapshot&) {});
}

std::uint32_t checksumPayload(const Bytes& payload) {
  // FNV-1a 32-bit over the payload bytes.
  std::uint32_t hash = 2166136261u;
  for (std::uint8_t b : payload) {
    hash ^= static_cast<std::uint32_t>(b);
    hash *= 16777619u;
  }
  return hash;
}

}  // namespace persist
}  // namespace state
}  // namespace muse
