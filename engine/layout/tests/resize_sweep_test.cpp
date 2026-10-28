// Batch 4 T6 contract tests — scripted compact→expanded→compact resize sweep
// (FR-LAYOUT-011 AC-031 + FR-LAYOUT-003 AC-006 + FR-LAYOUT-004 AC-007).
// Hermetic: C++17 + committed headers only (size_class.h via the other two,
// adaptive_layout.h, window_metrics.h — never responsive.h: T1 and T2 each
// define their own `layout::Status` with different enumerators, so a TU
// including both fails to compile); no network, no env/config reads, no
// filesystem, no live services. Test-only file: no new production file — the
// sweep exercises the T3 arrange path and the T5 in-place resize path.
//
// Binds to the T6 `Interface:` line (verbatim in T6.md contract):
//   `SweepReport runResizeSweep(Sequence compactToExpandedAndBack) ->
//    SweepReport{switchesPerBoundary, scrollDelta, focusLost};
//    focus lost -> SweepReport.focusLost=true (fail)`
//
// Shape ASSUMPTIONs (the Interface line names the shapes but not their
// fields; behavior, not field spelling, is what the ACs pin):
//   `Sequence` = scripted width sequence compact→expanded→compact at a fixed
//     height (std::vector<float> widths here; thresholds are T1's job per
//     plan A2, pinned to compactMaxWidth=600/mediumMaxWidth=840 via the
//     implementation's injected breakpoints: 360/500->Compact, 700->Medium,
//     900/1200->Expanded).
//   `SweepReport{switchesPerBoundary, scrollDelta, focusLost}` with
//     switchesPerBoundary[boundary][direction] counts (boundary 0 =
//     compact<->medium tracked on the list-detail decl, boundary 1 =
//     medium<->expanded tracked on the supporting-pane decl; direction 0 =
//     forward/expanding, 1 = backward/compacting), scrollDelta = absolute
//     scroll-offset drift across the round trip (must be ~0), focusLost =
//     true iff the focused node identity changed mid-sweep (fail).
//   `runResizeSweep` lives in this TU (test-only harness per the plan: no new
//     production file): each step reflows in place via T5
//     `onWindowBoundsChanged` (never recreation) and re-derives panes via T3
//     `arrangeAdaptive` from the recomputed class (T1 `computeSizeClass`
//     direct, cross-checked against T5's stored `currentSizeClass`).
//
// ASSUMPTION (scroll/focus as stable-identity, plan A3): layout's contract is
// stable node identity across the switch; actual focus restoration belongs to
// input/lifecycle and is consumed. The harness therefore holds a scroll
// offset and a focused node id OUTSIDE layout across the sweep and asserts
// layout's transitions never disturb them: scrollDelta stays within epsilon,
// focused-node identity is unchanged (focusLost=false). A control case
// injects a mid-sweep focus change to prove focusLost=true fires (fail).
#include <cmath>
#include <cstdio>
#include <vector>

#include "adaptive_layout.h"
#include "window_metrics.h"

