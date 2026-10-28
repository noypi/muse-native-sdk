#pragma once

// FR-TEXT-005 / AC-TEXT-008: code-editor mode over the same RichBuffer.
// Fixed-advance (monospace) helpers: tab expansion to next stop, bracket
// pair scan for ()[]{}, and \n line counting. Soft-wrap is a measurement
// flag only; these queries never mutate buffer content.

#include <cstddef>
#include <cstdint>

namespace muse::text {

class RichBuffer;

struct CodeOptions {
  bool softWrap;
  uint32_t tabWidth;
  bool lineNumbers;
};

// Expanded column width of the line starting at lineStart: each tab
// advances to the next multiple of tabWidth, every other byte is one
// column. Stops at '\n' or end of buffer. Out-of-range lineStart yields 0.
size_t ExpandTabs(const RichBuffer& buf, size_t lineStart, uint32_t tabWidth);

// Bidirectional bracket match for ()[]{}. On success writes *matchPos and
// returns true; returns false for out-of-range / non-bracket / unmatched
// positions, and for null matchPos (never crashes on null).
bool MatchBracket(const RichBuffer& buf, size_t pos, size_t* matchPos);

// 0-based line index: number of '\n' bytes before pos (pos clamped to size).
uint32_t LineNumberAt(const RichBuffer& buf, size_t pos);

}  // namespace muse::text
