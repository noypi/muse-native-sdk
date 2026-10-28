// T6 — FR-TEXT-015 / AC-TEXT-042 (+AC-TEXT-044 part): atomic undo/redo over
// text, spans, and inline objects (TC-TEXT-042).
// Hermetic harness: plain C++17, no test framework, no device. Cases drive
// only the plan Interface (reproduced verbatim below): one mixed
// text+span+image session entry undoes/redoes to byte-identical buffer, span,
// and inline-object state, building on the FR-TEXT-008 undo stack (consumed
// through the new interface only, never via runtime headers).
// Shape mirrors engine/text/ink/tests/handwriting_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdio>
#include <string>

#include "../edit_undo.h"

// Plan Interface lines (verbatim, plan.md Checklist T6):
// `UndoOp BeginAtomicEdit(BufferHandle& buf)`; `void CommitAtomicEdit(BufferHandle& buf, UndoOp op)`; `void Undo(BufferHandle& buf)` / `void Redo(BufferHandle& buf)`.
//
// ASSUMPTION (structural, for the coder — see contract T6.md): the undo
// stack lives in `muse::text::edit` as free functions with exactly the
// signatures above; `BufferHandle` is a default-constructible plain struct
// with public `std::string` members `text_data` (buffer bytes), `span_data`
// (span state bytes), and `object_data` (inline-object state bytes), plus
// `std::string text() const`, `spans() const`, `objects() const` fingerprint
// readers over those members. Edits between `BeginAtomicEdit` and
// `CommitAtomicEdit` are staged by appending to those members (standing in
// for the real text/span/image edit ops the coder ships inside the session);
// `Undo`/`Redo` restore all three members byte-identically (stack order, no
// op argument — unlike T4's op-addressed `Undo`). The contract pins only the
// plan signatures plus this plain-struct staging shape. The coder must not
// invent an alternate undo API.

namespace edit = muse::text::edit;

namespace {
// Test-local edit-op stand-ins: they append to the public staging members
// above, standing in for the real edit ops the coder ships; assertions read
// only the fingerprint readers, never these helpers.
inline void fake_insert_text(edit::BufferHandle& buf, const std::string& s) {
  buf.text_data += s;
}
inline void fake_apply_span(edit::BufferHandle& buf, const std::string& s) {
  buf.span_data += s;
}
inline void fake_insert_image(edit::BufferHandle& buf, const std::string& s) {
  buf.object_data += s;
}
}  // namespace

static bool check_mixed_session_undoes_byte_identical() {
  edit::BufferHandle buf{};
  edit::UndoOp op = edit::BeginAtomicEdit(buf);
  fake_insert_text(buf, "hello ");
  fake_apply_span(buf, "bold[0,5)");
  fake_insert_image(buf, "img:1");
  edit::CommitAtomicEdit(buf, op);
  if (buf.text() != "hello ") return false;
  edit::Undo(buf);
  return buf.text() == "" && buf.spans() == "" && buf.objects() == "";
}

static bool check_mixed_session_redoes_byte_identical() {
  edit::BufferHandle buf{};
  edit::UndoOp op = edit::BeginAtomicEdit(buf);
  fake_insert_text(buf, "hello ");
  fake_apply_span(buf, "bold[0,5)");
  fake_insert_image(buf, "img:1");
  edit::CommitAtomicEdit(buf, op);
  edit::Undo(buf);
  edit::Redo(buf);
  return buf.text() == "hello " && buf.spans() == "bold[0,5)" &&
         buf.objects() == "img:1";
}

static bool check_stacked_sessions_undo_in_reverse() {
  edit::BufferHandle buf{};
  edit::UndoOp op1 = edit::BeginAtomicEdit(buf);
  fake_insert_text(buf, "one ");
  edit::CommitAtomicEdit(buf, op1);
  edit::UndoOp op2 = edit::BeginAtomicEdit(buf);
  fake_insert_text(buf, "two");
  edit::CommitAtomicEdit(buf, op2);
  if (buf.text() != "one two") return false;
  edit::Undo(buf);
  if (buf.text() != "one ") return false;
  edit::Undo(buf);
  return buf.text() == "";
}

