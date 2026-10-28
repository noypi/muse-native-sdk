// Batch 2 T3 contract tests — FR-LAYOUT-013 AC-LAYOUT-037 (TC-037 external).
// Hermetic: C++17 + layout/window.h only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T3 `Interface:` line (verbatim in contract T3.md):
//   `void applyDisplayMetrics(WindowId id, DisplayMetrics m /* {density, insets per edge} */) -> void`
//   — error: `LayoutError::kUnknownDisplay` for bad display.
//
// DEVICE-LAB PROCEDURE (TC-037, external — tester-executed, not CI):
//   1. Attach two physical displays with different densities (e.g. phone 2.0x
//      + desktop 1.0x) and different system insets (gesture bar vs caption).
//   2. Open one window per display; apply each display's metrics via
//      applyDisplayMetrics.
//   3. Assert each window lays out with its own density/insets and that
//      changing display A's metrics never alters display B's layout.
//   The hermetic cases below mirror this with two synthetic profiles.
//
// ASSUMPTION (type shapes): the plan names WindowId/DisplayMetrics/LayoutError
//   but not their fields or error channel. Tests assume:
//     `using WindowId = std::uint32_t;`
//     `struct DisplayMetrics { float density; float insetLeft; float insetTop;
//       float insetRight; float insetBottom; };`
//     Errors surface as an exception of type `layout::LayoutError` whose value
//     compares equal to `layout::LayoutError::kUnknownDisplay` (same throw
//     convention as solver_test.cpp's std::out_of_range cases and the T1
//     kUnknownWindow contract). If the coder spells the channel differently,
//     the contract reconciles the call site; the kUnknownDisplay behavior
//     stands.
//
// ASSUMPTION (observation): applyDisplayMetrics returns void, so per-window
//   applied metrics are observed through `DisplayMetrics displayMetricsOf(
//   WindowId id)` in window.h, which echoes the last applied metrics for that
//   window (and throws kUnknownDisplay for a bad id). No cross-window leakage
//   is observable through it.
//
// ASSUMPTION (fixture setup): windows are created with the T1-settled
//   `WindowId createWindow(PxSize size)` entry point (Batch 1, staged context);
//   this file does not redeclare or modify it.
#include <cstdint>
#include <cstdio>

#include "layout/window.h"

namespace {

layout::PxSize px(float w, float h) {
  layout::PxSize s;
  s.width = w;
  s.height = h;
  return s;
}

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

bool is_unknown_display_error(const layout::LayoutError& e) {
  return e == layout::LayoutError::kUnknownDisplay;
}

bool same_metrics(const layout::DisplayMetrics& a,
                  const layout::DisplayMetrics& b) {
  return a.density == b.density && a.insetLeft == b.insetLeft &&
         a.insetTop == b.insetTop && a.insetRight == b.insetRight &&
         a.insetBottom == b.insetBottom;
}

}  // namespace

// AC-LAYOUT-037: each window lays out with its own density — two windows on
// different synthetic densities report their own applied metrics.
static bool check_each_window_keeps_own_density() {
  layout::WindowId a = layout::createWindow(px(360.0f, 640.0f));
  layout::WindowId b = layout::createWindow(px(1200.0f, 800.0f));
  layout::applyDisplayMetrics(a, metrics(2.0f, 0.0f, 24.0f, 0.0f, 16.0f));
  layout::applyDisplayMetrics(b, metrics(1.0f, 0.0f, 0.0f, 0.0f, 0.0f));
  return same_metrics(layout::displayMetricsOf(a),
                      metrics(2.0f, 0.0f, 24.0f, 0.0f, 16.0f)) &&
         same_metrics(layout::displayMetricsOf(b),
                      metrics(1.0f, 0.0f, 0.0f, 0.0f, 0.0f));
}

// AC-LAYOUT-037: per-edge insets are applied independently per window.
static bool check_each_window_keeps_own_insets() {
  layout::WindowId a = layout::createWindow(px(360.0f, 640.0f));
  layout::WindowId b = layout::createWindow(px(1200.0f, 800.0f));
  layout::applyDisplayMetrics(a, metrics(2.0f, 4.0f, 24.0f, 4.0f, 16.0f));
  layout::applyDisplayMetrics(b, metrics(3.0f, 0.0f, 48.0f, 0.0f, 0.0f));
  const layout::DisplayMetrics got = layout::displayMetricsOf(b);
  return got.insetLeft == 0.0f && got.insetTop == 48.0f &&
         got.insetRight == 0.0f && got.insetBottom == 0.0f;
}

