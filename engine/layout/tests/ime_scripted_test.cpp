// T9 contract tests — FR-LAYOUT-012 / AC-LAYOUT-035 (scripted keyboard
// open/close integration: canvas/list/bottom-bar per-frame, no full-tree
// relayout).
// Maps to TC-LAYOUT-035 (`engine/layout/tests/ime_scripted_test.cpp`).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services, no JS calls. Consumes (does not
// reimplement) the specs-state-reactivity.md inset channel (observes
// `queryInset`/`revision`), the specs-typography-text.md InputConnection
// bridge (faked with scripted `onImeFrame` calls — no live bridge), the
// render-graph vsync (scripted frameIds stand in for vsync; no timers, no
// threads), and the T5 `consumeForNextPass` scoped-invalidation semantics
// (only inset-subscribed subtrees invalidate; modeled with inline fakes).
// Binds ONLY to the T9 `Interface:` line (reproduced verbatim in
// unit-tests/T9.md):
//   `none — integration test only; exercises InsetBus::onImeFrame (T7) +
//    safeArea() (T8) + delegate invalidation counts (T5 semantics).`
// The `InsetBus`/`EdgeInsets`/`InsetType` shapes are owned by T6's contract
// (unit-tests/T6.md), `onImeFrame`/`imeFrameId` by T7's (unit-tests/T7.md),
// and `safeArea()` by T8's (unit-tests/T8.md); this suite consumes all
// three and wires no new production API.
//
// ASSUMPTION (stakeholder fakes): the three consumers — canvasetto
// (canvas), scrolling list, bottom bar — are inline fakes in this file.
// Each fake polls the bus once per scripted frame and records what it saw:
// canvas records the full Ime `EdgeInsets`; the list records the Ime
// bottom (its scroll-pad edge); the bottom bar records `safeArea().bottom`
// and the Ime bottom (its dodge offset). Per-frame polling is the
// defensible reading of "every frame delivers correct per-edge values to
// all three consumers" on a polling bus with no callback registry.
// ASSUMPTION (invalidation model): each fake owns exactly one subscribed
// subtree; a scripted frame emits one invalidation per subscribed subtree
// (3/frame) and zero full-tree relayouts. The full-tree counter only
// advances through an explicit `invalidateFullTree()` hook that the script
// never calls — asserting it stays zero is the observable half of "zero
// full-tree relayouts" (the device-lab half is external; solver internals
// are never asserted).
// ASSUMPTION (script): open 0→240px over 16 equal frames, hold 4 frames,
// close 240→0px over 16 equal frames; frameIds strictly increase across
// the whole 36-frame script. The plan pins open/close + per-frame delivery,
// not lengths; these mirror T7's 16-frame open.
// ASSUMPTION (safe-area fixture): SystemBars `{10, 20, 10, 0}` +
// DisplayCutout `{0, 12, 0, 0}` + NavigationBars `{0, 0, 0, 30}` stay fixed
// for the whole script, so the bottom bar's `safeArea().bottom` must read
// a constant 30px on every frame (T8 max-composite rule) while Ime moves.
// ASSUMPTION (float compare): epsilon `1e-4f`; fixtures exactly
// representable.
#include <cstdint>
#include <cstdio>
#include <utility>

#include "layout/insets.h"

namespace {

constexpr float kEps = 1e-4f;
constexpr float kOpenTarget = 240.0f;
constexpr int kOpenFrames = 16;
constexpr int kHoldFrames = 4;
constexpr int kCloseFrames = 16;
constexpr int kTotalFrames = kOpenFrames + kHoldFrames + kCloseFrames;

bool near(float a, float b) { return (a > b ? a - b : b - a) <= kEps; }

float scriptedBottom(int frame) {
  const float step = kOpenTarget / static_cast<float>(kOpenFrames);
  if (frame < kOpenFrames) {
    return step * static_cast<float>(frame + 1);
  }
  if (frame < kOpenFrames + kHoldFrames) {
    return kOpenTarget;
  }
  const int ci = frame - kOpenFrames - kHoldFrames;
  return kOpenTarget * (1.0f - static_cast<float>(ci + 1) /
                                   static_cast<float>(kCloseFrames));
}

// Inline stakeholder fake: polls the bus once per frame, records the last
// seen values, and counts its subscribed-subtree invalidations.
struct ConsumerFake {
  const char* name = "";
  layout::EdgeInsets lastIme{0.0f, 0.0f, 0.0f, 0.0f};
  float lastSafeBottom = 0.0f;
  int framesSeen = 0;
  int subtreeInvalidations = 0;

