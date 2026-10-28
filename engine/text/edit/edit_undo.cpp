// FR-TEXT-015 / AC-TEXT-042: atomic undo/redo implementation.
// Snapshot-based entries over the three public staging members: Begin
// captures before-state, Commit captures after-state as one entry (clearing
// redo), Undo/Redo swap whole snapshots so mixed text+span+image sessions
// round-trip byte-identically.

#include "edit_undo.h"

namespace muse::text::edit {

UndoOp BeginAtomicEdit(BufferHandle& buf) {
  BufferHandle::Pending frame;
  frame.id = buf.next_id++;
  if (frame.id == 0) {
    frame.id = buf.next_id++;
  }
  frame.before.text_data = buf.text_data;
  frame.before.span_data = buf.span_data;
  frame.before.object_data = buf.object_data;
  buf.pending.push_back(frame);
  UndoOp op;
  op.id = frame.id;
  return op;
}

void CommitAtomicEdit(BufferHandle& buf, UndoOp op) {
  for (std::size_t i = buf.pending.size(); i > 0; --i) {
    if (buf.pending[i - 1].id == op.id) {
      BufferHandle::Entry entry;
      entry.before = buf.pending[i - 1].before;
      entry.after.text_data = buf.text_data;
      entry.after.span_data = buf.span_data;
      entry.after.object_data = buf.object_data;
      buf.pending.erase(buf.pending.begin() +
                        static_cast<std::ptrdiff_t>(i - 1));
      buf.undo_stack.push_back(entry);
      buf.redo_stack.clear();
      return;
    }
  }
}

void Undo(BufferHandle& buf) noexcept {
  if (buf.undo_stack.empty()) {
    return;
  }
  try {
    BufferHandle::Entry entry = buf.undo_stack.back();
    buf.undo_stack.pop_back();
    buf.text_data = entry.before.text_data;
    buf.span_data = entry.before.span_data;
    buf.object_data = entry.before.object_data;
    buf.redo_stack.push_back(entry);
  } catch (...) {
  }
}

void Redo(BufferHandle& buf) noexcept {
  if (buf.redo_stack.empty()) {
    return;
  }
  try {
    BufferHandle::Entry entry = buf.redo_stack.back();
    buf.redo_stack.pop_back();
    buf.text_data = entry.after.text_data;
    buf.span_data = entry.after.span_data;
    buf.object_data = entry.after.object_data;
    buf.undo_stack.push_back(entry);
  } catch (...) {
  }
}

}  // namespace muse::text::edit
