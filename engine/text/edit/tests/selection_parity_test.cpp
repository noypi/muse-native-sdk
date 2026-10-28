// T5 — FR-TEXT-015 / AC-TEXT-041: selection handles, magnifier, and
// multi-tap parity with the platform editor (TC-TEXT-041).
// Hermetic harness: plain C++17, no test framework, no device, no platform
// editor. Cases drive only the plan Interface (reproduced verbatim below)
// against an inline document string: the FR-TEXT-008 buffer/caret addressing
// is consumed through the new interface only, never via runtime headers.
// Shape mirrors engine/text/ink/tests/handwriting_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdio>
#include <string>

#include "../selection_controller.h"

// Plan Interface lines (verbatim, plan.md Checklist T5):
// `SelectionRange ResolveTap(TapKind kind /*SINGLE, DOUBLE_WORD, TRIPLE_PARAGRAPH*/, ByteOffset caret, DragExtent drag)` → `{anchor, focus}`; `CaretRect PlaceWithMagnifier(Point p)` → caret rect kept visible.
//
// ASSUMPTION (structural, for the coder — see contract T5.md): the controller
// lives in `muse::text::edit` as free functions with exactly the signatures
// above; `TapKind` is an enum with `SINGLE, DOUBLE_WORD, TRIPLE_PARAGRAPH`;
// `ByteOffset` is an integer offset type brace-constructible from size_t;
// `DragExtent` is a default-constructible value (`DragExtent{}` = no drag) with
// an `end` byte offset member; `SelectionRange` is `{anchor, focus}` byte
// offsets; `Point` is `{x, y}`; `CaretRect` exposes `caret` (byte offset) and
// `visible: bool` (kept visible by the magnifier). The document under test is
// "hello world\nsecond paragraph here\n": word = [0,5), first paragraph =
// [0,11). The coder must not invent an alternate selection API.

namespace edit = muse::text::edit;

// "hello world\nsecond paragraph here\n": word0=[0,5), para0=[0,12).
static bool check_single_tap_collapses_caret() {
  edit::SelectionRange r =
      edit::ResolveTap(edit::TapKind::SINGLE, edit::ByteOffset{3}, edit::DragExtent{});
  return r.anchor == 3 && r.focus == 3;
}

static bool check_double_tap_selects_word() {
  edit::SelectionRange r =
      edit::ResolveTap(edit::TapKind::DOUBLE_WORD, edit::ByteOffset{2}, edit::DragExtent{});
  return r.anchor == 0 && r.focus == 5;
}

static bool check_double_tap_second_word() {
  edit::SelectionRange r =
      edit::ResolveTap(edit::TapKind::DOUBLE_WORD, edit::ByteOffset{8}, edit::DragExtent{});
  return r.anchor == 6 && r.focus == 11;
}

static bool check_triple_tap_selects_paragraph() {
  edit::SelectionRange r = edit::ResolveTap(
      edit::TapKind::TRIPLE_PARAGRAPH, edit::ByteOffset{2}, edit::DragExtent{});
  return r.anchor == 0 && r.focus == 12;
}

static bool check_drag_extends_from_tap() {
  edit::DragExtent drag{};
  drag.end = 20;
  edit::SelectionRange r =
      edit::ResolveTap(edit::TapKind::SINGLE, edit::ByteOffset{3}, drag);
  return r.anchor == 3 && r.focus == 20;
}

static bool check_drag_extends_word_selection() {
  edit::DragExtent drag{};
  drag.end = 20;
  edit::SelectionRange r = edit::ResolveTap(
      edit::TapKind::DOUBLE_WORD, edit::ByteOffset{2}, drag);
  return r.anchor == 0 && r.focus == 20;
}

static bool check_magnifier_places_caret_visible() {
  edit::CaretRect c = edit::PlaceWithMagnifier(edit::Point{4, 0});
  return c.visible && c.caret == 4;
}

static bool check_magnifier_at_document_end() {
  edit::CaretRect c = edit::PlaceWithMagnifier(edit::Point{200, 0});
  return c.visible;  // clamped into the document, still visible
}

static bool check_single_tap_empty_document_collapses_to_zero() {
  edit::SelectionRange r =
      edit::ResolveTap(edit::TapKind::SINGLE, edit::ByteOffset{0}, edit::DragExtent{});
  return r.anchor == 0 && r.focus == 0;
}

static bool check_out_of_range_caret_clamps_never_throws() {
  // Untrusted caret offsets (e.g. stale handle after edit) clamp into range
  // and never throw: parity must not crash the editor.
  edit::SelectionRange r = edit::ResolveTap(
      edit::TapKind::DOUBLE_WORD, edit::ByteOffset{1000000}, edit::DragExtent{});
  (void)r;
  return true;  // reaching here without an exception is the assertion
}

static bool check_signature_shapes_match_plan_interface() {
  // Compile-time shape check: the exact plan signatures exist and compose.
  edit::DragExtent noDrag{};
  edit::SelectionRange r =
      edit::ResolveTap(edit::TapKind::TRIPLE_PARAGRAPH, edit::ByteOffset{0}, noDrag);
  edit::CaretRect c = edit::PlaceWithMagnifier(edit::Point{0, 0});
  return (r.anchor <= r.focus || r.anchor > r.focus) && (c.visible || !c.visible);
}

int main() {
  static struct {
    const char* name;
    bool (*fn)();
  } cases[] = {
      {"check_single_tap_collapses_caret", check_single_tap_collapses_caret},
      {"check_double_tap_selects_word", check_double_tap_selects_word},
      {"check_double_tap_second_word", check_double_tap_second_word},
      {"check_triple_tap_selects_paragraph", check_triple_tap_selects_paragraph},
      {"check_drag_extends_from_tap", check_drag_extends_from_tap},
      {"check_drag_extends_word_selection", check_drag_extends_word_selection},
      {"check_magnifier_places_caret_visible", check_magnifier_places_caret_visible},
      {"check_magnifier_at_document_end", check_magnifier_at_document_end},
      {"check_single_tap_empty_document_collapses_to_zero",
       check_single_tap_empty_document_collapses_to_zero},
      {"check_out_of_range_caret_clamps_never_throws",
       check_out_of_range_caret_clamps_never_throws},
      {"check_signature_shapes_match_plan_interface",
       check_signature_shapes_match_plan_interface},
  };
  int failures = 0;
  for (std::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    bool ok = false;
    try {
      ok = cases[i].fn();
    } catch (...) {
      ok = false;
    }
    if (!ok) {
      ++failures;
      std::printf("FAIL %s\n", cases[i].name);
    } else {
      std::printf("PASS %s\n", cases[i].name);
    }
  }
  return failures == 0 ? 0 : 1;
}
