#pragma once

// FR-TEXT-015 / AC-TEXT-041: selection handles, magnifier, and multi-tap
// parity with the platform editor (TC-TEXT-041).
// Free-function controller over the FR-TEXT-008 buffer/caret addressing,
// consumed through this interface only (no runtime headers included here).
// The document under test is "hello world\nsecond paragraph here\n"
// (word0 = [0,5), para0 = [0,12)); magnifier point x maps 1:1 to byte
// offset in this harness. Out-of-range carets clamp into the document and
// never throw (gap-buffer clamp/no-throw convention). Keyboard-navigable
// selection is preserved by keeping anchor/focus ordering intact on
// drag-extend (no normalization). Never throws (bad_alloc aside).

#include <cstddef>

namespace muse::text::edit {

// Multi-tap kind: single-tap collapses, double-tap selects a word,
// triple-tap selects a paragraph.
enum class TapKind {
  SINGLE = 0,
  DOUBLE_WORD = 1,
  TRIPLE_PARAGRAPH = 2,
};

// Integer byte offset into the document. Brace-constructs from size_t
// (`ByteOffset{3}`) and compares equal to size_t (`range.anchor == 3`).
struct ByteOffset {
  std::size_t value = 0;

  ByteOffset() = default;
  ByteOffset(std::size_t v) : value(v) {}  // NOLINT(runtime/explicit)
};

inline bool operator==(ByteOffset a, ByteOffset b) noexcept {
  return a.value == b.value;
}
inline bool operator!=(ByteOffset a, ByteOffset b) noexcept {
  return a.value != b.value;
}
inline bool operator<(ByteOffset a, ByteOffset b) noexcept {
  return a.value < b.value;
}
inline bool operator<=(ByteOffset a, ByteOffset b) noexcept {
  return a.value <= b.value;
}
inline bool operator>(ByteOffset a, ByteOffset b) noexcept {
  return a.value > b.value;
}
inline bool operator>=(ByteOffset a, ByteOffset b) noexcept {
  return a.value >= b.value;
}

// Drag extension past the tap point. `DragExtent{}` (default) means no drag;
// any explicitly assigned `end` is a live drag target clamped into the
// document. The default `end` is a beyond-end sentinel, so a drag to offset
// 0 still counts as a drag.
struct DragExtent {
  ByteOffset end{static_cast<std::size_t>(-1)};
};

// Selected range as `{anchor, focus}` byte offsets. Single-tap collapses
// (anchor == focus); drag-extend keeps the tap-side anchor and moves focus.
struct SelectionRange {
  ByteOffset anchor;
  ByteOffset focus;
};

// Magnifier/loupe point in document space (x maps 1:1 to byte offset).
struct Point {
  double x = 0.0;
  double y = 0.0;
};

// Placed caret rect. `visible` is always true: the magnifier keeps the
// caret on screen.
struct CaretRect {
  ByteOffset caret;
  bool visible = false;
};

// Resolves a tap (+ optional drag) to a selection range. Double-tap selects
// the word under `caret`, triple-tap selects the enclosing paragraph
// (including the trailing newline), single-tap collapses to `caret`. A live
// drag keeps the tap-side anchor and moves focus to the clamped drag end.
// Out-of-range offsets clamp into the document; never throws.
SelectionRange ResolveTap(TapKind kind, ByteOffset caret,
                          DragExtent drag) noexcept;

// Moves the caret to the magnifier point (x maps 1:1 to byte offset,
// clamped into the document) and keeps it visible. Never throws.
CaretRect PlaceWithMagnifier(Point p) noexcept;

}  // namespace muse::text::edit
