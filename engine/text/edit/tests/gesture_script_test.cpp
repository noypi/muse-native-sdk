// T8 — FR-TEXT-015 / AC-TEXT-044: scripted end-to-end gesture + rich-edit
// session test (TC-TEXT-044). TEST-ONLY driver: no new production API.
// Hermetic harness: plain C++17, no test framework, no device, no clipboard.
// This file defines its own `GestureScript` / `TestReport` script types and
// drives only the T5/T6/T7 surfaces (selection_controller.h, edit_undo.h,
// plain_exporter.h); the services-owned clipboard boundary stays mocked —
// assertions read `ExportPlain`/`ExportHtml` outputs, never a live service.
// Shape mirrors engine/text/ink/tests/handwriting_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
//
// Plan Interface lines (verbatim, plan.md Checklist T8):
// `TestReport RunGestureScript(const GestureScript& script)` → `{selectionRanges, undoRedoByteIdentical: bool, htmlPaste: std::string}`. `none — test-only driver over T5/T6/T7 surfaces, no new production API`.
//
// Production files for T8: NONE — plan Checklist T8 `Files:` names only
// `engine/text/edit/tests/gesture_script_test.cpp` (new); confirmed against
// plan.md, no production header/source is named for T8. `RunGestureScript`
// and `GestureScript`/`TestReport` are defined in THIS test file, not in
// production. The coder ships no T8 production code.
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "../selection_controller.h"
#include "../edit_undo.h"
#include "../plain_exporter.h"

namespace edit = muse::text::edit;

// Test-only script driver (NOT production): encodes AC-TEXT-044 verbatim —
// double-tap word, triple-tap paragraph, drag-extend, magnifier place-caret →
// expected ranges; mixed edit → byte-identical undo/redo; image selection →
// HTML paste.
struct GestureScript {
  edit::ByteOffset double_tap_caret{2};   // inside "hello" → word [0,5)
  edit::ByteOffset triple_tap_caret{2};   // inside para0 → [0,12)
  edit::ByteOffset drag_anchor{3};
  std::size_t drag_end{20};
  edit::Point magnifier_point{4, 0};
  std::string edit_text{"hello "};
  std::string edit_span{"bold[0,5)"};
  std::string edit_image{"img:1"};
  std::string rich_text{"see this"};
  std::string rich_image{"img:1"};
};

struct TestReport {
  std::vector<std::pair<std::size_t, std::size_t>> selection_ranges;
  bool undo_redo_byte_identical{false};
  std::string html_paste;
};

static TestReport RunGestureScript(const GestureScript& script) {
  TestReport report{};
  // Clause 1 — gesture ranges via the T5 surface.
  edit::SelectionRange word = edit::ResolveTap(
      edit::TapKind::DOUBLE_WORD, script.double_tap_caret, edit::DragExtent{});
  report.selection_ranges.push_back({word.anchor.value, word.focus.value});
  edit::SelectionRange para = edit::ResolveTap(edit::TapKind::TRIPLE_PARAGRAPH,
                                               script.triple_tap_caret,
                                               edit::DragExtent{});
  report.selection_ranges.push_back({para.anchor.value, para.focus.value});
  edit::DragExtent drag{};
  drag.end = script.drag_end;
  edit::SelectionRange ext = edit::ResolveTap(
      edit::TapKind::SINGLE, script.drag_anchor, drag);
  report.selection_ranges.push_back({ext.anchor.value, ext.focus.value});
  edit::CaretRect placed = edit::PlaceWithMagnifier(script.magnifier_point);
  report.selection_ranges.push_back({placed.caret.value, placed.caret.value});
  // Clause 2 — mixed edit undo/redo via the T6 surface.
  edit::BufferHandle buf{};
  edit::UndoOp op = edit::BeginAtomicEdit(buf);
  buf.text_data += script.edit_text;
  buf.span_data += script.edit_span;
  buf.object_data += script.edit_image;
  edit::CommitAtomicEdit(buf, op);
  const std::string t = buf.text(), s = buf.spans(), o = buf.objects();
  edit::Undo(buf);
  const bool undone = buf.text() == "" && buf.spans() == "" && buf.objects() == "";
  edit::Redo(buf);
  report.undo_redo_byte_identical =
      undone && buf.text() == t && buf.spans() == s && buf.objects() == o;
  // Clause 3 — image selection → HTML paste via the T7 surface.
  edit::RichSelection sel{};
  sel.text = script.rich_text;
  sel.images = script.rich_image;
  report.html_paste = edit::ExportHtml(sel);
  return report;
}

