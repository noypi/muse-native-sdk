// Batch 2 T4 contract tests — FR-LAYOUT-013 AC-LAYOUT-038 (TC-038).
// Hermetic: C++17 + layout/window.h + layout/solver.h only; no network, no
// env/config reads, no filesystem, no live services.
// Binds ONLY to the T4 `Interface:` line (verbatim in contract T4.md):
//   `DragTarget resolveDropTarget(DragSession s, PxPoint p, WindowBounds resizing) -> WindowId`
//   — hit-test in-flight bounds; error `LayoutError::kNoTarget` outside all windows.
//
// ASSUMPTION (type shapes): the plan names DragSession/PxPoint/WindowBounds/
//   DragTarget/WindowId/LayoutError but not their fields or error channel.
//   Tests assume:
//     `using WindowId = std::uint32_t;`
//     `struct PxPoint { float x; float y; };`
//     `struct WindowBounds { float x; float y; float w; float h; };`
//     `struct DragSession { std::uint32_t id; };` (opaque session handle)
//     `using DragTarget = WindowId;` (the owning window id is returned)
//     Errors surface as an exception of type `layout::LayoutError` whose value
//     compares equal to `layout::LayoutError::kNoTarget` (same throw
//     convention as solver_test.cpp's std::out_of_range cases and the T1
//     kUnknownWindow contract). If the coder spells the channel differently,
//     the contract reconciles the call site; the kNoTarget behavior stands.
//
// ASSUMPTION (fixture setup): windows are placed with `WindowId
//   createWindowAt(WindowBounds b)` in window.h, which creates a window whose
//   committed bounds equal `b` and returns its id. The `resizing` parameter of
//   resolveDropTarget carries the in-flight bounds of the window under resize;
//   hit-testing uses `resizing` for that window and committed bounds for all
//   others. If the coder names placement differently, the contract reconciles
//   the setup call only; the in-flight hit-test expectations do not change.
//
// ASSUMPTION (T1 staged context): `createWindow`, `applyWindowBounds`,
//   `kInvalidWindowId`, and `LayoutError::kUnknownWindow` are Batch 1 settled
//   and are not redeclared here.
#include <cstdint>
#include <cstdio>

#include "layout/solver.h"
#include "layout/window.h"

namespace {

layout::WindowBounds bounds(float x, float y, float w, float h) {
  layout::WindowBounds b;
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
  return b;
}

layout::PxPoint pt(float x, float y) {
  layout::PxPoint p;
  p.x = x;
  p.y = y;
  return p;
}

layout::DragSession session(std::uint32_t id) {
  layout::DragSession s;
  s.id = id;
  return s;
}

bool is_no_target_error(const layout::LayoutError& e) {
  return e == layout::LayoutError::kNoTarget;
}

}  // namespace

// AC-LAYOUT-038 probe 1/3: a point inside the resizing window's IN-FLIGHT
// bounds — but outside its last-committed bounds — resolves to the resizing
// window (proves in-flight bounds are hit-tested, not committed bounds).
// Window A committed at {0,0,400,400}, in-flight {0,0,700,400}: point
// (550,200) is outside committed A and inside committed nothing, yet must
// resolve to A via the in-flight bounds.
static bool check_in_flight_growth_captures_point() {
  layout::WindowId a = layout::createWindowAt(bounds(0.0f, 0.0f, 400.0f, 400.0f));
  layout::WindowId b =
      layout::createWindowAt(bounds(800.0f, 0.0f, 400.0f, 400.0f));
  (void)b;
  layout::WindowId got = layout::resolveDropTarget(
      session(1), pt(550.0f, 200.0f), bounds(0.0f, 0.0f, 700.0f, 400.0f));
  return got == a;
}