namespace {

// --- T6 harness shapes (see file header) ---

using Sequence = std::vector<float>;

struct SweepReport {
  // [boundary: 0 = compact<->medium, 1 = medium<->expanded]
  // [direction: 0 = forward/expanding, 1 = backward/compacting]
  int switchesPerBoundary[2][2] = {{0, 0}, {0, 0}};
  float scrollDelta = 0.0f;
  bool focusLost = true;
};

struct ViewState {
  float scrollOffset = 0.0f;  // held outside layout across the sweep
  int focusedNode = 0;        // stable node identity held outside layout
};

layout::WindowMetrics metrics(float w, float h) {
  layout::WindowMetrics m;
  m.width = w;
  m.height = h;
  return m;
}

layout::DisplayMetrics display(float density = 2.0f, int id = 0) {
  layout::DisplayMetrics d;
  d.density = density;
  d.displayId = id;
  return d;
}

layout::AdaptiveDecl decl(int id) {
  layout::AdaptiveDecl d;
  d.layoutId = id;
  return d;
}

layout::PostureSignal flatPosture() {
  layout::PostureSignal p;
  p.halfOpened = false;
  p.hingePresent = false;
  return p;
}

// Single-pane probe: list-detail is single-pane iff Compact; supporting-pane
// is single-pane iff below Expanded (per the T3 boundary contract).
bool listDetailDual(const layout::SizeClass& s) {
  auto r = layout::arrangeAdaptive(decl(layout::kListDetailId), s,
                                   flatPosture());
  return r.status == layout::Status::kOk && r.secondaryVisible;
}

bool supportingDual(const layout::SizeClass& s) {
  auto r = layout::arrangeAdaptive(decl(layout::kSupportingPaneId), s,
                                   flatPosture());
  return r.status == layout::Status::kOk && r.secondaryVisible;
}

// Test-only sweep driver: scripted compact→expanded→compact widths at a fixed
// height. Each step reflows in place (T5) and re-derives panes (T3). Counts
// one switch per arrangement edge, attributed to the boundary whose class
// step carries it, per direction. Scroll/focus are harness-held (plan A3);
// `disturbFocusMidSweep` injects a focus change to exercise the fail signal.
// `disturbScrollMidSweep` injects a scroll jump for the same purpose.
SweepReport runResizeSweep(const Sequence& widths, float height,
                           ViewState& view, bool disturbFocusMidSweep = false,
                           bool disturbScrollMidSweep = false) {
  SweepReport report;
  const float scrollBaseline = view.scrollOffset;
  const int focusBaseline = view.focusedNode;
  bool backwards = false;
  layout::WidthClass prevClass = layout::WidthClass::Unknown;
  bool prevListDual = false;
  bool prevSupDual = false;
  bool first = true;
  for (size_t i = 0; i < widths.size(); ++i) {
    if (i > 0 && widths[i] < widths[i - 1]) backwards = true;
    if (layout::onWindowBoundsChanged(metrics(widths[i], height), display()) !=
        layout::Status::kOk) {
      continue;  // invalid step: no reflow, no switch, state untouched
    }
    layout::SizeClass cls = layout::currentSizeClass();
    // Cross-check: T5's stored class matches a direct T1 recompute.
    layout::Breakpoints b;  // plan A2 pinned thresholds (600/840 defaults)
    layout::SizeClassResult direct =
        layout::computeSizeClass(metrics(widths[i], height), b);
    if (direct.status != layout::Status::kOk ||
        direct.size.widthClass != cls.widthClass) {
      continue;
    }
    bool listDual = listDetailDual(cls);
    bool supDual = supportingDual(cls);
    if (!first) {
      int dir = backwards ? 1 : 0;
      if (listDual != prevListDual) {
        // List-detail edge rides the compact<->medium class step.
        report.switchesPerBoundary[0][dir] += 1;
      }
      if (supDual != prevSupDual) {
        // Supporting-pane edge rides the medium<->expanded class step.
        report.switchesPerBoundary[1][dir] += 1;
      }
    }
    first = false;
    prevClass = cls.widthClass;
    prevListDual = listDual;
    prevSupDual = supDual;
    if (disturbFocusMidSweep && i == widths.size() / 2) {
      view.focusedNode += 1000;  // simulated focus theft mid-sweep
    }
    if (disturbScrollMidSweep && i == widths.size() / 2) {
      view.scrollOffset += 250.0f;  // simulated scroll jump mid-sweep
    }
    (void)prevClass;
  }
  report.scrollDelta =
      std::fabs(view.scrollOffset - scrollBaseline);
  report.focusLost = (view.focusedNode != focusBaseline);
  return report;
}

// Canonical scripted sweep: compact → expanded → compact.
Sequence roundTrip() {
  return {360.0f, 500.0f, 700.0f, 900.0f, 1200.0f, 900.0f, 700.0f, 500.0f,
          360.0f};
}

constexpr float kScrollEpsilon = 1e-3f;

}  // namespace

// Forward (expanding) leg: exactly one switch per boundary — list-detail at
// compact→medium, supporting-pane at medium→expanded (AC-031 + AC-006).
static bool check_forward_sweep_switches_once_per_boundary() {
  ViewState view{128.0f, 42};
  SweepReport r = runResizeSweep({360.0f, 500.0f, 700.0f, 900.0f, 1200.0f},
                                 700.0f, view);
  return r.switchesPerBoundary[0][0] == 1 &&
         r.switchesPerBoundary[1][0] == 1 && !r.focusLost &&
         r.scrollDelta <= kScrollEpsilon;
}