static bool check_clause1_word_range() {
  TestReport r = RunGestureScript(GestureScript{});
  return r.selection_ranges.size() == 4 && r.selection_ranges[0].first == 0 &&
         r.selection_ranges[0].second == 5;
}

static bool check_clause1_paragraph_range() {
  TestReport r = RunGestureScript(GestureScript{});
  return r.selection_ranges[1].first == 0 && r.selection_ranges[1].second == 12;
}

static bool check_clause1_drag_extend_range() {
  TestReport r = RunGestureScript(GestureScript{});
  return r.selection_ranges[2].first == 3 && r.selection_ranges[2].second == 20;
}

static bool check_clause1_magnifier_caret() {
  TestReport r = RunGestureScript(GestureScript{});
  return r.selection_ranges[3].first == 4 && r.selection_ranges[3].second == 4;
}

static bool check_clause2_undo_redo_byte_identical() {
  TestReport r = RunGestureScript(GestureScript{});
  return r.undo_redo_byte_identical;
}

static bool check_clause3_html_paste_keeps_image() {
  TestReport r = RunGestureScript(GestureScript{});
  return r.html_paste.find("see this") != std::string::npos &&
         r.html_paste.find("<img") != std::string::npos;
}

static bool check_full_script_all_three_clauses_one_run() {
  // AC-TEXT-044 verbatim: all three clauses pass in ONE script run.
  TestReport r = RunGestureScript(GestureScript{});
  const bool clause1 = r.selection_ranges.size() == 4 &&
                       r.selection_ranges[0] == std::make_pair(std::size_t{0}, std::size_t{5}) &&
                       r.selection_ranges[1] == std::make_pair(std::size_t{0}, std::size_t{12}) &&
                       r.selection_ranges[2] == std::make_pair(std::size_t{3}, std::size_t{20});
  const bool clause2 = r.undo_redo_byte_identical;
  const bool clause3 = r.html_paste.find("<img") != std::string::npos;
  return clause1 && clause2 && clause3;
}

static bool check_empty_script_never_throws() {
  // Boundary: empty script content still produces a well-formed report.
  GestureScript script{};
  script.edit_text = "";
  script.edit_span = "";
  script.edit_image = "";
  script.rich_text = "";
  script.rich_image = "";
  try {
    TestReport r = RunGestureScript(script);
    return r.selection_ranges.size() == 4 && r.undo_redo_byte_identical;
  } catch (...) {
    return false;
  }
}

int main() {
  static struct {
    const char* name;
    bool (*fn)();
  } cases[] = {
      {"check_clause1_word_range", check_clause1_word_range},
      {"check_clause1_paragraph_range", check_clause1_paragraph_range},
      {"check_clause1_drag_extend_range", check_clause1_drag_extend_range},
      {"check_clause1_magnifier_caret", check_clause1_magnifier_caret},
      {"check_clause2_undo_redo_byte_identical",
       check_clause2_undo_redo_byte_identical},
      {"check_clause3_html_paste_keeps_image", check_clause3_html_paste_keeps_image},
      {"check_full_script_all_three_clauses_one_run",
       check_full_script_all_three_clauses_one_run},
      {"check_empty_script_never_throws", check_empty_script_never_throws},
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
