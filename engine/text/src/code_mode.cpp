// FR-TEXT-005 / AC-TEXT-008: code-mode helpers over RichBuffer::plainText().
// Monospace model: one column per byte except '\t' (next tab stop) and
// '\n' (line boundary, never counted as a column).

#include "muse/text/code_mode.h"

#include "muse/text/rich_buffer.h"

namespace muse::text {
namespace {

bool IsOpen(char c) { return c == '(' || c == '[' || c == '{'; }
bool IsClose(char c) { return c == ')' || c == ']' || c == '}'; }

char Mate(char c) {
  switch (c) {
    case '(':
      return ')';
    case ')':
      return '(';
    case '[':
      return ']';
    case ']':
      return '[';
    case '{':
      return '}';
    case '}':
      return '{';
    default:
      return 0;
  }
}

}  // namespace

size_t ExpandTabs(const RichBuffer& buf, size_t lineStart,
                  uint32_t tabWidth) {
  const std::string text = buf.plainText();
  if (lineStart >= text.size()) return 0;
  if (tabWidth == 0) tabWidth = 1;
  size_t col = 0;
  for (size_t i = lineStart; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\n') break;
    if (c == '\t') {
      col = (col / tabWidth + 1) * tabWidth;
    } else {
      col += 1;
    }
  }
  return col;
}

bool MatchBracket(const RichBuffer& buf, size_t pos, size_t* matchPos) {
  if (matchPos == nullptr) return false;
  const std::string text = buf.plainText();
  if (pos >= text.size()) return false;
  const char c = text[pos];
  if (!IsOpen(c) && !IsClose(c)) return false;
  const char mate = Mate(c);
  const bool forward = IsOpen(c);
  int depth = 0;
  if (forward) {
    for (size_t i = pos; i < text.size(); ++i) {
      if (text[i] == c) {
        ++depth;
      } else if (text[i] == mate) {
        --depth;
        if (depth == 0) {
          *matchPos = i;
          return true;
        }
      }
    }
  } else {
    for (size_t i = pos + 1; i > 0; --i) {
      const char t = text[i - 1];
      if (t == c) {
        ++depth;
      } else if (t == mate) {
        --depth;
        if (depth == 0) {
          *matchPos = i - 1;
          return true;
        }
      }
    }
  }
  return false;
}

uint32_t LineNumberAt(const RichBuffer& buf, size_t pos) {
  const std::string text = buf.plainText();
  if (pos > text.size()) pos = text.size();
  uint32_t line = 0;
  for (size_t i = 0; i < pos; ++i) {
    if (text[i] == '\n') ++line;
  }
  return line;
}

}  // namespace muse::text