static bool check_redo_after_two_undos() {
  edit::BufferHandle buf{};
  edit::UndoOp op1 = edit::BeginAtomicEdit(buf);
  fake_insert_text(buf, "one ");
  edit::CommitAtomicEdit(buf, op1);
  edit::UndoOp op2 = edit::BeginAtomicEdit(buf);
  fake_insert_text(buf, "two");
  edit::CommitAtomicEdit(buf, op2);
  edit::Undo(buf);
  edit::Undo(buf);
  edit::Redo(buf);
  if (buf.text() != "one ") return false;
  edit::Redo(buf);
  return buf.text() == "one two";
}

static bool check_new_edit_clears_redo() {
  edit::BufferHandle buf{};
  edit::UndoOp op1 = edit::BeginAtomicEdit(buf);
  fake_insert_text(buf, "one");
  edit::CommitAtomicEdit(buf, op1);
  edit::Undo(buf);
  edit::UndoOp op2 = edit::BeginAtomicEdit(buf);
  fake_insert_text(buf, "two");
  edit::CommitAtomicEdit(buf, op2);
  edit::Redo(buf);  // no-op: the redo stack was cleared by the new edit
  return buf.text() == "two";
}

static bool check_undo_on_empty_stack_is_noop_never_throws() {
  edit::BufferHandle buf{};
  try {
    edit::Undo(buf);
    edit::Redo(buf);
  } catch (...) {
    return false;
  }
  return buf.text() == "";
}

static bool check_span_only_session_roundtrips() {
  edit::BufferHandle buf{};
  fake_insert_text(buf, "plain ");
  edit::UndoOp op = edit::BeginAtomicEdit(buf);
  fake_apply_span(buf, "italic[0,5)");
  fake_apply_span(buf, "bold[6,11)");
  edit::CommitAtomicEdit(buf, op);
  if (buf.spans() != "italic[0,5)bold[6,11)") return false;
  edit::Undo(buf);
  if (buf.spans() != "" || buf.text() != "plain ") return false;
  edit::Redo(buf);
  return buf.spans() == "italic[0,5)bold[6,11)";
}

static bool check_image_only_session_roundtrips() {
  edit::BufferHandle buf{};
  edit::UndoOp op = edit::BeginAtomicEdit(buf);
  fake_insert_image(buf, "img:1");
  fake_insert_image(buf, "img:2");
  edit::CommitAtomicEdit(buf, op);
  if (buf.objects() != "img:1img:2") return false;
  edit::Undo(buf);
  if (buf.objects() != "") return false;
  edit::Redo(buf);
  return buf.objects() == "img:1img:2";
}

static bool check_large_mixed_session_completes() {
  // Boundary: a 64 KiB mixed session completes and undoes byte-identically.
  edit::BufferHandle buf{};
  edit::UndoOp op = edit::BeginAtomicEdit(buf);
  const std::string chunk(4096, 'x');
  for (int i = 0; i < 16; ++i) fake_insert_text(buf, chunk);
  fake_apply_span(buf, std::string(1024, 's'));
  fake_insert_image(buf, std::string(1024, 'i'));
  edit::CommitAtomicEdit(buf, op);
  if (buf.text().size() != 65536) return false;
  edit::Undo(buf);
  return buf.text() == "" && buf.spans() == "" && buf.objects() == "";
}

static bool check_signature_shapes_match_plan_interface() {
  edit::BufferHandle buf{};
  edit::UndoOp op = edit::BeginAtomicEdit(buf);
  edit::CommitAtomicEdit(buf, op);
  edit::Undo(buf);
  edit::Redo(buf);
  return buf.text() == "";
}

int main() {
  static struct {
    const char* name;
    bool (*fn)();
  } cases[] = {
      {"check_mixed_session_undoes_byte_identical",
       check_mixed_session_undoes_byte_identical},
      {"check_mixed_session_redoes_byte_identical",
       check_mixed_session_redoes_byte_identical},
      {"check_stacked_sessions_undo_in_reverse", check_stacked_sessions_undo_in_reverse},
      {"check_redo_after_two_undos", check_redo_after_two_undos},
      {"check_new_edit_clears_redo", check_new_edit_clears_redo},
      {"check_undo_on_empty_stack_is_noop_never_throws",
       check_undo_on_empty_stack_is_noop_never_throws},
      {"check_span_only_session_roundtrips", check_span_only_session_roundtrips},
      {"check_image_only_session_roundtrips", check_image_only_session_roundtrips},
      {"check_large_mixed_session_completes", check_large_mixed_session_completes},
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
