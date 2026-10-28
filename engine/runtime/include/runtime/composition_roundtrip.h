#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "runtime/gap_buffer.h"

// Surrounding-text + composing-region helpers (FR-TEXT-009 / AC-TEXT-019).
// Synchronous, hermetic, header-only. Out-of-range inputs clamp to buffer
// bounds; functions never throw on bad indices (allocation failure may
// still throw std::bad_alloc, matching GapBuffer). Empty buffer yields
// empty text with sel 0/0.
struct SurroundingText {
  std::string text;
  size_t sel_lo = 0;
  size_t sel_hi = 0;
};

namespace detail_composition {
inline constexpr uint32_t kComposingKind = 0x434F4D50u;
}  // namespace detail_composition

inline SurroundingText get_surrounding_text(const GapBuffer& buf, size_t before,
                                            size_t after) {
  try {
    const size_t size = buf.byte_size();
    if (size == 0) {
      return SurroundingText{};
    }
    size_t cursor = size;
    size_t anchor_lo = cursor;
    size_t anchor_hi = cursor;
    const auto& cursors = buf.cursors();
    if (!cursors.empty()) {
      anchor_lo = (std::min)(cursors[0].first, size);
      anchor_hi = (std::min)(cursors[0].second, size);
      cursor = (std::min)(cursors[0].second, size);
    }
    const size_t win_lo = cursor >= before ? cursor - before : 0;
    size_t win_hi = size;
    if (after <= size - cursor) {
      win_hi = cursor + after;
    }
    if (win_hi < win_lo) {
      win_hi = win_lo;
    }
    std::string full;
    try {
      full = buf.to_string();
    } catch (...) {
      return SurroundingText{};
    }
    std::string window;
    try {
      window = full.substr(win_lo, win_hi - win_lo);
    } catch (...) {
      return SurroundingText{};
    }
    const size_t rel_lo =
        anchor_lo <= win_lo ? 0 : ((std::min)(anchor_lo, win_hi) - win_lo);
    const size_t rel_hi =
        anchor_hi <= win_lo ? 0 : ((std::min)(anchor_hi, win_hi) - win_lo);
    SurroundingText out;
    out.text = window;
    out.sel_lo = (std::min)(rel_lo, window.size());
    out.sel_hi = (std::min)(rel_hi, window.size());
    return out;
  } catch (...) {
    return SurroundingText{};
  }
}

inline void set_composing_region(GapBuffer& buf, size_t lo, size_t hi) {
  try {
    const size_t size = buf.byte_size();
    lo = (std::min)(lo, size);
    hi = (std::min)(hi, size);
    if (lo > hi) {
      lo = hi;
    }
    buf.clear_spans_of_kind(detail_composition::kComposingKind);
    buf.tag_span(lo, hi, detail_composition::kComposingKind);
  } catch (...) {
    // Never throws on bad indices.
  }
}

inline std::string get_composition_span(const GapBuffer& buf) {
  try {
    const std::vector<std::pair<size_t, size_t>> spans =
        buf.spans_of_kind(detail_composition::kComposingKind);
    if (spans.empty()) {
      return std::string{};
    }
    const size_t size = buf.byte_size();
    size_t lo = (std::min)(spans.back().first, size);
    size_t hi = (std::min)(spans.back().second, size);
    if (lo >= hi) {
      return std::string{};
    }
    std::string full;
    try {
      full = buf.to_string();
    } catch (...) {
      return std::string{};
    }
    try {
      return full.substr(lo, hi - lo);
    } catch (...) {
      return std::string{};
    }
  } catch (...) {
    return std::string{};
  }
}
