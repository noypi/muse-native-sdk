#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

#include "runtime/gap_buffer.h"

// Rich clipboard copy/paste (FR-TEXT-013 / AC-TEXT-035). Hermetic: payloads
// are plain in-memory structs; no OS clipboard, no network, no filesystem.
// Out-of-range [lo, hi) clamps to buffer bounds; empty payload paste is a
// no-op; paste inserts (shifts text right, never overwrites). Never throws.
struct ClipPayload {
  std::string plain;
  std::string html;
  bool has_image = false;
};

inline ClipPayload copy_rich(const GapBuffer& buf, size_t lo, size_t hi) noexcept {
  try {
    const size_t size = buf.byte_size();
    lo = (std::min)(lo, size);
    hi = (std::min)(hi, size);
    ClipPayload p;
    if (lo >= hi) {
      return p;
    }
    const std::string text = buf.to_string();
    p.plain = text.substr(lo, hi - lo);
    p.html = p.plain;
    p.has_image = false;
    return p;
  } catch (...) {
    return ClipPayload{};
  }
}

inline void paste_rich(GapBuffer& buf, size_t pos, const ClipPayload& p) noexcept {
  try {
    if (p.plain.empty()) {
      return;
    }
    pos = (std::min)(pos, buf.byte_size());
    buf.insert(pos, p.plain);
  } catch (...) {
  }
}

inline std::string degrade_to_plain(const ClipPayload& p) noexcept {
  try {
    return p.plain;
  } catch (...) {
    return std::string{};
  }
}
