#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Gap buffer text model (FR-TEXT-008 / AC-TEXT-014). Hermetic: plain
// in-memory bytes with cursor ranges, undo/redo snapshots, and span tags.
// Out-of-range positions/lengths clamp to [0, byte_size()]; empty
// undo()/redo() are no-ops. Never throws on bad indices (allocation
// failure may still throw std::bad_alloc).
class GapBuffer {
 public:
  GapBuffer() : buf_(kInitialGap, '\0'), gap_start_(0), gap_end_(kInitialGap) {}

  void insert(size_t byte_pos, std::string_view s) {
    if (s.empty()) {
      return;
    }
    byte_pos = clamp_pos(byte_pos);
    push_text_undo(UndoOp::Kind::Insert, byte_pos, std::string(s));
    move_gap(byte_pos);
    ensure_gap(s.size());
    std::copy(s.begin(), s.end(), buf_.begin() + gap_start_);
    gap_start_ += s.size();
    shift_spans_for_insert(byte_pos, s.size());
    stamp_spans_after();
  }

  void erase(size_t byte_pos, size_t len) {
    const size_t size = byte_size();
    if (byte_pos >= size || len == 0) {
      return;
    }
    len = (std::min)(len, size - byte_pos);
    push_text_undo(UndoOp::Kind::Erase, byte_pos, read_range(byte_pos, len));
    move_gap(byte_pos);
    // Drop [gap_start_, gap_start_ + len) from the tail side by widening gap.
    gap_end_ += len;
    shift_spans_for_erase(byte_pos, len);
    stamp_spans_after();
  }

  std::string to_string() const {
    std::string out;
    out.resize(byte_size());
    const size_t head = gap_start_;
    for (size_t i = 0; i < head; ++i) {
      out[i] = buf_[i];
    }
    const size_t tail = buf_.size() - gap_end_;
    for (size_t i = 0; i < tail; ++i) {
      out[head + i] = buf_[gap_end_ + i];
    }
    return out;
  }

  size_t byte_size() const { return buf_.size() - (gap_end_ - gap_start_); }

  void set_cursors(const std::vector<std::pair<size_t, size_t>>& ranges) {
    const size_t size = byte_size();
    cursors_ = ranges;
    for (auto& r : cursors_) {
      r.first = (std::min)(r.first, size);
      r.second = (std::min)(r.second, size);
    }
  }

  const std::vector<std::pair<size_t, size_t>>& cursors() const { return cursors_; }

  void insert_at_cursors(std::string_view s) {
    if (s.empty() || cursors_.empty()) {
      return;
    }
    const size_t size = byte_size();
    std::vector<size_t> positions;
    positions.reserve(cursors_.size());
    for (const auto& r : cursors_) {
      positions.push_back((std::min)(r.second, size));
    }
    std::sort(positions.begin(), positions.end(), std::greater<size_t>());
    for (size_t pos : positions) {
      insert(pos, s);
    }
  }

  void undo() {
    if (undo_.empty()) {
      return;
    }
    UndoOp op = std::move(undo_.back());
    undo_.pop_back();
    apply_undo(op);
    redo_.push_back(std::move(op));
    if (redo_.size() > kUndoDepth) {
      redo_.pop_front();
    }
  }

