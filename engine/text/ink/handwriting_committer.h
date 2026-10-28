#pragma once

// FR-TEXT-014 / AC-TEXT-038: handwriting commit into the shared editable
// buffer with atomic undo (TC-TEXT-038).
// Owns the commit/undo ops over its own buffer handle; the FR-TEXT-008
// gap-buffer/undo stack is reused conceptually (one phrase = one atomic
// undo op, stacked undos apply in reverse) without including runtime/
// headers. Recognition itself stays behind the services/platform boundary:
// this unit takes already-recognized UTF-8 and appends it verbatim.
// Never throws (bad_alloc aside); never re-encodes or normalizes bytes.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace muse::text::ink {

struct BufferHandle;
struct SpanContext;
struct CommitOp;
CommitOp CommitRecognizedText(BufferHandle& buf, std::string utf8Text,
                              const SpanContext& ctx);
void Undo(BufferHandle& buf, CommitOp op);

// Shared editable content: typed text and committed handwriting in one
// buffer. Default-constructible; `text()` returns the full content.
struct BufferHandle {
  // Returns the full buffer content as bytes (UTF-8 preserved verbatim).
  std::string text() const { return content_; }

 private:
  friend CommitOp CommitRecognizedText(BufferHandle& buf, std::string utf8Text,
                                      const SpanContext& ctx);
  friend void Undo(BufferHandle& buf, CommitOp op);

  struct OpRecord {
    std::uint64_t id = 0;
    std::size_t offset = 0;
    std::size_t length = 0;
  };

  std::string content_;
  std::vector<OpRecord> ops_;
  std::uint64_t nextId_ = 1;
};

// Span styling context riding along with a commit. Currently the no-span
// case only (`SpanContext{}`); commits never let it alter committed bytes.
struct SpanContext {};

// Undoable handle for one recognized-phrase commit. Brace-constructible
// as `CommitOp{ opId }`; `opId` identifies the phrase for `Undo`.
struct CommitOp {
  std::uint64_t opId = 0;
};

}  // namespace muse::text::ink
