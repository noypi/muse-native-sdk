// FR-TEXT-014 / AC-TEXT-038: handwriting commit implementation.
// One recognized phrase = one atomic undo op appended at the buffer end;
// Undo erases exactly that phrase's byte range (gap-buffer convention:
// stacked undos apply in reverse). Empty commits and unknown ops are
// no-ops. Bytes pass through verbatim — no re-encoding, no normalization,
// no validation throws. `SpanContext` is intentionally ignored.

#include "handwriting_committer.h"

#include <utility>

namespace muse::text::ink {

CommitOp CommitRecognizedText(BufferHandle& buf, std::string utf8Text,
                              const SpanContext& /*ctx*/) {
  const std::uint64_t id = buf.nextId_++;
  if (id == 0) {
    // nextId_ wrapped past zero (practically unreachable): skip zero so a
    // default CommitOp{0} never aliases a live op.
    return CommitRecognizedText(buf, std::move(utf8Text), SpanContext{});
  }
  BufferHandle::OpRecord rec;
  rec.id = id;
  rec.offset = buf.content_.size();
  rec.length = utf8Text.size();
  buf.content_.append(utf8Text);
  buf.ops_.push_back(rec);
  CommitOp op;
  op.opId = id;
  return op;
}

void Undo(BufferHandle& buf, CommitOp op) {
  for (std::size_t i = 0; i < buf.ops_.size(); ++i) {
    if (buf.ops_[i].id != op.opId) continue;
    const std::size_t offset = buf.ops_[i].offset;
    const std::size_t length = buf.ops_[i].length;
    buf.ops_.erase(buf.ops_.begin() + static_cast<std::ptrdiff_t>(i));
    if (length == 0) return;
    if (offset >= buf.content_.size()) return;
    std::size_t clamped = length;
    if (offset + clamped > buf.content_.size()) {
      clamped = buf.content_.size() - offset;
    }
    buf.content_.erase(offset, clamped);
    // Keep surviving op offsets valid if an older op undoes out of order.
    for (auto& surviving : buf.ops_) {
      if (surviving.offset > offset) {
        surviving.offset -= (clamped < surviving.offset - offset)
                                ? clamped
                                : (surviving.offset - offset);
      }
    }
    return;
  }
}

}  // namespace muse::text::ink