// AC-LAYOUT-038 probe 2/3: a point inside the untouched window still resolves
// to it while the other window is mid-resize.
static bool check_untouched_window_still_resolves() {
  layout::WindowId a = layout::createWindowAt(bounds(0.0f, 0.0f, 400.0f, 400.0f));
  layout::WindowId b =
      layout::createWindowAt(bounds(800.0f, 0.0f, 400.0f, 400.0f));
  (void)a;
  layout::WindowId got = layout::resolveDropTarget(
      session(2), pt(900.0f, 200.0f), bounds(0.0f, 0.0f, 700.0f, 400.0f));
  return got == b;
}

// AC-LAYOUT-038 probe 3/3: a point inside the resizing window's committed
// (and in-flight) bounds resolves to it mid-resize.
static bool check_point_in_both_committed_and_inflight_resolves() {
  layout::WindowId a = layout::createWindowAt(bounds(0.0f, 0.0f, 400.0f, 400.0f));
  layout::createWindowAt(bounds(800.0f, 0.0f, 400.0f, 400.0f));
  layout::WindowId got = layout::resolveDropTarget(
      session(3), pt(100.0f, 100.0f), bounds(0.0f, 0.0f, 700.0f, 400.0f));
  return got == a;
}

// Boundary: in-flight SHRINK releases the point — a point inside committed
// bounds but outside the shrunk in-flight bounds no longer resolves to the
// resizing window (falls through to kNoTarget when nothing else covers it).
// (ASSUMPTION: shrink is honored symmetrically with growth; reconciles if the
// coder settles a committed-bounds fallback rule.)
static bool check_in_flight_shrink_releases_point() {
  layout::createWindowAt(bounds(0.0f, 0.0f, 400.0f, 400.0f));
  layout::createWindowAt(bounds(800.0f, 0.0f, 400.0f, 400.0f));
  try {
    layout::resolveDropTarget(session(4), pt(350.0f, 200.0f),
                              bounds(0.0f, 0.0f, 200.0f, 400.0f));
  } catch (const layout::LayoutError& e) {
    return is_no_target_error(e);
  } catch (...) {
    return false;
  }
  return false;
}

// Error path: a point outside all windows (committed and in-flight) reports
// LayoutError::kNoTarget.
static bool check_outside_all_windows_reports_no_target() {
  layout::createWindowAt(bounds(0.0f, 0.0f, 400.0f, 400.0f));
  layout::createWindowAt(bounds(800.0f, 0.0f, 400.0f, 400.0f));
  try {
    layout::resolveDropTarget(session(5), pt(2000.0f, 2000.0f),
                              bounds(0.0f, 0.0f, 700.0f, 400.0f));
  } catch (const layout::LayoutError& e) {
    return is_no_target_error(e);
  } catch (...) {
    return false;
  }
  return false;
}

// Error path: a point in the gap between windows reports kNoTarget even
// mid-resize (no nearest-window fallback).
static bool check_gap_point_reports_no_target() {
  layout::createWindowAt(bounds(0.0f, 0.0f, 400.0f, 400.0f));
  layout::createWindowAt(bounds(800.0f, 0.0f, 400.0f, 400.0f));
  try {
    // In-flight A grows to x+w=700 < 800, so 750 stays uncovered.
    layout::resolveDropTarget(session(6), pt(750.0f, 200.0f),
                              bounds(0.0f, 0.0f, 700.0f, 400.0f));
  } catch (const layout::LayoutError& e) {
    return is_no_target_error(e);
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
  RUN_CASE(check_in_flight_growth_captures_point);
  RUN_CASE(check_untouched_window_still_resolves);
  RUN_CASE(check_point_in_both_committed_and_inflight_resolves);
  RUN_CASE(check_in_flight_shrink_releases_point);
  RUN_CASE(check_outside_all_windows_reports_no_target);
  RUN_CASE(check_gap_point_reports_no_target);
  if (failures > 0) {
    std::printf("drag_resize_test: %d failing\n", failures);
    return 1;
  }
  std::printf("drag_resize_test: all passing\n");
  return 0;
}
