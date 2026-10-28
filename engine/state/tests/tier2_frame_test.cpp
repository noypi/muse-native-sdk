// Batch 2 (Tier-2 native path, FR-STATE-003) contract tests — T4b (TC-016).
// Spec refs: FR-STATE-003, AC-STATE-016 (TC-STATE-016). Depends on: T3.
// Hermetic: C++17 + committed engine/state headers only; injected harness
// fakes only (JsCounter/VsyncClock from frame.hpp), std::thread ok, no env/
// config/network. A Tier-2 write is performed with the JS counter armed;
// the frame update must arrive within one VSYNC tick while the counter
// stays flat.
//
// ASSUMPTIONS (see unit-tests/T4.md): A-T4-1, A-T4-2 from native_path_test
// plus A-T3-1/A-T3-2 (Provenance::kTier2 added by the coder; enum lives in
// exactly one header) and A-T4-3: the value written by tier2Write is the
// value observable via AtomStore::read after propagateNative (last-writer-
// wins survives propagation).
#include <cstdio>

#include "muse/state/atom.hpp"
#include "muse/state/frame.hpp"
#include "muse/state/provenance.hpp"

namespace {

using muse::state::AtomId;
using muse::state::AtomStore;
using muse::state::Batch;
using muse::state::FrameStats;
using muse::state::JsCounter;
using muse::state::ObserverId;
using muse::state::Status;
using muse::state::TypedValue;
using muse::state::VsyncClock;

// TC-STATE-016: Tier-2 write + native propagate delivers within one VSYNC
// while the armed JS counter stays flat.
static bool check_tier2_write_propagates_within_one_vsync_counter_flat() {
  AtomStore s;
  AtomId id = s.create(TypedValue(1));
  s.read(id, static_cast<ObserverId>(1));
  if (tier2Write(s, id, TypedValue(42)) != Status::kOk) return false;
  Batch b = s.flush();
  JsCounter js;
  VsyncClock clk;
  int before = js.count();
  FrameStats st = propagateNative(s, b, js, clk);
  if (js.count() != before) return false;
  if (st.jsHops != 0 || st.jniHops != 0) return false;
  return st.vsyncsElapsed <= 1;
}

// Last-writer-wins survives propagation: JS then Tier-2 leaves the Tier-2
// value readable after propagateNative.
static bool check_tier2_last_writer_wins_visible_after_propagate() {
  AtomStore s;
  AtomId id = s.create(TypedValue(0));
  s.read(id, static_cast<ObserverId>(1));
  s.write(id, TypedValue(10),
          muse::state::Provenance::kJs);
  if (tier2Write(s, id, TypedValue(20)) != Status::kOk) return false;
  Batch b = s.flush();
  JsCounter js;
  VsyncClock clk;
  (void)propagateNative(s, b, js, clk);
  auto got = s.read(id, static_cast<ObserverId>(2));
  return got.ok() && got.value() == TypedValue(20);
}

// Provenance of the Tier-2 writer is still reported after propagation.
static bool check_tier2_provenance_preserved_across_propagation() {
  AtomStore s;
  AtomId id = s.create(TypedValue(0));
  s.read(id, static_cast<ObserverId>(1));
  if (tier2Write(s, id, TypedValue(7)) != Status::kOk) return false;
  Batch b = s.flush();
  JsCounter js;
  VsyncClock clk;
  (void)propagateNative(s, b, js, clk);
  return provenanceOf(s, id) == muse::state::Provenance::kTier2;
}

}  // namespace

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);       \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_tier2_write_propagates_within_one_vsync_counter_flat);
  RUN_CASE(check_tier2_last_writer_wins_visible_after_propagate);
  RUN_CASE(check_tier2_provenance_preserved_across_propagation);
  if (failures > 0) {
    std::printf("tier2_frame_test: %d failing\n", failures);
    return 1;
  }
  std::printf("tier2_frame_test: all passing\n");
  return 0;
}
