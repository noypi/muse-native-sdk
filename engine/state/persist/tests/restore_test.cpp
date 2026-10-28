// Batch 4 contract tests — T11: process-death restore of stores/nav/scroll/
// focus with migration and bit-flip rejection (FR-STATE-007, AC-STATE-032).
// Hermetic: C++17 + committed engine/state headers only; nav/scroll/focus are
// opaque fixtures, FieldDecryptor + TelemetrySink faked in-test;
// no env/config/network/encryption. Binds ONLY to the T11 `Interface:` line;
// see unit-tests/T11.md assumptions.
//
// CONVERGENCE ASSUMPTION (A-P1, shared with T8): input snapshot is
// `muse::state::persist::Snapshot`. ASSUMPTION A-P8: `Status::kCorrupt` does
// not exist in committed atom.hpp (only kOk/kNotFound/kNonIdempotent) — the
// T11 coder adds it additively; these tests use it.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/corruption.hpp"
#include "muse/state/restore.hpp"
#include "muse/state/snapshot.hpp"

namespace {

using muse::state::Status;
using muse::state::StatusOr;
using muse::state::persist::AppState;
using muse::state::persist::Bytes;
using muse::state::persist::FieldDecryptor;
using muse::state::persist::MigrationRegistry;
using muse::state::persist::Snapshot;
using muse::state::persist::TelemetryEvent;
using muse::state::persist::TelemetrySink;
using muse::state::persist::registerMigration;
using muse::state::persist::restoreAfterDeath;

static uint32_t fnv(const Bytes& p) {
  uint32_t h = 2166136261u;
  for (uint8_t b : p) {
    h ^= b;
    h *= 16777619u;
  }
  return h;
}

class RecordingSink : public TelemetrySink {
 public:
  void emit(TelemetryEvent e) override { events.push_back(e); }
  std::vector<TelemetryEvent> events;
};

// Nullable decryptor fake: identity transform, records calls.
class FakeDecryptor : public FieldDecryptor {
 public:
  Bytes decrypt(const Bytes& in) override {
    ++calls;
    return in;
  }
  int calls = 0;
};

static MigrationRegistry chain12() {
  MigrationRegistry r;
  registerMigration(r, 1, 2, [](const Bytes& in) -> StatusOr<Bytes> {
    if (in.empty()) return Status::kNotFound;
    Bytes o = in;
    o[0] = 2;
    return o;
  });
  return r;
}

// Named case: clean current-version snapshot restores all four slots —
// stores/nav/scroll/focus round-trip through opaque fixtures.
static bool check_clean_restore_populates_all_slots() {
  Bytes payload{2, 11, 22, 33};
  Snapshot s{2, fnv(payload), payload};
  MigrationRegistry r;  // already current, no migrations needed
  RecordingSink sink;
  FakeDecryptor d;
  StatusOr<AppState> out = restoreAfterDeath(s, r, &sink, &d);
  if (!out.ok()) return false;
  const AppState& a = out.value();
  // Opaque slots: each must be present / non-empty after a clean restore.
  return !a.stores.empty() && !a.nav.empty() && !a.scroll.empty() &&
         a.focusValid;
}

// Named case: old-version snapshot migrates during restore (v1 -> v2).
static bool check_old_version_migrates_on_restore() {
  Bytes payload{1, 77};
  Snapshot s{1, fnv(payload), payload};
  MigrationRegistry r = chain12();
  RecordingSink sink;
  FakeDecryptor d;
  StatusOr<AppState> out = restoreAfterDeath(s, r, &sink, &d);
  if (!out.ok()) return false;
  return !out.value().stores.empty();
}

// Named case: bit-flip rejects with Status::kCorrupt + clean-default AppState
// + exactly one kSnapshotCorrupt event (ASSUMPTION A-P8: kCorrupt is coder-added).
static bool check_bitflip_rejects_with_corrupt_status() {
  Bytes payload{2, 11, 22, 33};
  Snapshot s{2, fnv(payload), payload};
  s.payload[2] ^= 0x04;  // bit flip, checksum stale
  MigrationRegistry r;
  RecordingSink sink;
  FakeDecryptor d;
  StatusOr<AppState> out = restoreAfterDeath(s, r, &sink, &d);
  if (out.ok()) return false;
  if (out.status() != Status::kCorrupt) return false;
  if (sink.events.size() != 1) return false;
  return sink.events[0] == TelemetryEvent::kSnapshotCorrupt;
}

// Named case: null decryptor is allowed (opaque slots restore without it).
static bool check_null_decryptor_allowed() {
  Bytes payload{2, 5, 6};
  Snapshot s{2, fnv(payload), payload};
  MigrationRegistry r;
  RecordingSink sink;
  StatusOr<AppState> out = restoreAfterDeath(s, r, &sink, nullptr);
  return out.ok() && !out.value().stores.empty();
}

// Named case: null sink is safe on the corrupt path (no crash, kCorrupt).
static bool check_corrupt_null_sink_safe() {
  Bytes payload{2, 5, 6};
  Snapshot s{2, fnv(payload), payload};
  s.checksum ^= 1u;
  MigrationRegistry r;
  StatusOr<AppState> out = restoreAfterDeath(s, r, nullptr, nullptr);
  return !out.ok() && out.status() == Status::kCorrupt;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name);  \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_clean_restore_populates_all_slots);
  RUN_CASE(check_old_version_migrates_on_restore);
  RUN_CASE(check_bitflip_rejects_with_corrupt_status);
  RUN_CASE(check_null_decryptor_allowed);
  RUN_CASE(check_corrupt_null_sink_safe);
  if (failures > 0) {
    std::printf("restore_test: %d failing\n", failures);
    return 1;
  }
  std::printf("restore_test: all passing\n");
  return 0;
}
