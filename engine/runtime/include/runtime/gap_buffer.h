#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
    push_undo();
    move_gap(byte_pos);
    ensure_gap(s.size());
    std::copy(s.begin(), s.end(), buf_.begin() + gap_start_);
    gap_start_ += s.size();
    shift_spans_for_insert(byte_pos, s.size());
  }

  void erase(size_t byte_pos, size_t len) {
    const size_t size = byte_size();
    if (byte_pos >= size || len == 0) {
      return;
    }
    len = (std::min)(len, size - byte_pos);
    push_undo();
    move_gap(byte_pos);
    // Drop [gap_start_, gap_start_ + len) from the tail side by widening gap.
    gap_end_ += len;
    shift_spans_for_erase(byte_pos, len);
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

  void undo() {
    if (undo_.empty()) {
      return;
    }
    redo_.push_back(snapshot());
    restore(undo_.back());
    undo_.pop_back();
  }

  void redo() {
    if (redo_.empty()) {
      return;
    }
    undo_.push_back(snapshot());
    restore(redo_.back());
    redo_.pop_back();
  }

  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }

  void tag_span(size_t lo, size_t hi, uint32_t span_kind) {
    const size_t size = byte_size();
    lo = (std::min)(lo, size);
    hi = (std::min)(hi, size);
    if (lo > hi) {
      std::swap(lo, hi);
    }
    spans_.push_back(Span{lo, hi, span_kind});
  }

 private:
  struct Span {
    size_t lo = 0;
    size_t hi = 0;
    uint32_t kind = 0;
  };
  struct Snapshot {
    std::string text;
    std::vector<Span> spans;
  };

  static constexpr size_t kInitialGap = 64;

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

  Snapshot snapshot() const {
    Snapshot s;
    s.text = to_string();
    s.spans = spans_;
    return s;
  }

  void restore(const Snapshot& s) {
    const size_t n = s.text.size();
    buf_.assign(n + kInitialGap, '\0');
    for (size_t i = 0; i < n; ++i) {
      buf_[i] = s.text[i];
    }
    gap_start_ = n;
    gap_end_ = buf_.size();
    spans_ = s.spans;
  }

  void push_undo() {
    undo_.push_back(snapshot());
    redo_.clear();
  }

  std::vector<char> buf_;
  size_t gap_start_ = 0;
  size_t gap_end_ = 0;
  std::vector<std::pair<size_t, size_t>> cursors_;
  std::vector<Span> spans_;
  std::vector<Snapshot> undo_;
  std::vector<Snapshot> redo_;
};