  void redo() {
    if (redo_.empty()) {
      return;
    }
    UndoOp op = std::move(redo_.back());
    redo_.pop_back();
    apply_redo(op);
    undo_.push_back(std::move(op));
    if (undo_.size() > kUndoDepth) {
      undo_.pop_front();
    }
  }

  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }

  void tag_span(size_t lo, size_t hi, uint32_t span_kind) {
    const size_t size = byte_size();
    lo = (std::min)(lo, size);
    hi = (std::min)(hi, size);
    if (lo > hi) {
      // Inverted range collapses to empty: store nothing (aligns with
      // set_composing_region's lo=hi collapse rule).
      return;
    }
    push_span_undo();
    spans_.push_back(Span{lo, hi, span_kind});
    stamp_spans_after();
  }

  void clear_spans_of_kind(uint32_t span_kind) {
    bool any = false;
    for (const auto& sp : spans_) {
      if (sp.kind == span_kind) {
        any = true;
        break;
      }
    }
    if (!any) {
      return;
    }
    push_span_undo();
    spans_.erase(std::remove_if(spans_.begin(), spans_.end(),
                                [span_kind](const Span& sp) {
                                  return sp.kind == span_kind;
                                }),
                 spans_.end());
    stamp_spans_after();
  }

  std::vector<std::pair<size_t, size_t>> spans_of_kind(
      uint32_t span_kind) const {
    std::vector<std::pair<size_t, size_t>> out;
    for (const auto& sp : spans_) {
      if (sp.kind == span_kind) {
        out.emplace_back(sp.lo, sp.hi);
      }
    }
    return out;
  }

 private:
  struct Span {
    size_t lo = 0;
    size_t hi = 0;
    uint32_t kind = 0;
  };
  // Inverse-op undo record: text edits store the bytes needed for the
  // inverse (insert<->erase); span edits store the span vector before and
  // after so undo/redo restores span state without a full-text copy.
  struct UndoOp {
    enum class Kind { Insert, Erase, Spans };
    Kind kind = Kind::Insert;
    size_t pos = 0;
    std::string text;
    std::vector<Span> spans_before;
    std::vector<Span> spans_after;
  };

  static constexpr size_t kInitialGap = 64;
  static constexpr size_t kUndoDepth = 100;

  size_t clamp_pos(size_t pos) const { return (std::min)(pos, byte_size()); }

  void move_gap(size_t pos) {
    if (pos < gap_start_) {
      const size_t n = gap_start_ - pos;
      const size_t new_gap_end = gap_end_ - n;
      for (size_t i = 0; i < n; ++i) {
        buf_[new_gap_end + i] = buf_[pos + i];
      }
      gap_start_ = pos;
      gap_end_ = new_gap_end;
    } else if (pos > gap_start_) {
      const size_t n = pos - gap_start_;
      for (size_t i = 0; i < n; ++i) {
        buf_[gap_start_ + i] = buf_[gap_end_ + i];
      }
      gap_start_ = pos;
      gap_end_ += n;
    }
  }

  void ensure_gap(size_t n) {
    const size_t gap = gap_end_ - gap_start_;
    if (gap >= n) {
      return;
    }
    size_t grown = buf_.size() == 0 ? kInitialGap : buf_.size();
    while (grown - byte_size() < n) {
      grown *= 2;
    }
    std::vector<char> next(grown, '\0');
    for (size_t i = 0; i < gap_start_; ++i) {
      next[i] = buf_[i];
    }
    const size_t tail = buf_.size() - gap_end_;
    const size_t new_gap_end = grown - tail;
    for (size_t i = 0; i < tail; ++i) {
      next[new_gap_end + i] = buf_[gap_end_ + i];
    }
    buf_.swap(next);
    gap_end_ = new_gap_end;
  }

  void shift_spans_for_insert(size_t pos, size_t n) {
    for (auto& sp : spans_) {
      if (sp.lo >= pos) {
        sp.lo += n;
      }
      if (sp.hi > pos) {
        sp.hi += n;
      }
    }
  }

  void shift_spans_for_erase(size_t pos, size_t n) {
    const size_t end = pos + n;
    for (auto& sp : spans_) {
      sp.lo = shift_point_for_erase(sp.lo, pos, end, n);
      sp.hi = shift_point_for_erase(sp.hi, pos, end, n);
      if (sp.lo > sp.hi) {
        sp.lo = sp.hi;
      }
    }
  }

  static size_t shift_point_for_erase(size_t x, size_t pos, size_t end, size_t n) {
    if (x <= pos) {
      return x;
    }
    if (x >= end) {
      return x - n;
    }
    return pos;
  }

  std::string read_range(size_t pos, size_t len) const {
    std::string out;
    out.resize(len);
    for (size_t i = 0; i < len; ++i) {
      size_t p = pos + i;
      out[i] = (p < gap_start_) ? buf_[p] : buf_[gap_end_ + (p - gap_start_)];
    }
    return out;
  }

  void insert_text_raw(size_t pos, std::string_view s) {
    move_gap(pos);
    ensure_gap(s.size());
    std::copy(s.begin(), s.end(), buf_.begin() + gap_start_);
    gap_start_ += s.size();
    shift_spans_for_insert(pos, s.size());
  }

  void erase_text_raw(size_t pos, size_t len) {
    move_gap(pos);
    gap_end_ += len;
    shift_spans_for_erase(pos, len);
  }

  void apply_undo(const UndoOp& op) {
    switch (op.kind) {
      case UndoOp::Kind::Insert:
        erase_text_raw(op.pos, op.text.size());
        break;
      case UndoOp::Kind::Erase:
        insert_text_raw(op.pos, op.text);
        break;
      case UndoOp::Kind::Spans:
        break;
    }
    spans_ = op.spans_before;
  }

  void apply_redo(const UndoOp& op) {
    switch (op.kind) {
      case UndoOp::Kind::Insert:
        insert_text_raw(op.pos, op.text);
        break;
      case UndoOp::Kind::Erase:
        erase_text_raw(op.pos, op.text.size());
        break;
      case UndoOp::Kind::Spans:
        break;
    }
    spans_ = op.spans_after;
  }

  void push_text_undo(UndoOp::Kind kind, size_t pos, std::string text) {
    UndoOp op;
    op.kind = kind;
    op.pos = pos;
    op.text = std::move(text);
    op.spans_before = spans_;
    undo_.push_back(std::move(op));
    // spans_after for a text op is not known until after the edit shifts
    // spans; fill it in lazily is complex, so recompute: the caller applies
    // the edit right after; instead record after-state here by deferring.
    // Simplest exact approach: refresh after the edit via fixup in
    // insert/erase — handled by capturing in those methods below.
    if (undo_.size() > kUndoDepth) {
      undo_.pop_front();
    }
    redo_.clear();
  }

  void push_span_undo() {
    UndoOp op;
    op.kind = UndoOp::Kind::Spans;
    op.spans_before = spans_;
    undo_.push_back(std::move(op));
    if (undo_.size() > kUndoDepth) {
      undo_.pop_front();
    }
    redo_.clear();
  }

  // After a span mutation, stamp spans_after on the just-pushed record.
  void stamp_spans_after() {
    if (!undo_.empty()) {
      undo_.back().spans_after = spans_;
    }
  }

  std::vector<char> buf_;
  size_t gap_start_ = 0;
  size_t gap_end_ = 0;
  std::vector<std::pair<size_t, size_t>> cursors_;
  std::vector<Span> spans_;
  std::deque<UndoOp> undo_;
  std::deque<UndoOp> redo_;
};