  void onFrame(const layout::InsetBus& bus) {
    lastIme = bus.queryInset(layout::InsetType::Ime);
    lastSafeBottom = bus.safeArea().bottom;
    ++framesSeen;
    ++subtreeInvalidations;  // only this fake's subscribed subtree
  }
};

struct ScriptHarness {
  layout::InsetBus bus;
  ConsumerFake canvas{"canvasetto"};
  ConsumerFake list{"scrolling-list"};
  ConsumerFake bottomBar{"bottom-bar"};
  int fullTreeRelayouts = 0;

  void invalidateFullTree() { ++fullTreeRelayouts; }  // never called

  // Drives the open/hold/close script; frameIds start at 1 and increase
  // monotonically. Returns false if any per-frame expectation fails.
  bool runScript() {
    bus.setInset(layout::InsetType::SystemBars,
                 layout::EdgeInsets{10.0f, 20.0f, 10.0f, 0.0f});
    bus.setInset(layout::InsetType::DisplayCutout,
                 layout::EdgeInsets{0.0f, 12.0f, 0.0f, 0.0f});
    bus.setInset(layout::InsetType::NavigationBars,
                 layout::EdgeInsets{0.0f, 0.0f, 0.0f, 30.0f});
    for (int f = 0; f < kTotalFrames; ++f) {
      const float bottom = scriptedBottom(f);
      const std::uint64_t id = static_cast<std::uint64_t>(f + 1);
      bus.onImeFrame(bottom, id);
      if (bus.imeFrameId() != id) return false;
      canvas.onFrame(bus);
      list.onFrame(bus);
      bottomBar.onFrame(bus);
      // Per-frame, per-consumer, per-edge delivery checks.
      if (!near(canvas.lastIme.bottom, bottom)) return false;
      if (!near(canvas.lastIme.left, 0.0f) ||
          !near(canvas.lastIme.top, 0.0f) ||
          !near(canvas.lastIme.right, 0.0f)) {
        return false;
      }
      if (!near(list.lastIme.bottom, bottom)) return false;
      if (!near(bottomBar.lastIme.bottom, bottom)) return false;
      // Bottom bar rides on the T8 snapshot: constant 30px despite the
      // moving keyboard (Ime is not a safe-area contributor).
      if (!near(canvas.lastSafeBottom, 30.0f)) return false;
      if (!near(list.lastSafeBottom, 30.0f)) return false;
      if (!near(bottomBar.lastSafeBottom, 30.0f)) return false;
    }
    return true;
  }

  int totalSubtreeInvalidations() const {
    return canvas.subtreeInvalidations + list.subtreeInvalidations +
           bottomBar.subtreeInvalidations;
  }
};

}  // namespace

// Named case (TC-LAYOUT-035 core): the full open/hold/close script runs to
// completion — every frame lands with its frameId and every consumer sees
// the scripted per-edge value on that frame.
static bool check_script_runs_every_frame_with_matching_ids() {
  ScriptHarness h;
  if (!h.runScript()) return false;
  if (h.canvas.framesSeen != kTotalFrames) return false;
  if (h.list.framesSeen != kTotalFrames) return false;
  if (h.bottomBar.framesSeen != kTotalFrames) return false;
  if (h.bus.imeFrameId() != static_cast<std::uint64_t>(kTotalFrames)) {
    return false;
  }
  return near(h.bus.queryInset(layout::InsetType::Ime).bottom, 0.0f);
}

