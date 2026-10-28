// Batch 2 (Tier-2 native path, FR-STATE-003) contract tests — T4a (TC-015).
// Spec refs: FR-STATE-003, AC-STATE-015 (TC-STATE-015).
// Hermetic: C++17 + committed engine/state headers only; injected harness
// fakes only (JsCounter/VsyncClock from frame.hpp), std::thread ok, no env/
// config/network. Production path takes no JS/JNI handle at all — the
// counters are test-only observation fakes threaded through the harness
// entry point.
//
// ASSUMPTIONS (see unit-tests/T4.md):
//   A-T4-1: `frame.hpp` declares `JsCounter` (default-constructible, with
//     `int count() const` reporting observed JS hops), `VsyncClock`
//     (default-constructible, with `void tick()` and `int elapsed() const`),
//     `FrameStats`, and `propagateNative` exactly as the T4 `Interface:` line
//     states. Only the `Interface:` signature is binding; the fake member
//     shapes are the minimal harness the coder must provide.
//   A-T4-2: `vsyncsElapsed <= 1` is the observable form of "one-VSYNC
//     delivery" on the fake clock.
#include <cstdio>

#include "muse/state/atom.hpp"
#include "muse/state/frame.hpp"

namespace {

using muse::state::AtomId;
using muse::state::AtomStore;
using muse::state::Batch;
using muse::state::FrameStats;
using muse::state::JsCounter;
using muse::state::ObserverId;
using muse::state::Provenance;
using muse::state::TypedValue;
using muse::state::VsyncClock;

// TC-STATE-015: native-originated propagation performs zero JS/JNI hops.
static bool check_native_propagation_reports_zero_js_jni_hops() {
  AtomStore s;
  AtomId id = s.create(TypedValue(1));
  s.read(id, static_cast<ObserverId>(1));
  s.write(id, TypedValue(2), Provenance::kJs);
  Batch b = s.flush();
  JsCounter js;
  VsyncClock clk;
  FrameStats st = propagateNative(s, b, js, clk);
  return st.jsHops == 0 && st.jniHops == 0;
}

// One-VSYNC delivery: the propagation reports at most one VSYNC elapsed.
static bool check_native_propagation_delivers_within_one_vsync() {
  AtomStore s;
  AtomId id = s.create(TypedValue(1));
  s.read(id, static_cast<ObserverId>(1));
  s.write(id, TypedValue(2), Provenance::kJs);
  Batch b = s.flush();
  JsCounter js;
  VsyncClock clk;
  FrameStats st = propagateNative(s, b, js, clk);
  return st.vsyncsElapsed <= 1;
}

// The injected JS counter stays flat across native propagation: the
// production path takes no JS handle, so no hop can be observed.
static bool check_js_counter_stays_flat_during_native_propagation() {
  AtomStore s;
  AtomId id = s.create(TypedValue(1));
  s.read(id, static_cast<ObserverId>(1));
  s.write(id, TypedValue(2), Provenance::kJs);
  Batch b = s.flush();
  JsCounter js;
  VsyncClock clk;
  int before = js.count();
  (void)propagateNative(s, b, js, clk);
  return js.count() == before;
}

// Boundary: propagating an empty (drained) batch still reports zero hops
// and never exceeds one VSYNC.
static bool check_empty_batch_still_zero_hops_within_one_vsync() {
  AtomStore s;
  Batch b = s.flush();  // drained: empty
  if (!b.empty()) return false;
  JsCounter js;
  VsyncClock clk;
  FrameStats st = propagateNative(s, b, js, clk);
  return st.jsHops == 0 && st.jniHops == 0 && st.vsyncsElapsed <= 1;
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
  RUN_CASE(check_native_propagation_reports_zero_js_jni_hops);
  RUN_CASE(check_native_propagation_delivers_within_one_vsync);
  RUN_CASE(check_js_counter_stays_flat_during_native_propagation);
  RUN_CASE(check_empty_batch_still_zero_hops_within_one_vsync);
  if (failures > 0) {
    std::printf("native_path_test: %d failing\n", failures);
    return 1;
  }
  std::printf("native_path_test: all passing\n");
  return 0;
}
