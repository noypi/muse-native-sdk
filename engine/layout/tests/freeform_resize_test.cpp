// Batch 1 T1 contract tests — FR-LAYOUT-013 AC-LAYOUT-036 (TC-LAYOUT-036).
// Hermetic: C++17 + layout/window.h only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T1 `Interface:` line (verbatim in contract T1.md):
//   `WindowResizeResult applyWindowBounds(WindowId id, PxSize size, ResizeOpts opts) -> { SolveStats stats; ScrollAnchor preservedScroll; FocusId preservedFocus; SelectionRange preservedSelection; bool recreated }`
//   — `recreated` must be false; error: `LayoutError::kUnknownWindow` for bad id.
//
// ASSUMPTION (type shapes): the plan names WindowId/PxSize/ResizeOpts/
//   WindowResizeResult/ScrollAnchor/FocusId/SelectionRange/SolveStats/
//   LayoutError but not their fields or error channel. Tests assume:
//     `using WindowId = std::uint32_t;`
//     `struct PxSize { float width; float height; };`
//     `struct ResizeOpts { };` (default-constructible; no fields needed)
//     `struct WindowResizeResult { SolveStats stats; ScrollAnchor preservedScroll;
//       FocusId preservedFocus; SelectionRange preservedSelection; bool recreated; };`
//     ScrollAnchor/FocusId/SelectionRange are comparable (operator==) and
//     default-constructible; SolveStats is default-constructible.
//     Errors surface as an exception of type `layout::LayoutError` whose value
//     compares equal to `layout::LayoutError::kUnknownWindow` (same throw
//     convention as solver_test.cpp's std::out_of_range cases). If the coder
//     spells the error channel differently (e.g. Status return), the contract
//     reconciles the call site; the kUnknownWindow behavior stands.
//
// ASSUMPTION (fixture setup): the plan does not name a window-creation entry
//   point. Tests assume `WindowId createWindow(PxSize size)` in window.h that
//   returns a valid id for a fresh window with a focused field, a scroll
//   anchor, and a selection (so preservation is observable). If the coder
//   names creation differently, the contract reconciles the setup call only;
//   preservation expectations do not change.
//
// ASSUMPTION (observation): "preserved in place" is observed through the
//   result fields (preservedScroll/preservedFocus/preservedSelection equal
//   their pre-resize values) plus `recreated == false` on every step.
#include <cstdint>
#include <cstdio>

#include "layout/window.h"

namespace {

// Phone -> desktop continuous sweep widths (TC-039 style subset for T1).
const float kWidths[] = {360.0f, 500.0f, 700.0f, 900.0f, 1200.0f};

layout::PxSize px(float w, float h) {
  layout::PxSize s;
  s.width = w;
  s.height = h;
  return s;
}

bool is_unknown_window_error(const layout::LayoutError& e) {
  return e == layout::LayoutError::kUnknownWindow;
}

}  // namespace

// AC-LAYOUT-036: each continuous-resize step reflows in place — recreated is
// false on every step from phone to desktop width.
static bool check_continuous_resize_never_recreates() {
  layout::WindowId id = layout::createWindow(px(360.0f, 640.0f));
  layout::ResizeOpts opts;
  for (float w : kWidths) {
    layout::WindowResizeResult r = layout::applyWindowBounds(id, px(w, 700.0f), opts);
    if (r.recreated) return false;
  }
  return true;
}

// AC-LAYOUT-036: scroll anchor is preserved across a resize step.
static bool check_resize_preserves_scroll_anchor() {
  layout::WindowId id = layout::createWindow(px(360.0f, 640.0f));
  layout::ResizeOpts opts;
  layout::WindowResizeResult before = layout::applyWindowBounds(id, px(360.0f, 640.0f), opts);
  layout::WindowResizeResult after = layout::applyWindowBounds(id, px(700.0f, 640.0f), opts);
  if (after.recreated) return false;
  return after.preservedScroll == before.preservedScroll;
}