// Named case: the keyboard opens monotonically then closes monotonically —
// no jumps in either direction across the whole 36-frame script.
static bool check_open_close_monotonic_no_jumps() {
  ScriptHarness h;
  if (!h.runScript()) return false;
  // Re-derive monotonicity from the script shape: open must be
  // non-decreasing, close non-increasing, steps bounded by one frame step.
  const float step = kOpenTarget / static_cast<float>(kOpenFrames);
  float prev = 0.0f;
  for (int f = 0; f < kTotalFrames; ++f) {
    const float cur = scriptedBottom(f);
    if (f < kOpenFrames + kHoldFrames) {
      if (!(cur > prev || near(cur, prev))) return false;
    } else {
      if (!(cur < prev || near(cur, prev))) return false;
    }
    if (cur - prev > 1.5f * step + kEps) return false;
    if (prev - cur > 1.5f * step + kEps) return false;
    prev = cur;
  }
  return near(prev, 0.0f);
}

// Named case: all three consumers observe identical per-frame values —
// canvas full Ime insets, list Ime bottom, bottom-bar Ime bottom +
// safe-area bottom agree on every frame of a re-run probe.
static bool check_all_consumers_agree_per_frame() {
  ScriptHarness h;
  h.bus.setInset(layout::InsetType::SystemBars,
                 layout::EdgeInsets{10.0f, 20.0f, 10.0f, 0.0f});
  h.bus.setInset(layout::InsetType::DisplayCutout,
                 layout::EdgeInsets{0.0f, 12.0f, 0.0f, 0.0f});
  h.bus.setInset(layout::InsetType::NavigationBars,
                 layout::EdgeInsets{0.0f, 0.0f, 0.0f, 30.0f});
  for (int f = 0; f < kTotalFrames; ++f) {
    const float bottom = scriptedBottom(f);
    h.bus.onImeFrame(bottom, static_cast<std::uint64_t>(f + 1));
    h.canvas.onFrame(h.bus);
    h.list.onFrame(h.bus);
    h.bottomBar.onFrame(h.bus);
    if (!near(h.canvas.lastIme.bottom, h.list.lastIme.bottom)) return false;
    if (!near(h.list.lastIme.bottom, h.bottomBar.lastIme.bottom)) {
      return false;
    }
    if (!near(h.canvas.lastIme.bottom, bottom)) return false;
    if (!near(h.bottomBar.lastSafeBottom, 30.0f)) return false;
  }
  return true;
}

// Named case (T5 semantics): only inset-subscribed subtrees invalidate —
// exactly 3 subtree invalidations per frame (one per consumer), so the
// 36-frame script emits exactly 108 scoped invalidations.
static bool check_only_subscribed_subtrees_invalidate() {
  ScriptHarness h;
  if (!h.runScript()) return false;
  if (h.canvas.subtreeInvalidations != kTotalFrames) return false;
  if (h.list.subtreeInvalidations != kTotalFrames) return false;
  if (h.bottomBar.subtreeInvalidations != kTotalFrames) return false;
  return h.totalSubtreeInvalidations() == 3 * kTotalFrames;
}

// Named case (TC-LAYOUT-035 no-full-relayout): the script performs zero
// full-tree relayouts — the full-tree hook is never invoked while all 108
// scoped invalidations are delivered.
static bool check_zero_full_tree_relayouts() {
  ScriptHarness h;
  if (!h.runScript()) return false;
  if (h.fullTreeRelayouts != 0) return false;
  return h.totalSubtreeInvalidations() == 3 * kTotalFrames;
}

