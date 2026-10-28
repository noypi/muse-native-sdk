#pragma once

#include <cstddef>

// Caret geometry + scroll-to-reveal (FR-TEXT-013 / AC-TEXT-034). Hermetic:
// pure functions over caller-supplied metrics; InsetSync heights arrive as
// plain `float keyboard_height` arguments, no inset header included.
struct CaretRect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 2.0f;
  float h = 16.0f;
};

struct VisibleViewport {
  float top = 0.0f;
  float height = 0.0f;
};

inline CaretRect caret_rect_for_offset(size_t byte_offset, float line_height,
                                       float char_advance,
                                       size_t line_len) noexcept {
  CaretRect r;
  size_t clamped = byte_offset <= line_len ? byte_offset : line_len;
  r.x = static_cast<float>(clamped) * char_advance;
  r.y = 0.0f;
  r.w = 2.0f;
  r.h = line_height;
  return r;
}

inline float scroll_to_reveal(const CaretRect& caret, const VisibleViewport& vp,
                              float keyboard_height) noexcept {
  if (!(vp.height > 0.0f)) {
    return 0.0f;
  }
  float kh = keyboard_height > 0.0f ? keyboard_height : 0.0f;
  float visible_top = vp.top;
  float visible_bottom = vp.top + vp.height - kh;
  if (caret.y < visible_top) {
    return caret.y - visible_top;
  }
  float caret_bottom = caret.y + caret.h;
  if (caret_bottom > visible_bottom) {
    return caret_bottom - visible_bottom;
  }
  return 0.0f;
}
