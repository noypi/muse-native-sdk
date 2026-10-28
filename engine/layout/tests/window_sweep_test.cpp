// Batch 2 T5 contract tests — FR-LAYOUT-013 AC-LAYOUT-039 (TC-039) with the
// required FR-LIFECYCLE-003 dependency consumed read-only.
// Hermetic: C++17 + layout/window.h + layout/solver.h only; no network, no
// env/config reads, no filesystem, no live services.
// Binds ONLY to the T5 `Interface:` line (verbatim in contract T5.md):
//   `SweepReport runResizeSweep(SweepSpec spec /* widths, focus node, scroll anchor, display move */) -> { bool focusKept; bool scrollKept; bool densityRelayoutClean }`
//   — no error surface; asserts internally.
//
// FR-LIFECYCLE-003 is consumed read-only: the sweep delivers the display-move
// config in place (no lifecycle change, no recreation); this file asserts the
// layout-side retention flags only and never touches lifecycle behavior.
//
// ASSUMPTION (type shapes): the plan names SweepSpec/SweepReport but not
//   their fields. Tests assume:
//     `struct SweepSpec { float widths[8]; std::size_t widthCount;
//       std::uint32_t focusField; std::uint32_t scrollNode;
//       DisplayMetrics fromDisplay; DisplayMetrics toDisplay;
//       bool moveDisplayMidSweep; };`
//     `struct SweepReport { bool focusKept; bool scrollKept;
//       bool densityRelayoutClean; };`
//   DisplayMetrics is the T3-settled shape (density + insets per edge). If
//   the coder settles different field names, the contract reconciles the
//   construction call sites; the retention expectations do not change.
//
// ASSUMPTION (fixture): runResizeSweep builds its own window from the spec
//   (widths driven phone->desktop, focusField/scrollNode observed) and, when
//   moveDisplayMidSweep is true, delivers toDisplay in place mid-script. No
//   caller-side window setup is needed.
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "layout/solver.h"
#include "layout/window.h"

namespace {

const float kPhoneToDesktop[] = {360.0f, 500.0f, 700.0f, 900.0f, 1200.0f};

layout::DisplayMetrics metrics(float density, float l, float t, float r,
                               float b) {
  layout::DisplayMetrics m;
  m.density = density;
  m.insetLeft = l;
  m.insetTop = t;
  m.insetRight = r;
  m.insetBottom = b;
  return m;
}

layout::SweepSpec make_spec(bool moveDisplay) {
  layout::SweepSpec spec;
  spec.widthCount = 5;
  for (std::size_t i = 0; i < spec.widthCount; ++i) {
    spec.widths[i] = kPhoneToDesktop[i];
  }
  spec.focusField = 1;
  spec.scrollNode = 1;
  spec.fromDisplay = metrics(2.0f, 0.0f, 24.0f, 0.0f, 16.0f);
  spec.toDisplay = metrics(1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  spec.moveDisplayMidSweep = moveDisplay;
  return spec;
}

}  // namespace

// AC-LAYOUT-039: phone->desktop sweep keeps the focused field focused.
static bool check_sweep_keeps_focus() {
  layout::SweepReport r = layout::runResizeSweep(make_spec(false));
  return r.focusKept;
}

// AC-LAYOUT-039: phone->desktop sweep keeps the scroll anchor at position.
static bool check_sweep_keeps_scroll() {
  layout::SweepReport r = layout::runResizeSweep(make_spec(false));
  return r.scrollKept;
}

// AC-LAYOUT-039: display move mid-sweep re-lays out with the new density and
// reports no jump (densityRelayoutClean).
static bool check_display_move_relayouts_clean() {
  layout::SweepReport r = layout::runResizeSweep(make_spec(true));
  return r.densityRelayoutClean;
}

// AC-LAYOUT-039 combined: sweep WITH display move keeps focus and scroll AND
// relayouts clean — the full in-place proof (depends on T1 + T3 paths).
static bool check_sweep_with_display_move_keeps_all() {
  layout::SweepReport r = layout::runResizeSweep(make_spec(true));
  return r.focusKept && r.scrollKept && r.densityRelayoutClean;
}

// Boundary: single-width sweep (no resize steps) trivially retains all flags.
static bool check_single_width_sweep_is_stable() {
  layout::SweepSpec spec = make_spec(false);
  spec.widthCount = 1;
  spec.widths[0] = 700.0f;
  layout::SweepReport r = layout::runResizeSweep(spec);
  return r.focusKept && r.scrollKept && r.densityRelayoutClean;
}

// Boundary: sweep with identical from/to displays (no density change) still
// reports a clean relayout.
// (ASSUMPTION: densityRelayoutClean is true when no density change occurs;
// reconciles if the coder settles a stricter "move required" rule.)
static bool check_sweep_without_display_change_is_clean() {
  layout::SweepSpec spec = make_spec(true);
  spec.toDisplay = spec.fromDisplay;
  layout::SweepReport r = layout::runResizeSweep(spec);
  return r.focusKept && r.scrollKept && r.densityRelayoutClean;
}

#define RUN_CASE(name)                  \
  do {                                  \
    if (!(name)()) {                    \
      std::printf("FAIL %s\n", #name);  \
      ++failures;                       \
    } else {                            \
      std::printf("PASS %s\n", #name); \
    }                                   \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_sweep_keeps_focus);
  RUN_CASE(check_sweep_keeps_scroll);
  RUN_CASE(check_display_move_relayouts_clean);
  RUN_CASE(check_sweep_with_display_move_keeps_all);
  RUN_CASE(check_single_width_sweep_is_stable);
  RUN_CASE(check_sweep_without_display_change_is_clean);
  if (failures > 0) {
    std::printf("window_sweep_test: %d failing\n", failures);
    return 1;
  }
  std::printf("window_sweep_test: all passing\n");
  return 0;
}
