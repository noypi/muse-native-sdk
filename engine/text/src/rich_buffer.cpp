// FR-TEXT-005 / AC-TEXT-008: RichBuffer implementation.
// Rendered-byte model: text_ holds plain bytes with each inline object stored
// as the 3-byte U+FFFC sequence (EF BF BC); objects_ tracks the marker
// offsets so erase/insert snap to marker boundaries. Spans shift exactly
// like GapBuffer (insert: lo>=pos and hi>pos shift; erase: collapse into
// the erase start). Undo/redo are whole-state snapshots, so each mutation
// (including setSpan and insertObject) is atomic and restores byte-identical
// text/span/object state.

#include "muse/text/rich_buffer.h"

#include <algorithm>

namespace muse::text {
namespace {

constexpr char kFffc[] = "\xEF\xBF\xBC";
constexpr size_t kFffcLen = 3;

size_t shiftPointForErase(size_t x, size_t pos, size_t end, size_t n) {
  if (x <= pos) return x;
  if (x >= end) return x - n;
  return pos;
}

}  // namespace

void RichBuffer::pushUndo() {
  undo_.push_back(State{text_, spans_, objects_});
  if (undo_.size() > kUndoDepth) {
    undo_.erase(undo_.begin());
  }
  redo_.clear();
}

bool RichBuffer::markerAt(size_t pos) const {
  for (const auto& o : objects_) {
    if (o.pos == pos) return true;
  }
  return false;
}

size_t RichBuffer::snapPos(size_t pos) const {
  if (pos > text_.size()) pos = text_.size();
  for (const auto& o : objects_) {
    if (pos > o.pos && pos < o.pos + kFffcLen) return o.pos;
  }
  return pos;
}

void RichBuffer::insert(size_t pos, std::string_view s) {
  if (s.empty()) return;
  pos = snapPos(pos);
  pushUndo();
  for (auto& sp : spans_) {
    if (sp.lo >= pos) sp.lo += s.size();
    if (sp.hi > pos) sp.hi += s.size();
  }
  for (auto& o : objects_) {
    if (o.pos >= pos) o.pos += s.size();
  }
  text_.insert(pos, s.data(), s.size());
}

void RichBuffer::erase(size_t pos, size_t len) {
  if (len == 0 || pos >= text_.size()) return;
  pos = snapPos(pos);
  len = std::min(len, text_.size() - pos);
  size_t end = pos + len;
  // Expand to cover partially overlapped markers so a marker is never split.
  for (const auto& o : objects_) {
    const size_t m0 = o.pos;
    const size_t m1 = o.pos + kFffcLen;
    if (pos < m1 && end > m0) {
      if (pos > m0 || end < m1) {
        if (pos > m0) pos = m0;
        if (end < m1) end = m1;
      }
    }
  }
  const size_t n = end - pos;
  pushUndo();
  text_.erase(pos, n);
  std::vector<ObjectEntry> kept;
  kept.reserve(objects_.size());
  for (auto& o : objects_) {
    if (o.pos >= pos && o.pos < end) continue;  // marker removed
    ObjectEntry e = o;
    if (e.pos >= end) e.pos -= n;
    kept.push_back(e);
  }
  objects_.swap(kept);
  for (auto& sp : spans_) {
    sp.lo = shiftPointForErase(sp.lo, pos, end, n);
    sp.hi = shiftPointForErase(sp.hi, pos, end, n);
    if (sp.lo > sp.hi) sp.lo = sp.hi;
  }
}

void RichBuffer::setSpan(size_t lo, size_t hi, SpanKind kind,
                         uint32_t payload) {
  lo = std::min(lo, text_.size());
  hi = std::min(hi, text_.size());
  if (lo > hi) return;
  pushUndo();
  spans_.push_back(Span{lo, hi, kind, payload});
}

void RichBuffer::insertObject(size_t pos, const InlineObject& obj) {
  pos = snapPos(pos);
  pushUndo();
  for (auto& sp : spans_) {
    if (sp.lo >= pos) sp.lo += kFffcLen;
    if (sp.hi > pos) sp.hi += kFffcLen;
  }
  for (auto& o : objects_) {
    if (o.pos >= pos) o.pos += kFffcLen;
  }
  text_.insert(pos, kFffc, kFffcLen);
  ObjectEntry e;
  e.pos = pos;
  e.obj = obj;
  objects_.push_back(e);
  std::sort(objects_.begin(), objects_.end(),
            [](const ObjectEntry& a, const ObjectEntry& b) {
              return a.pos < b.pos;
            });
}

bool RichBuffer::undo() {
  if (undo_.empty()) return false;
  redo_.push_back(State{text_, spans_, objects_});
  State s = std::move(undo_.back());
  undo_.pop_back();
  text_ = std::move(s.text);
  spans_ = std::move(s.spans);
  objects_ = std::move(s.objects);
  return true;
}

bool RichBuffer::redo() {
  if (redo_.empty()) return false;
  undo_.push_back(State{text_, spans_, objects_});
  if (undo_.size() > kUndoDepth) undo_.erase(undo_.begin());
  State s = std::move(redo_.back());
  redo_.pop_back();
  text_ = std::move(s.text);
  spans_ = std::move(s.spans);
  objects_ = std::move(s.objects);
  return true;
}

std::string RichBuffer::plainText() const { return text_; }

}  // namespace muse::text