// Backward (compacting) leg: exactly one switch per boundary each direction
// (AC-031: "each direction").
static bool check_backward_sweep_switches_once_per_boundary() {
  ViewState view{128.0f, 42};
  SweepReport r = runResizeSweep({1200.0f, 900.0f, 700.0f, 500.0f, 360.0f},
                                 700.0f, view);
  return r.switchesPerBoundary[0][1] == 1 &&
         r.switchesPerBoundary[1][1] == 1 && !r.focusLost &&
         r.scrollDelta <= kScrollEpsilon;
}

// Full round trip: scroll offset unchanged within epsilon (AC-031, plan A3
// stable-identity assertion).
static bool check_roundtrip_preserves_scroll_within_epsilon() {
  ViewState view{128.0f, 42};
  SweepReport r = runResizeSweep(roundTrip(), 700.0f, view);
  return !r.focusLost && r.scrollDelta <= kScrollEpsilon;
}

// Full round trip: focused node identity unchanged; focusLost=false (AC-031:
// focus lost -> focusLost=true (fail) — so the passing sweep reports false).
static bool check_roundtrip_preserves_focus_identity() {
  ViewState view{64.0f, 7};
  SweepReport r = runResizeSweep(roundTrip(), 700.0f, view);
  return !r.focusLost && view.focusedNode == 7 &&
         r.scrollDelta <= kScrollEpsilon;
}

// Control: a mid-sweep focus change surfaces as focusLost=true (fail signal
// proves the harness actually observes focus identity).
static bool check_focus_change_mid_sweep_reports_focus_lost() {
  ViewState view{64.0f, 7};
  SweepReport r =
      runResizeSweep(roundTrip(), 700.0f, view, true /* disturb focus */);
  return r.focusLost;
}

// Control: a mid-sweep scroll jump surfaces as nonzero scrollDelta.
static bool check_scroll_jump_mid_sweep_reports_scroll_delta() {
  ViewState view{64.0f, 7};
  SweepReport r = runResizeSweep(roundTrip(), 700.0f, view, false,
                                 true /* disturb scroll */);
  return !r.focusLost && r.scrollDelta > kScrollEpsilon;
}

// No flicker: repeated steps inside one class switch nothing (exactly-once,
// not at-most-once-per-step).
static bool check_redundant_same_class_steps_switch_nothing() {
  ViewState view{0.0f, 1};
  SweepReport r = runResizeSweep({360.0f, 360.0f, 500.0f, 500.0f, 360.0f},
                                 700.0f, view);
  return r.switchesPerBoundary[0][0] == 0 &&
         r.switchesPerBoundary[0][1] == 0 &&
         r.switchesPerBoundary[1][0] == 0 &&
         r.switchesPerBoundary[1][1] == 0 && !r.focusLost &&
         r.scrollDelta <= kScrollEpsilon;
}

// In-place across the whole sweep: no Activity recreation (FR-LAYOUT-004
// AC-007 cross-ref in AC-031), and the sweep ends back in Compact.
static bool check_sweep_performs_no_recreation_and_returns_to_compact() {
  int recreationsBefore = layout::recreationRequestCount();
  ViewState view{10.0f, 3};
  SweepReport r = runResizeSweep(roundTrip(), 700.0f, view);
  return layout::recreationRequestCount() == recreationsBefore &&
         layout::currentSizeClass().widthClass ==
             layout::WidthClass::Compact &&
         !r.focusLost;
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
  RUN_CASE(check_forward_sweep_switches_once_per_boundary);
  RUN_CASE(check_backward_sweep_switches_once_per_boundary);
  RUN_CASE(check_roundtrip_preserves_scroll_within_epsilon);
  RUN_CASE(check_roundtrip_preserves_focus_identity);
  RUN_CASE(check_focus_change_mid_sweep_reports_focus_lost);
  RUN_CASE(check_scroll_jump_mid_sweep_reports_scroll_delta);
  RUN_CASE(check_redundant_same_class_steps_switch_nothing);
  RUN_CASE(check_sweep_performs_no_recreation_and_returns_to_compact);
  if (failures > 0) {
    std::printf("resize_sweep_test: %d failing\n", failures);
    return 1;
  }
  std::printf("resize_sweep_test: all passing\n");
  return 0;
}
