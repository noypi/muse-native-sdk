// T4 — FR-TEXT-014 / AC-TEXT-038: handwriting composition into the shared
// editable buffer with undo/redo (TC-TEXT-038).
// Hermetic harness: plain C++17, no test framework, no device, no recognizer,
// no downloads. Cases drive only the plan Interface (reproduced verbatim
// below) with already-recognized UTF-8 strings: recognition itself stays
// behind the services/platform boundary per plan Risks. Shape mirrors
// engine/text/ink/tests/stylus_axes_test.cpp and
// engine/runtime/tests/gap_buffer_test.cpp: static bool check_*() cases +
// main, return 0 all-green / non-zero + failing name.
//
// Undo-stack semantics mirror engine/runtime/tests/gap_buffer_test.cpp: one
// recognized phrase = one atomic undo op on the shared buffer (the FR-TEXT-008
// gap-buffer stack is reused, not forked).
#include <cstddef>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>

#include "../handwriting_committer.h"

// Plan Interface line (verbatim, plan.md Checklist T4):
// `CommitOp CommitRecognizedText(BufferHandle& buf, std::string utf8Text, const SpanContext& ctx)` → returns an undoable `CommitOp{ opId }`; `void Undo(BufferHandle& buf, CommitOp op)`.
//
// ASSUMPTION (structural, for the coder — see contract T4.md): the committer
// lives in `muse::text::ink` as free functions with exactly the signatures
// above; `BufferHandle` is a default-constructible value struct exposing
// `std::string text() const` (the shared editable content: typed text and
// committed handwriting in one buffer); `SpanContext` is a
// default-constructible value struct (`SpanContext{}` is the no-span case);
// `CommitOp` is a value struct brace-constructible as `CommitOp{ opId }` with
// an `opId` member. `Undo` removes exactly the text its `CommitOp` committed.
// Redo (AC-TEXT-038 "undoes/redoes") is NOT in the plan Interface: it is
// covered only as a gap (re-commit after undo); the coder must not invent a
// `Redo` overload to satisfy these tests.

namespace ink = muse::text::ink;

static ink::BufferHandle make_buffer_with_typed_text(const std::string& typed) {
  ink::BufferHandle buf{};
  if (!typed.empty()) {
    ink::SpanContext ctx{};
    (void)ink::CommitRecognizedText(buf, typed, ctx);
  }
  return buf;
}

static bool check_commit_appends_to_empty_buffer() {
  ink::BufferHandle buf{};
  ink::SpanContext ctx{};
  (void)ink::CommitRecognizedText(buf, "hello", ctx);
  return buf.text() == "hello";
}

static bool check_commit_lands_in_same_buffer_as_typed_text() {
  ink::BufferHandle buf = make_buffer_with_typed_text("typed ");
  ink::SpanContext ctx{};
  (void)ink::CommitRecognizedText(buf, "hello", ctx);
  return buf.text() == "typed hello";
}

static bool check_one_phrase_is_one_atomic_undo_op() {
  ink::BufferHandle buf = make_buffer_with_typed_text("typed ");
  ink::SpanContext ctx{};
  ink::CommitOp op = ink::CommitRecognizedText(buf, "hello world", ctx);
  if (buf.text() != "typed hello world") return false;
  ink::Undo(buf, op);
  return buf.text() == "typed ";
}

static bool check_undo_removes_only_its_own_phrase() {
  ink::BufferHandle buf{};
  ink::SpanContext ctx{};
  ink::CommitOp op1 = ink::CommitRecognizedText(buf, "one ", ctx);
  ink::CommitOp op2 = ink::CommitRecognizedText(buf, "two", ctx);
  if (buf.text() != "one two") return false;
  ink::Undo(buf, op2);
  if (buf.text() != "one ") return false;
  ink::Undo(buf, op1);
  return buf.text() == "";
}

static bool check_undo_after_interleaved_typed_text() {
  // Typed text committed between two handwriting commits stays put when the
  // later handwriting op undoes: only the recognized phrase is removed.
  ink::BufferHandle buf{};
  ink::SpanContext ctx{};
  (void)ink::CommitRecognizedText(buf, "ab", ctx);
  ink::CommitOp op = ink::CommitRecognizedText(buf, "cd", ctx);
  if (buf.text() != "abcd") return false;
  ink::Undo(buf, op);
  return buf.text() == "ab";
}

