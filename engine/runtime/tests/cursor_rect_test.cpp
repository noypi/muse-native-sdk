// T9 contract tests — FR-TEXT-013 / AC-TEXT-034 (TC-TEXT-034).
// Binds ONLY to the T9 `Interface:` line; never to implementation.
// Pure functions over InsetSync heights; no headers beyond the T9 seam.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected red state: `runtime/cursor_rect.h` does not exist yet, so this
// file must FAIL TO COMPILE. That is the recorded red state.
//
// ASSUMPTION (namespace): `CaretRect`/`VisibleViewport`/
// `caret_rect_for_offset`/`scroll_to_reveal` are referenced unqualified
// (global namespace, matching the GapBuffer/ALooperBridge precedent). If the
// coder nests them (e.g. `runtime::`), call sites reconcile at verify —
// expectations do not change.
// ASSUMPTION (single-line geometry): the Interface line pins clamping and
// the zero-viewport rule but not the layout model. The tests assert the
// most defensible single-line reading — origin caret at (0,0), horizontal
// advance `x = offset * char_advance`, `y = 0`, `h = line_height` — plus
// clamp-equivalence for out-of-range offsets. If the coder implements
// multi-line wrapping via `line_len`, the wrapping expectations reconcile
// at verify; the clamping, visibility, and no-throw expectations do not.
// ASSUMPTION (scroll sign): a caret below the visible bottom needs a
// positive scroll delta, a caret above the visible top a negative one, and
// a visible caret needs exactly 0.0f.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "runtime/cursor_rect.h"

// --- harness cases (match existing test style) -----------------------------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T9 cases (each maps to the Interface: line + AC-TEXT-034) --------------

// Happy path: the origin caret sits at (0, 0) with the line height.
static bool check_origin_caret_at_zero_zero() {
  CaretRect r = caret_rect_for_offset(0, 16.0f, 8.0f, 10);
  return r.x == 0.0f && r.y == 0.0f && r.h == 16.0f;
}

// Happy path: horizontal advance grows with the byte offset.
static bool check_caret_advances_with_offset() {
  CaretRect a = caret_rect_for_offset(2, 16.0f, 8.0f, 10);
  CaretRect b = caret_rect_for_offset(3, 16.0f, 8.0f, 10);
  if (a.x != 16.0f) return false;
  if (b.x != 24.0f) return false;
  return a.y == 0.0f && b.y == 0.0f && a.h == 16.0f && b.h == 16.0f;
}

// Happy path: last valid caret sits at the end of the line.
static bool check_last_caret_at_line_end() {
  CaretRect r = caret_rect_for_offset(10, 16.0f, 8.0f, 10);
  return r.x == 80.0f && r.y == 0.0f;
}

// Boundary: offset past the end clamps to the last caret position.
static bool check_offset_past_end_clamps_to_last_caret() {
  CaretRect at_end = caret_rect_for_offset(10, 16.0f, 8.0f, 10);
  CaretRect past_end = caret_rect_for_offset(1000000, 16.0f, 8.0f, 10);
  return past_end.x == at_end.x && past_end.y == at_end.y &&
         past_end.h == at_end.h;
}

// Boundary: different line heights flow into the caret height.
static bool check_line_height_flows_into_caret() {
  CaretRect r = caret_rect_for_offset(1, 24.0f, 10.0f, 10);
  return r.h == 24.0f && r.x == 10.0f;
}

// Happy path: a caret inside the viewport needs no scroll.
static bool check_visible_caret_needs_no_scroll() {
  CaretRect caret;
  caret.x = 8.0f;
  caret.y = 20.0f;
  caret.w = 2.0f;
  caret.h = 16.0f;
  VisibleViewport vp;
  vp.top = 0.0f;
  vp.height = 400.0f;
  return scroll_to_reveal(caret, vp, 0.0f) == 0.0f;
}

// Happy path: a caret hidden behind the shown keyboard scrolls positive.
static bool check_keyboard_covered_caret_scrolls_positive() {
  CaretRect caret;
  caret.x = 8.0f;
  caret.y = 390.0f;
  caret.w = 2.0f;
  caret.h = 16.0f;
  VisibleViewport vp;
  vp.top = 0.0f;
  vp.height = 400.0f;
  float delta = scroll_to_reveal(caret, vp, 120.0f);
  if (!(delta > 0.0f)) return false;
  // Applying the delta keeps the caret bottom above the keyboard edge.
  return caret.y + caret.h - delta <= vp.top + vp.height - 120.0f;
}