// AC-LAYOUT-037: no cross-contamination — re-applying display A's metrics
// never alters display B's stored metrics.
static bool check_reapply_does_not_contaminate_other_window() {
  layout::WindowId a = layout::createWindow(px(360.0f, 640.0f));
  layout::WindowId b = layout::createWindow(px(1200.0f, 800.0f));
  layout::applyDisplayMetrics(a, metrics(2.0f, 0.0f, 24.0f, 0.0f, 16.0f));
  layout::applyDisplayMetrics(b, metrics(1.0f, 0.0f, 0.0f, 0.0f, 0.0f));
  layout::applyDisplayMetrics(a, metrics(3.0f, 8.0f, 48.0f, 8.0f, 32.0f));
  return same_metrics(layout::displayMetricsOf(b),
                      metrics(1.0f, 0.0f, 0.0f, 0.0f, 0.0f)) &&
         same_metrics(layout::displayMetricsOf(a),
                      metrics(3.0f, 8.0f, 48.0f, 8.0f, 32.0f));
}

// Boundary: re-applying identical metrics is a stable no-op.
static bool check_identical_reapply_is_stable() {
  layout::WindowId a = layout::createWindow(px(360.0f, 640.0f));
  layout::applyDisplayMetrics(a, metrics(2.0f, 0.0f, 24.0f, 0.0f, 16.0f));
  layout::applyDisplayMetrics(a, metrics(2.0f, 0.0f, 24.0f, 0.0f, 16.0f));
  return same_metrics(layout::displayMetricsOf(a),
                      metrics(2.0f, 0.0f, 24.0f, 0.0f, 16.0f));
}

// Error path: unknown window id reports LayoutError::kUnknownDisplay.
static bool check_unknown_display_reports_error() {
  try {
    layout::applyDisplayMetrics(static_cast<layout::WindowId>(9999),
                                metrics(2.0f, 0.0f, 0.0f, 0.0f, 0.0f));
  } catch (const layout::LayoutError& e) {
    return is_unknown_display_error(e);
  } catch (...) {
    return false;
  }
  return false;
}

// Error path: the invalid sentinel also reports kUnknownDisplay.
// (ASSUMPTION: the invalid id constant is `layout::kInvalidWindowId`,
// settled by T1; reconciles if renamed.)
static bool check_invalid_window_id_reports_unknown_display() {
  try {
    layout::applyDisplayMetrics(layout::kInvalidWindowId,
                                metrics(1.0f, 0.0f, 0.0f, 0.0f, 0.0f));
  } catch (const layout::LayoutError& e) {
    return is_unknown_display_error(e);
  } catch (...) {
    return false;
  }
  return false;
}

// Ghost-id hole: a never-issued small id below any fast-reject cap (500,
// with only two windows created) is rejected by the windowExists oracle on
// both the apply and observe paths — never silently accepted.
static bool check_never_issued_small_id_reports_unknown_display() {
  layout::WindowId a = layout::createWindow(px(360.0f, 640.0f));
  layout::WindowId b = layout::createWindow(px(1200.0f, 800.0f));
  if (!layout::windowExists(a) || !layout::windowExists(b)) return false;
  if (layout::windowExists(static_cast<layout::WindowId>(500))) return false;
  bool apply_threw = false;
  try {
    layout::applyDisplayMetrics(static_cast<layout::WindowId>(500),
                                metrics(2.0f, 0.0f, 0.0f, 0.0f, 0.0f));
  } catch (const layout::LayoutError& e) {
    apply_threw = is_unknown_display_error(e);
  } catch (...) {
    return false;
  }
  if (!apply_threw) return false;
  try {
    (void)layout::displayMetricsOf(static_cast<layout::WindowId>(500));
  } catch (const layout::LayoutError& e) {
    return is_unknown_display_error(e);
  } catch (...) {
    return false;
  }
  return false;
}

#define RUN_CASE(name)                  \
  do {                                  \
    if (!(name)()) {                    \
      std::printf("FAIL %s\n", #name);  \
      ++failures;                       \
    } else {                            \
      std::printf("PASS %s\n", #name);  \
    }                                   \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_each_window_keeps_own_density);
  RUN_CASE(check_each_window_keeps_own_insets);
  RUN_CASE(check_reapply_does_not_contaminate_other_window);
  RUN_CASE(check_identical_reapply_is_stable);
  RUN_CASE(check_unknown_display_reports_error);
  RUN_CASE(check_invalid_window_id_reports_unknown_display);
  RUN_CASE(check_never_issued_small_id_reports_unknown_display);
  if (failures > 0) {
    std::printf("display_metrics_test: %d failing\n", failures);
    return 1;
  }
  std::printf("display_metrics_test: all passing\n");
  return 0;
}