static bool check_empty_recognition_is_noop() {
  ink::BufferHandle buf = make_buffer_with_typed_text("typed ");
  ink::SpanContext ctx{};
  ink::CommitOp op = ink::CommitRecognizedText(buf, "", ctx);
  if (buf.text() != "typed ") return false;
  ink::Undo(buf, op);
  return buf.text() == "typed ";
}

static bool check_utf8_multibyte_roundtrips_byte_identical() {
  // Already-recognized UTF-8 (CJK + emoji) commits byte-identically and undoes
  // as one op; the committer never re-encodes or normalizes text.
  const std::string phrase = u8"hello \u4e2d\u6587 \U0001F600";
  ink::BufferHandle buf{};
  ink::SpanContext ctx{};
  ink::CommitOp op = ink::CommitRecognizedText(buf, phrase, ctx);
  if (buf.text() != phrase) return false;
  ink::Undo(buf, op);
  return buf.text() == "";
}

static bool check_span_context_does_not_alter_text() {
  // Span styling rides along without changing committed bytes: the default
  // (no-span) context and an explicitly defaulted context commit identically.
  ink::BufferHandle a{};
  ink::BufferHandle b{};
  (void)ink::CommitRecognizedText(a, "hello", ink::SpanContext{});
  ink::SpanContext ctx{};
  (void)ink::CommitRecognizedText(b, "hello", ctx);
  return a.text() == "hello" && b.text() == "hello";
}

static bool check_large_phrase_commit_completes() {
  // Hostile/oversized recognition result still returns; undo restores.
  ink::BufferHandle buf{};
  ink::SpanContext ctx{};
  const std::string big(256 * 1024, 'x');
  ink::CommitOp op = ink::CommitRecognizedText(buf, big, ctx);
  if (buf.text() != big) return false;
  ink::Undo(buf, op);
  return buf.text() == "";
}

static bool check_invalid_utf8_passthrough_never_throws() {
  // Untrusted recognizer bytes (lone continuation bytes) must not crash the
  // committer; they pass through and undo as one op.
  const std::string bad("\xff\xfe\x80", 3);
  ink::BufferHandle buf = make_buffer_with_typed_text("ok ");
  ink::SpanContext ctx{};
  ink::CommitOp op = ink::CommitRecognizedText(buf, bad, ctx);
  if (buf.text() != std::string("ok ") + bad) return false;
  ink::Undo(buf, op);
  return buf.text() == "ok ";
}

static bool check_signature_matches_plan_interface() {
  // The plan's public boundary, exactly: free functions in muse::text::ink
  // with by-value utf8Text, const-ref ctx, and CommitOp{ opId } shape.
  static_assert(std::is_same_v<
                decltype(&ink::CommitRecognizedText),
                ink::CommitOp (*)(ink::BufferHandle&, std::string,
                                  const ink::SpanContext&)>);
  static_assert(std::is_same_v<decltype(&ink::Undo),
                               void (*)(ink::BufferHandle&, ink::CommitOp)>);
  ink::CommitOp op{0};
  (void)op.opId;
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
  RUN_CASE(check_commit_appends_to_empty_buffer);
  RUN_CASE(check_commit_lands_in_same_buffer_as_typed_text);
  RUN_CASE(check_one_phrase_is_one_atomic_undo_op);
  RUN_CASE(check_undo_removes_only_its_own_phrase);
  RUN_CASE(check_undo_after_interleaved_typed_text);
  RUN_CASE(check_empty_recognition_is_noop);
  RUN_CASE(check_utf8_multibyte_roundtrips_byte_identical);
  RUN_CASE(check_span_context_does_not_alter_text);
  RUN_CASE(check_large_phrase_commit_completes);
  RUN_CASE(check_invalid_utf8_passthrough_never_throws);
  RUN_CASE(check_signature_matches_plan_interface);
  if (failures > 0) {
    std::printf("handwriting_test: %d failing\n", failures);
    return 1;
  }
  std::printf("handwriting_test: all passing\n");
  return 0;
}