// Happy path: a caret above the viewport scrolls negative.
static bool check_caret_above_viewport_scrolls_negative() {
  CaretRect caret;
  caret.x = 0.0f;
  caret.y = 10.0f;
  caret.w = 2.0f;
  caret.h = 16.0f;
  VisibleViewport vp;
  vp.top = 100.0f;
  vp.height = 400.0f;
  float delta = scroll_to_reveal(caret, vp, 0.0f);
  return delta < 0.0f;
}

// Boundary: zero viewport height returns 0.0f (no scroll).
static bool check_zero_viewport_height_returns_zero() {
  CaretRect caret;
  caret.x = 0.0f;
  caret.y = 500.0f;
  caret.w = 2.0f;
  caret.h = 16.0f;
  VisibleViewport vp;
  vp.top = 0.0f;
  vp.height = 0.0f;
  return scroll_to_reveal(caret, vp, 120.0f) == 0.0f;
}

// Boundary: negative viewport height returns 0.0f (no scroll).
static bool check_negative_viewport_height_returns_zero() {
  CaretRect caret;
  caret.x = 0.0f;
  caret.y = 500.0f;
  caret.w = 2.0f;
  caret.h = 16.0f;
  VisibleViewport vp;
  vp.top = 0.0f;
  vp.height = -50.0f;
  return scroll_to_reveal(caret, vp, 120.0f) == 0.0f;
}

// Acceptance: under a scripted keyboard show/hide the caret never leaves
// the visible viewport after applying the scroll delta.
static bool check_show_hide_keeps_caret_visible() {
  CaretRect caret;
  caret.x = 8.0f;
  caret.y = 350.0f;
  caret.w = 2.0f;
  caret.h = 16.0f;
  VisibleViewport vp;
  vp.top = 0.0f;
  vp.height = 400.0f;
  const float heights[] = {0.0f, 60.0f, 120.0f, 200.0f, 120.0f, 0.0f};
  for (float kh : heights) {
    float delta = scroll_to_reveal(caret, vp, kh);
    float visible_top = vp.top;
    float visible_bottom = vp.top + vp.height - kh;
    if (visible_bottom <= visible_top) {
      // Keyboard covers the whole viewport: contract only promises no
      // scroll computation crash; skip the visibility check.
      continue;
    }
    float caret_top = caret.y - delta;
    float caret_bottom = caret.y + caret.h - delta;
    if (caret_top < visible_top) return false;
    if (caret_bottom > visible_bottom) return false;
  }
  return true;
}

// Error path: extreme inputs never throw.
static bool check_extreme_inputs_never_throw() {
  try {
    CaretRect r = caret_rect_for_offset(18446744073709551615ULL, 1.0e30f,
                                        1.0e30f, 18446744073709551615ULL);
    (void)r.x;
    (void)r.y;
    CaretRect caret;
    caret.x = 1.0e30f;
    caret.y = -1.0e30f;
    caret.w = 2.0f;
    caret.h = 1.0e30f;
    VisibleViewport vp;
    vp.top = -1.0e30f;
    vp.height = 1.0e30f;
    (void)scroll_to_reveal(caret, vp, 1.0e30f);
    VisibleViewport zero;
    zero.top = 0.0f;
    zero.height = 0.0f;
    if (scroll_to_reveal(caret, zero, -1.0e30f) != 0.0f) return false;
  } catch (...) {
    return false;
  }
  return true;
}

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_origin_caret_at_zero_zero);
  RUN_CASE(check_caret_advances_with_offset);
  RUN_CASE(check_last_caret_at_line_end);
  RUN_CASE(check_offset_past_end_clamps_to_last_caret);
  RUN_CASE(check_line_height_flows_into_caret);
  RUN_CASE(check_visible_caret_needs_no_scroll);
  RUN_CASE(check_keyboard_covered_caret_scrolls_positive);
  RUN_CASE(check_caret_above_viewport_scrolls_negative);
  RUN_CASE(check_zero_viewport_height_returns_zero);
  RUN_CASE(check_negative_viewport_height_returns_zero);
  RUN_CASE(check_show_hide_keeps_caret_visible);
  RUN_CASE(check_extreme_inputs_never_throw);
  if (failures > 0) {
    std::printf("cursor_rect_test: %d failing\n", failures);
    return 1;
  }
  std::printf("cursor_rect_test: all passing\n");
  return 0;
}
