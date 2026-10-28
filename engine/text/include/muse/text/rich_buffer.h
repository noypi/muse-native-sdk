#pragma once

// FR-TEXT-005 / AC-TEXT-008: rich-text buffer over one gap-buffer semantic.
// Spans ride byte offsets and shift on insert/erase (mirrors GapBuffer in
// engine/runtime/include/runtime/gap_buffer.h); inline objects degrade to
// U+FFFC in plainText(); every mutation is one atomic undo op with
// new-edit-clears-redo semantics.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace muse::text {

enum class SpanKind : uint32_t {
  Bold,
  Italic,
  Underline,
  Strike,
  Color,
  Background,
  Link,
  Code,
  Ruby
};

struct InlineObject {
  uint32_t id;
  float widthPx;
  float heightPx;
  float baselineOffsetPx;
};

class RichBuffer {
 public:
  RichBuffer() = default;

  void insert(size_t pos, std::string_view s);
  void erase(size_t pos, size_t len);
  void setSpan(size_t lo, size_t hi, SpanKind kind, uint32_t payload);
  void insertObject(size_t pos, const InlineObject& obj);
  bool undo();
  bool redo();
  std::string plainText() const;

 private:
  struct Span {
    size_t lo = 0;
    size_t hi = 0;
    SpanKind kind = SpanKind::Bold;
    uint32_t payload = 0;
  };
  struct ObjectEntry {
    size_t pos = 0;  // rendered byte offset of the U+FFFC marker.
    InlineObject obj{};
  };
  struct State {
    std::string text;
    std::vector<Span> spans;
    std::vector<ObjectEntry> objects;
  };

  static constexpr size_t kUndoDepth = 100;

  void pushUndo();
  size_t snapPos(size_t pos) const;
  bool markerAt(size_t pos) const;

  std::string text_;
  std::vector<Span> spans_;
  std::vector<ObjectEntry> objects_;
  std::vector<State> undo_;
  std::vector<State> redo_;
};

}  // namespace muse::text