// AC-LAYOUT-036: focused field stays focused across a resize step.
static bool check_resize_preserves_focus() {
  layout::WindowId id = layout::createWindow(px(360.0f, 640.0f));
  layout::ResizeOpts opts;
  layout::WindowResizeResult before = layout::applyWindowBounds(id, px(360.0f, 640.0f), opts);
  layout::WindowResizeResult after = layout::applyWindowBounds(id, px(1200.0f, 800.0f), opts);
  if (after.recreated) return false;
  return after.preservedFocus == before.preservedFocus;
}

// AC-LAYOUT-036: text selection is preserved across a resize step.
static bool check_resize_preserves_selection() {
  layout::WindowId id = layout::createWindow(px(360.0f, 640.0f));
  layout::ResizeOpts opts;
  layout::WindowResizeResult before = layout::applyWindowBounds(id, px(360.0f, 640.0f), opts);
  layout::WindowResizeResult after = layout::applyWindowBounds(id, px(900.0f, 700.0f), opts);
  if (after.recreated) return false;
  return after.preservedSelection == before.preservedSelection;
}

// Boundary: zero-size bounds still resolve in place without recreation.
// (ASSUMPTION: zero size is accepted and preserves state; if the coder treats
// it as invalid per a settled rule, this case reconciles to that rule.)
static bool check_zero_size_resize_stays_in_place() {
  layout::WindowId id = layout::createWindow(px(360.0f, 640.0f));
  layout::ResizeOpts opts;
  layout::WindowResizeResult r = layout::applyWindowBounds(id, px(0.0f, 0.0f), opts);
  return !r.recreated;
}

// Error path: unknown window id reports LayoutError::kUnknownWindow.
static bool check_unknown_window_reports_error() {
  layout::ResizeOpts opts;
  try {
    layout::applyWindowBounds(static_cast<layout::WindowId>(9999), px(700.0f, 640.0f), opts);
  } catch (const layout::LayoutError& e) {
    return is_unknown_window_error(e);
  } catch (...) {
    return false;
  }
  return false;
}

// Error path: kInvalidWindowId-equivalent (the invalid sentinel) also reports
// kUnknownWindow and never recreates. (ASSUMPTION: the invalid id constant is
// `layout::kInvalidWindowId`; reconciles if named differently.)
static bool check_invalid_window_id_reports_error() {
  layout::ResizeOpts opts;
  try {
    layout::applyWindowBounds(layout::kInvalidWindowId, px(700.0f, 640.0f), opts);
  } catch (const layout::LayoutError& e) {
    return is_unknown_window_error(e);
  } catch (...) {
    return false;
  }
  return false;
}

// AC-LAYOUT-036: non-default state set via the public setters survives a
// resize with exact values — focus field 42, scroll anchor (node 7,
// offset 12.5), selection (5, 9).
static bool check_non_default_state_survives_resize() {
  layout::WindowId id = layout::createWindow(px(360.0f, 640.0f));
  layout::FocusId focus;
  focus.field = 42u;
  layout::ScrollAnchor scroll;
  scroll.node = 7u;
  scroll.offset = 12.5f;
  layout::SelectionRange selection;
  selection.start = 5u;
  selection.end = 9u;
  layout::setWindowFocus(id, focus);
  layout::setWindowScroll(id, scroll);
  layout::setWindowSelection(id, selection);
  layout::ResizeOpts opts;
  layout::WindowResizeResult r =
      layout::applyWindowBounds(id, px(900.0f, 700.0f), opts);
  if (r.recreated) return false;
  return r.preservedFocus == focus && r.preservedScroll == scroll &&
         r.preservedSelection == selection;
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
  RUN_CASE(check_continuous_resize_never_recreates);
  RUN_CASE(check_resize_preserves_scroll_anchor);
  RUN_CASE(check_resize_preserves_focus);
  RUN_CASE(check_resize_preserves_selection);
  RUN_CASE(check_non_default_state_survives_resize);
  RUN_CASE(check_zero_size_resize_stays_in_place);
  RUN_CASE(check_unknown_window_reports_error);
  RUN_CASE(check_invalid_window_id_reports_error);
  if (failures > 0) {
    std::printf("freeform_resize_test: %d failing\n", failures);
    return 1;
  }
  std::printf("freeform_resize_test: all passing\n");
  return 0;
}
