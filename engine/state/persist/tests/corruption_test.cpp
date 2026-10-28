// Batch 4 contract tests — T9: corruption detection, degrade-to-defaults,
// reliability event (FR-STATE-007, AC-STATE-030).
// Hermetic: C++17 + committed engine/state headers only; TelemetrySink faked
// in-test (recording double); no env/config/network/encryption; never throws.
// Binds ONLY to the T9 `Interface:` line; see unit-tests/T9.md assumptions.
//
// CONVERGENCE ASSUMPTION (A-P1, shared with T8): persist snapshot is
// `muse::state::persist::Snapshot` from `muse/state/snapshot.hpp`.
// `loadChecked`/`RestoreOutcome`/telemetry seam are assumed declared in
// `muse/state/corruption.hpp` (ASSUMPTION A-P4; coder-owned header).
#include <cstdint>
#include <cstdio>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/corruption.hpp"
#include "muse/state/snapshot.hpp"

namespace {

using muse::state::persist::Bytes;
using muse::state::persist::RestoreOutcome;
using muse::state::persist::Snapshot;
using muse::state::persist::TelemetryEvent;
using muse::state::persist::TelemetrySink;
using muse::state::persist::loadChecked;

class RecordingSink : public TelemetrySink {
 public:
  void emit(TelemetryEvent e) override { events.push_back(e); }
  std::vector<TelemetryEvent> events;
};

// Deterministic checksum helper mirroring the assumed persist scheme:
// checksum = FNV-1a-32 over payload (ASSUMPTION A-P5; coder honors it).
static uint32_t fnv(const Bytes& p) {
  uint32_t h = 2166136261u;
  for (uint8_t b : p) {
    h ^= b;
    h *= 16777619u;
  }
  return h;
}

static Snapshot cleanSnapshot() {
  Bytes payload{3, 10, 20, 30};
  return Snapshot{3, fnv(payload), payload};
}

// Named case: clean snapshot loads clean, no event emitted.
static bool check_clean_loads_clean_no_event() {
  RecordingSink sink;
  RestoreOutcome o = loadChecked(cleanSnapshot(), &sink);
  return o.clean && sink.events.empty();
}

// Named case: bit-flipped payload yields defaults + exactly one
// kSnapshotCorrupt event (never throws, never aborts).
static bool check_bitflip_yields_defaults_plus_one_event() {
  RecordingSink sink;
  Snapshot s = cleanSnapshot();
  s.payload[1] ^= 0x01;  // single bit flip, checksum now stale
  RestoreOutcome o = loadChecked(s, &sink);
  if (o.clean) return false;
  if (sink.events.size() != 1) return false;
  return sink.events[0] == TelemetryEvent::kSnapshotCorrupt;
}

// Named case: truncated payload yields defaults + exactly one event.
static bool check_truncated_yields_defaults_plus_one_event() {
  RecordingSink sink;
  Snapshot s = cleanSnapshot();
  s.payload.pop_back();  // length failure, checksum now stale
  RestoreOutcome o = loadChecked(s, &sink);
  if (o.clean) return false;
  if (sink.events.size() != 1) return false;
  return sink.events[0] == TelemetryEvent::kSnapshotCorrupt;
}

// Named case: wrong-length (over-long) payload yields defaults + one event.
static bool check_wrong_length_yields_defaults_plus_one_event() {
  RecordingSink sink;
  Snapshot s = cleanSnapshot();
  s.payload.push_back(0xFF);  // over-long, checksum now stale
  RestoreOutcome o = loadChecked(s, &sink);
  if (o.clean) return false;
  if (sink.events.size() != 1) return false;
  return sink.events[0] == TelemetryEvent::kSnapshotCorrupt;
}

// Named case: corrupt checksum field (payload intact) still detected.
static bool check_bad_checksum_field_detected() {
  RecordingSink sink;
  Snapshot s = cleanSnapshot();
  s.checksum ^= 0xFFFFFFFFu;
  RestoreOutcome o = loadChecked(s, &sink);
  if (o.clean) return false;
  return sink.events.size() == 1 &&
         sink.events[0] == TelemetryEvent::kSnapshotCorrupt;
}

// Named case: null sink never crashes; outcome still reports unclean.
static bool check_null_sink_safe() {
  Snapshot s = cleanSnapshot();
  s.payload[0] ^= 0x80;
  RestoreOutcome o = loadChecked(s, nullptr);
  return !o.clean;
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
  RUN_CASE(check_clean_loads_clean_no_event);
  RUN_CASE(check_bitflip_yields_defaults_plus_one_event);
  RUN_CASE(check_truncated_yields_defaults_plus_one_event);
  RUN_CASE(check_wrong_length_yields_defaults_plus_one_event);
  RUN_CASE(check_bad_checksum_field_detected);
  RUN_CASE(check_null_sink_safe);
  if (failures > 0) {
    std::printf("corruption_test: %d failing\n", failures);
    return 1;
  }
  std::printf("corruption_test: all passing\n");
  return 0;
}