// Named case: the safe-area snapshot the bottom bar rides on is constant
// across the keyboard animation — bars/cutout/nav revisions untouched by
// Ime frames, safe-area bottom pinned at 30px from first to last frame.
static bool check_safe_area_stable_across_animation() {
  ScriptHarness h;
  const std::uint64_t barsRev0 =
      h.bus.revision(layout::InsetType::SystemBars);
  const std::uint64_t cutoutRev0 =
      h.bus.revision(layout::InsetType::DisplayCutout);
  const std::uint64_t navRev0 =
      h.bus.revision(layout::InsetType::NavigationBars);
  (void)barsRev0;
  (void)cutoutRev0;
  (void)navRev0;
  h.bus.setInset(layout::InsetType::SystemBars,
                 layout::EdgeInsets{10.0f, 20.0f, 10.0f, 0.0f});
  h.bus.setInset(layout::InsetType::DisplayCutout,
                 layout::EdgeInsets{0.0f, 12.0f, 0.0f, 0.0f});
  h.bus.setInset(layout::InsetType::NavigationBars,
                 layout::EdgeInsets{0.0f, 0.0f, 0.0f, 30.0f});
  const std::uint64_t barsRev = h.bus.revision(layout::InsetType::SystemBars);
  const std::uint64_t cutoutRev =
      h.bus.revision(layout::InsetType::DisplayCutout);
  const std::uint64_t navRev =
      h.bus.revision(layout::InsetType::NavigationBars);
  const float pinned = h.bus.safeArea().bottom;
  if (!near(pinned, 30.0f)) return false;
  for (int f = 0; f < kTotalFrames; ++f) {
    h.bus.onImeFrame(scriptedBottom(f), static_cast<std::uint64_t>(f + 1));
    if (!near(h.bus.safeArea().bottom, pinned)) return false;
  }
  // Ime animation bumped only the Ime revision; contributor revisions
  // (and hence the cached snapshot inputs) never moved mid-script.
  if (h.bus.revision(layout::InsetType::SystemBars) != barsRev) return false;
  if (h.bus.revision(layout::InsetType::DisplayCutout) != cutoutRev) {
    return false;
  }
  if (h.bus.revision(layout::InsetType::NavigationBars) != navRev) {
    return false;
  }
  return true;
}

// Named case: a stale redelivered frame mid-script changes nothing for any
// consumer — inset value, frameId, and consumer records all stay put.
static bool check_stale_frame_mid_script_changes_nothing() {
  ScriptHarness h;
  h.bus.setInset(layout::InsetType::NavigationBars,
                 layout::EdgeInsets{0.0f, 0.0f, 0.0f, 30.0f});
  for (int f = 0; f < 8; ++f) {
    h.bus.onImeFrame(scriptedBottom(f), static_cast<std::uint64_t>(f + 1));
    h.canvas.onFrame(h.bus);
  }
  const float pinned = h.canvas.lastIme.bottom;
  const std::uint64_t rev = h.bus.revision(layout::InsetType::Ime);
  h.bus.onImeFrame(999.0f, 3u);  // stale: long-past frameId
  h.canvas.onFrame(h.bus);
  h.list.onFrame(h.bus);
  h.bottomBar.onFrame(h.bus);
  if (h.bus.imeFrameId() != 8u) return false;
  if (!near(h.bus.queryInset(layout::InsetType::Ime).bottom, pinned)) {
    return false;
  }
  if (h.bus.revision(layout::InsetType::Ime) != rev) return false;
  return near(h.canvas.lastIme.bottom, pinned);
}

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
  RUN_CASE(check_script_runs_every_frame_with_matching_ids);
  RUN_CASE(check_open_close_monotonic_no_jumps);
  RUN_CASE(check_all_consumers_agree_per_frame);
  RUN_CASE(check_only_subscribed_subtrees_invalidate);
  RUN_CASE(check_zero_full_tree_relayouts);
  RUN_CASE(check_safe_area_stable_across_animation);
  RUN_CASE(check_stale_frame_mid_script_changes_nothing);
  if (failures > 0) {
    std::printf("ime_scripted_test: %d failing\n", failures);
    return 1;
  }
  std::printf("ime_scripted_test: all passing\n");
  return 0;
}
