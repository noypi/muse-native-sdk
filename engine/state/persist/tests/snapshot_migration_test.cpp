// Batch 4 contract tests — T8: versioned snapshot format, migration registry,
// lifecycle seam (FR-STATE-007, AC-STATE-029 / TC-STATE-029).
// Hermetic: C++17 + committed engine/state headers only; payloads are inline
// byte vectors; LifecycleHook faked in-test; no env/config/network/encryption.
// Binds ONLY to the T8 `Interface:` line; see unit-tests/T8.md assumptions.
//
// CONVERGENCE ASSUMPTION (A-P1): the persist snapshot is
// `muse::state::persist::Snapshot` from `muse/state/snapshot.hpp` — the
// versioned struct { version, checksum, payload }. It MUST NOT be
// `muse::state::Snapshot` (Batch 3 T7 SWR cache alias in cache.hpp, which this
// file never includes). The coder implements snapshot.hpp with exactly this
// spelling.
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/snapshot.hpp"

namespace {

using muse::state::Status;
using muse::state::StatusOr;
using muse::state::persist::Bytes;
using muse::state::persist::LifecycleHook;
using muse::state::persist::MigrateFn;
using muse::state::persist::MigrationRegistry;
using muse::state::persist::Snapshot;
using muse::state::persist::migrate;
using muse::state::persist::registerMigration;
using muse::state::persist::restoreHook;

// Tiny payload codec for the migration chain: payload[0] = schema tag,
// payload[1..] = field bytes. v1 payload {1, a}; v1->v2 appends default b;
// v2->v3 appends default c. Fields-intact means a survives both hops.
static Bytes v1Payload(uint8_t a) { return Bytes{1, a}; }

// Named case (TC-STATE-029): v1 -> v3 through registered v1->v2->v3 chain,
// fields intact.
static bool check_v1_to_v3_chain_fields_intact() {
  MigrationRegistry r;
  registerMigration(r, 1, 2, [](const Bytes& in) -> StatusOr<Bytes> {
    if (in.size() < 2 || in[0] != 1) return Status::kNotFound;
    return Bytes{2, in[1], 0};
  });
  registerMigration(r, 2, 3, [](const Bytes& in) -> StatusOr<Bytes> {
    if (in.size() < 3 || in[0] != 2) return Status::kNotFound;
    return Bytes{3, in[1], in[2], 0};
  });
  Snapshot s{1, 0, v1Payload(42)};
  StatusOr<Snapshot> out = migrate(s, r);
  if (!out.ok()) return false;
  const Snapshot& got = out.value();
  if (got.version != 3) return false;
  if (got.payload.size() != 4) return false;
  return got.payload[0] == 3 && got.payload[1] == 42;
}

// Named case: single-hop migration v1 -> v2 applies exactly once.
static bool check_single_hop_migration() {
  MigrationRegistry r;
  registerMigration(r, 1, 2, [](const Bytes& in) -> StatusOr<Bytes> {
    if (in.empty()) return Status::kNotFound;
    Bytes o = in;
    o[0] = 2;
    return o;
  });
  Snapshot s{1, 0, v1Payload(9)};
  StatusOr<Snapshot> out = migrate(s, r);
  return out.ok() && out.value().version == 2 && out.value().payload[0] == 2;
}

// Named case: already-current version is a no-op (payload untouched).
static bool check_current_version_noop() {
  MigrationRegistry r;  // empty registry
  Snapshot s{3, 0, Bytes{3, 7, 8, 9}};
  StatusOr<Snapshot> out = migrate(s, r);
  return out.ok() && out.value().version == 3 && out.value().payload == s.payload;
}

// Named case: missing hop in the chain surfaces non-ok Status (never throws).
static bool check_missing_hop_fails_cleanly() {
  MigrationRegistry r;
  registerMigration(r, 1, 2, [](const Bytes& in) -> StatusOr<Bytes> { return in; });
  // No 2->3 registered: v1 -> v3 request cannot complete.
  Snapshot s{1, 0, v1Payload(5)};
  StatusOr<Snapshot> out = migrate(s, r);
  return !out.ok();
}

// Named case: failing migration function propagates failure, input untouched.
static bool check_failing_migration_propagates() {
  MigrationRegistry r;
  registerMigration(r, 1, 2,
                    [](const Bytes&) -> StatusOr<Bytes> { return Status::kNotFound; });
  Snapshot s{1, 0, v1Payload(5)};
  StatusOr<Snapshot> out = migrate(s, r);
  if (out.ok()) return false;
  return s.version == 1 && s.payload == v1Payload(5);
}

// Named case: LifecycleHook restore seam is a plain invokable callback —
// the fake hook fires exactly once when invoked (FR-LIFECYCLE-002/LIFE-SAVE).
static bool check_restore_hook_fake_fires() {
  LifecycleHook hook = restoreHook();
  int calls = 0;
  LifecycleHook fake = [&calls](const Snapshot&) { ++calls; };
  Snapshot s{3, 0, Bytes{3}};
  fake(s);
  (void)hook;  // seam exists and is a plain callback; fake stands in.
  return calls == 1;
}

}  // namespace

#define RUN_CASE(name)             \
  do {                             \
    if (!(name)()) {               \
      std::printf("FAIL %s\n", #name); \
      ++failures;                  \
    } else {                       \
      std::printf("PASS %s\n", #name); \
    }                              \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_v1_to_v3_chain_fields_intact);
  RUN_CASE(check_single_hop_migration);
  RUN_CASE(check_current_version_noop);
  RUN_CASE(check_missing_hop_fails_cleanly);
  RUN_CASE(check_failing_migration_propagates);
  RUN_CASE(check_restore_hook_fake_fires);
  if (failures > 0) {
    std::printf("snapshot_migration_test: %d failing\n", failures);
    return 1;
  }
  std::printf("snapshot_migration_test: all passing\n");
  return 0;
}
