#pragma once

// FR-TEXT-015 / AC-TEXT-042 (+AC-TEXT-044 part): atomic undo/redo over text,
// spans, and inline objects (TC-TEXT-042).
// One mixed text+span+image session entry undoes/redoes to byte-identical
// buffer, span, and inline-object state, building on the FR-TEXT-008 undo
// stack (consumed through this interface only, no runtime headers here).
// Edits between BeginAtomicEdit and CommitAtomicEdit are staged by appending
// to the public BufferHandle members; Undo/Redo restore all three members
// byte-identically in stack order. Undo/redo on an empty stack are no-ops
// and never throw (gap-buffer clamp/no-throw convention). A new committed
// session clears the redo stack. Never throws (bad_alloc aside).

#include <cstdint>
#include <string>
#include <vector>

namespace muse::text::edit {

// Session token returned by BeginAtomicEdit and consumed by CommitAtomicEdit.
struct UndoOp {
  std::uint64_t id = 0;
};

// Editable buffer staging surface: `text_data` (buffer bytes), `span_data`
// (span state bytes), `object_data` (inline-object state bytes), with
// `text()` / `spans()` / `objects()` fingerprint readers over those members.
// Carries its own undo/redo stacks so free-function Undo/Redo need only the
// buffer (stack order, unlike T4's op-addressed Undo).
struct BufferHandle {
  std::string text_data;
  std::string span_data;
  std::string object_data;

  std::string text() const { return text_data; }
  std::string spans() const { return span_data; }
  std::string objects() const { return object_data; }

  struct Snapshot {
    std::string text_data;
    std::string span_data;
    std::string object_data;
  };
  struct Entry {
    Snapshot before;
    Snapshot after;
  };
  struct Pending {
    std::uint64_t id = 0;
    Snapshot before;
  };

  std::vector<Entry> undo_stack;
  std::vector<Entry> redo_stack;
  std::vector<Pending> pending;
  std::uint64_t next_id = 1;
};

// Opens an atomic edit session, capturing the byte-identical before-state.
UndoOp BeginAtomicEdit(BufferHandle& buf);

// Closes the session opened by `op`, pushing one undo entry holding the
// before/after snapshots and clearing the redo stack. Committing an unknown
// or already-committed token is a no-op and never throws.
void CommitAtomicEdit(BufferHandle& buf, UndoOp op);

// Restores the most recent undo entry's before-state; pushes it to the redo
// stack. Empty stack: no-op, never throws.
void Undo(BufferHandle& buf) noexcept;

// Restores the most recent redo entry's after-state; pushes it back to the
// undo stack. Empty stack: no-op, never throws.
void Redo(BufferHandle& buf) noexcept;

}  // namespace muse::text::edit
