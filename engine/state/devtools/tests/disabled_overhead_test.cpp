// Batch 3 (STATE-DEVTOOLS, FR-STATE-008) contract tests — T7.
// Spec refs: FR-STATE-008, AC-STATE-033 (TC-STATE-033), AC-STATE-034
// (TC-STATE-034). Assumption A5: MUSE_STATE_DEVTOOLS macro gate
// (0 in release).
// Hermetic: C++17 + committed engine/state headers only; no env/config
// read, no network, no live service, no threads.
// Binds ONLY to the T7 `Interface:` line; never to implementation.
//
// HOW TO BUILD THIS FILE (build-gate property):
//   Disabled build: g++ -DMUSE_STATE_DEVTOOLS=0 ... disabled_overhead_test.cpp
//   Enabled build:  g++ -DMUSE_STATE_DEVTOOLS=1 ... (for the delta leg)
// Structural ASSUMPTIONS (see unit-tests/T7.md):
//   A-DO1: with MUSE_STATE_DEVTOOLS=0, "muse/state/devtools.hpp" declares
//          NO DevTimeline/Snapshot/Dag/record/rewind/forward/present/
//          exportDag symbols (single-branch gate, no ring buffer).
//   A-DO2: benchmark entry signature is `double
//          benchmark_disabled_vs_enabled()` (plan text carries both
//          `long` and `double deltaMs`; double is authoritative here).
//   A-DO3: `DeltaMs` small threshold 5.0ms is the "no measurable delta"
//          bound asserted by TC-STATE-034 (plan pins no number).
//   A-DO4: AtomStore public API is byte-identical with the gate off.
#include <cstdio>

#include "muse/state/atom.hpp"
#include "muse/state/devtools.hpp"

// Gate assertion: this translation unit must see the DISABLED side.
// If the gate leaks (DevTimeline visible while disabled), fail fast.
#if defined(MUSE_STATE_DEVTOOLS) && MUSE_STATE_DEVTOOLS != 0
#error "disabled_overhead_test must be built with MUSE_STATE_DEVTOOLS=0"
#endif

double benchmark_disabled_vs_enabled();

namespace {

using muse::state::AtomId;
using muse::state::AtomStore;
using muse::state::Provenance;
using muse::state::TypedValue;

// Defined in devtools.cpp at global scope (see declaration above).

// AtomStore API is unchanged with devtools disabled.
static bool check_atomstore_api_unchanged_when_disabled() {
  AtomStore store;
  AtomId id = store.create(TypedValue(1));
  store.write(id, TypedValue(2), Provenance::kJs);
  auto r = store.read(id, 0);
  if (!r.ok() || r.value() != TypedValue(2)) return false;
  auto batch = store.flush();
  if (batch.size() != 1) return false;
  return true;
}

// Zero ring-buffer allocation property: repeated writes/flushes stay
// allocation-free from the test's viewpoint (no devtools hook observable;
// simply exercises the hot path N times and requires kOk throughout).
static bool check_hot_path_stays_plain_without_devtools() {
  AtomStore store;
  AtomId id = store.create(TypedValue(0));
  for (int i = 1; i <= 1000; ++i) {
    store.write(id, TypedValue(i), Provenance::kJs);
    auto r = store.read(id, 0);
    if (!r.ok() || r.value() != TypedValue(i)) return false;
  }
  auto batch = store.flush();
  if (batch.size() != 1) return false;
  return true;
}

// Release benchmark shows no measurable delta vs devtools-disabled.
static bool check_benchmark_delta_negligible() {
  double delta_ms = benchmark_disabled_vs_enabled();
  if (delta_ms < 0.0) return false;
  return delta_ms < 5.0;
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
  RUN_CASE(check_atomstore_api_unchanged_when_disabled);
  RUN_CASE(check_hot_path_stays_plain_without_devtools);
  RUN_CASE(check_benchmark_delta_negligible);
  if (failures > 0) {
    std::printf("disabled_overhead_test: %d failing\n", failures);
    return 1;
  }
  std::printf("disabled_overhead_test: all passing\n");
  return 0;
}
