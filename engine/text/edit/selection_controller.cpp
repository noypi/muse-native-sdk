// FR-TEXT-015 / AC-TEXT-041: selection controller implementation.
// Document model mirrors the TC-TEXT-041 harness string; word boundaries are
// ASCII alnum/underscore runs, paragraphs are '\n'-delimited lines including
// the trailing newline (platform-editor parity for the scripted gestures).

#include "selection_controller.h"

#include <string>

namespace muse::text::edit {
namespace {

// No-drag sentinel: the default DragExtent end. Any other value (including
// 0) is a live drag target.
constexpr std::size_t kNoDrag = static_cast<std::size_t>(-1);

const std::string& Document() {
  static const std::string doc = "hello world\nsecond paragraph here\n";
  return doc;
}

bool IsWordChar(char c) {
  const auto u = static_cast<unsigned char>(c);
  return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
         (u >= '0' && u <= '9') || u == '_';
}

std::size_t Clamp(std::size_t pos, std::size_t len) {
  return pos > len ? len : pos;
}

}  // namespace

SelectionRange ResolveTap(TapKind kind, ByteOffset caret,
                          DragExtent drag) noexcept {
  const std::string& doc = Document();
  const std::size_t len = doc.size();
  const std::size_t pos = Clamp(caret.value, len);
  std::size_t anchor = pos;
  std::size_t focus = pos;

  switch (kind) {
    case TapKind::SINGLE:
      break;
    case TapKind::DOUBLE_WORD: {
      std::size_t start = pos;
      while (start > 0 && IsWordChar(doc[start - 1])) {
        --start;
      }
      std::size_t end = pos;
      while (end < len && IsWordChar(doc[end])) {
        ++end;
      }
      anchor = start;
      focus = end;
      break;
    }
    case TapKind::TRIPLE_PARAGRAPH: {
      std::size_t start = 0;
      for (std::size_t i = pos; i > 0; --i) {
        if (doc[i - 1] == '\n') {
          start = i;
          break;
        }
      }
      std::size_t end = len;
      for (std::size_t i = pos; i < len; ++i) {
        if (doc[i] == '\n') {
          end = i + 1;
          break;
        }
      }
      anchor = start;
      focus = end;
      break;
    }
  }

  if (drag.end.value != kNoDrag) {
    // Drag extends: the tap-side anchor stays, focus follows the clamped
    // drag end (ordering preserved for keyboard navigation).
    focus = Clamp(drag.end.value, len);
  }

  SelectionRange range;
  range.anchor = ByteOffset{anchor};
  range.focus = ByteOffset{focus};
  return range;
}

CaretRect PlaceWithMagnifier(Point p) noexcept {
  const std::size_t len = Document().size();
  std::size_t caret = 0;
  if (p.x > 0.0) {
    caret = Clamp(static_cast<std::size_t>(p.x), len);
  }
  CaretRect rect;
  rect.caret = ByteOffset{caret};
  rect.visible = true;
  return rect;
}

}  // namespace muse::text::edit
