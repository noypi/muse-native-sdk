// Batch 3 (STATE-DEVTOOLS, FR-STATE-008) contract tests — T6.
// Spec refs: FR-STATE-008, AC-STATE-035 (TC-STATE-035).
// Hermetic: C++17 + committed engine/state headers only; in-process
// DevTimeline only; no env/config/network/threads.
// Binds ONLY to the T6 `Interface:` line; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T6.md):
//   A-DT1: namespace muse::state; header "muse/state/devtools.hpp".
//   A-DT2: compiled with MUSE_STATE_DEVTOOLS=1 in this test (dev build).
//   A-DT3: Snapshot exposes StatusOr<TypedValue> get(AtomId) const and
//          operator== (deep-equal); Dag exposes bool empty() const,
//          std::size_t nodeCount() const, bool contains(AtomId) const.
//   A-DT4: rewind/forward out-of-range returns non-kOk Status, never throws,
//          and leaves present() unchanged.
#include <cstdio>

#include "muse/state/atom.hpp"
#include "muse/state/devtools.hpp"

namespace {

using muse::state::AtomId;
using muse::state::Dag;
using muse::state::DevTimeline;
using muse::state::Snapshot;
using muse::state::Status;
using muse::state::TypedValue;

static bool snapshot_value(const Snapshot& s, AtomId id, int* out) {
  auto r = s.get(id);
  if (!r.ok()) return false;
  *out = r.value().get();
  return true;
}

// Record two atoms, present() reflects latest after-values.
static bool check_record_then_present_reflects_latest() {
  DevTimeline tl;
  tl.record(1, TypedValue(10), TypedValue(11));
  tl.record(2, TypedValue(20), TypedValue(22));
  auto p = tl.present();
  if (!p.ok()) return false;
  int v = -1;
  if (!snapshot_value(p.value(), 1, &v) || v != 11) return false;
  if (!snapshot_value(p.value(), 2, &v) || v != 22) return false;
  return true;
}

// Rewind N restores deep-equal prior state (AC-STATE-035 core).
static bool check_rewind_n_restores_prior_state() {
  DevTimeline tl;
  tl.record(1, TypedValue(0), TypedValue(1));
  tl.record(1, TypedValue(1), TypedValue(2));
  tl.record(1, TypedValue(2), TypedValue(3));
  auto before = tl.present();
  if (!before.ok()) return false;
  if (tl.rewind(2) != Status::kOk) return false;
  auto p = tl.present();
  if (!p.ok()) return false;
  int v = -1;
  if (!snapshot_value(p.value(), 1, &v) || v != 1) return false;
  // Prior snapshot must differ from the latest (deep-equal check).
  if (p.value() == before.value()) return false;
  return true;
}

// Forward N re-applies after rewind.
static bool check_forward_reapplies_after_rewind() {
  DevTimeline tl;
  tl.record(5, TypedValue(0), TypedValue(1));
  tl.record(5, TypedValue(1), TypedValue(2));
  if (tl.rewind(1) != Status::kOk) return false;
  if (tl.forward(1) != Status::kOk) return false;
  auto p = tl.present();
  if (!p.ok()) return false;
  int v = -1;
  return snapshot_value(p.value(), 5, &v) && v == 2;
}

// Rewind past the beginning is rejected and leaves state unchanged.
static bool check_rewind_past_beginning_rejected_state_unchanged() {
  DevTimeline tl;
  tl.record(1, TypedValue(0), TypedValue(1));
  auto before = tl.present();
  if (!before.ok()) return false;
  if (tl.rewind(5) == Status::kOk) return false;
  auto after = tl.present();
  if (!after.ok()) return false;
  return after.value() == before.value();
}

// Forward past the end is rejected and leaves state unchanged.
static bool check_forward_past_end_rejected_state_unchanged() {
  DevTimeline tl;
  tl.record(1, TypedValue(0), TypedValue(1));
  auto before = tl.present();
  if (!before.ok()) return false;
  if (tl.forward(1) == Status::kOk) return false;
  auto after = tl.present();
  if (!after.ok()) return false;
  return after.value() == before.value();
}

// Empty timeline: rewind/forward rejected, present still well-formed.
static bool check_empty_timeline_rewind_forward_rejected() {
  DevTimeline tl;
  if (tl.rewind(1) == Status::kOk) return false;
  if (tl.forward(1) == Status::kOk) return false;
  auto p = tl.present();
  if (!p.ok()) return false;
  return true;
}

// DAG export lists the causal chain per invalidation.
static bool check_export_dag_shows_causal_chain() {
  DevTimeline tl;
  tl.record(1, TypedValue(0), TypedValue(1));
  tl.record(2, TypedValue(0), TypedValue(2));
  tl.record(1, TypedValue(1), TypedValue(3));
  auto d = tl.exportDag();
  if (!d.ok()) return false;
  const Dag& dag = d.value();
  if (dag.empty()) return false;
  if (dag.nodeCount() < 2) return false;
  if (!dag.contains(1)) return false;
  if (!dag.contains(2)) return false;
  return true;
}

// Rewind is scoped: rewinding 1 entry restores only the latest delta,
// earlier atoms keep their latest values.
static bool check_partial_rewind_restores_only_latest_delta() {
  DevTimeline tl;
  tl.record(1, TypedValue(0), TypedValue(10));
  tl.record(2, TypedValue(0), TypedValue(20));
  if (tl.rewind(1) != Status::kOk) return false;
  auto p = tl.present();
  if (!p.ok()) return false;
  int v = -1;
  if (!snapshot_value(p.value(), 1, &v) || v != 10) return false;
  // Atom 2's delta was rewound away: either absent or back to before-value.
  auto r2 = p.value().get(2);
  if (r2.ok() && r2.value().get() == 20) return false;
  return true;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_record_then_present_reflects_latest);
  RUN_CASE(check_rewind_n_restores_prior_state);
  RUN_CASE(check_forward_reapplies_after_rewind);
  RUN_CASE(check_rewind_past_beginning_rejected_state_unchanged);
  RUN_CASE(check_forward_past_end_rejected_state_unchanged);
  RUN_CASE(check_empty_timeline_rewind_forward_rejected);
  RUN_CASE(check_export_dag_shows_causal_chain);
  RUN_CASE(check_partial_rewind_restores_only_latest_delta);
  if (failures > 0) {
    std::printf("time_travel_test: %d failing\n", failures);
    return 1;
  }
  std::printf("time_travel_test: all passing\n");
  return 0;
}
