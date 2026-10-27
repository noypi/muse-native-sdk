#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace runtime {

// Single bundled gallery font (plan A3: no shaping/bidi). Empty type:
// metrics are fixed font-table constants; the parameter exists so the
// renderer call site binds to a font without reading files or config.
struct BundledFont {
  BundledFont() = default;
};

struct TextMetrics {
  float ascent = 0.0f;
  float descent = 0.0f;
  float advance = 0.0f;
};

// Bundled-font metrics from font tables; per-run advance is the sum of
// glyph advances. Unknown glyphs (including U+E000 and malformed bytes)
// contribute 0 advance (tofu metric). Never throws: malformed UTF-8 is
// consumed one byte at a time as a 0-advance unit.
inline TextMetrics measure_text(const BundledFont& /*font*/,
                                std::string_view run) {
  constexpr float kAscent = 14.0f;
  constexpr float kDescent = 4.0f;
  constexpr float kAsciiAdvance = 8.0f;

  float advance = 0.0f;
  std::size_t i = 0;
  const std::size_t n = run.size();
  while (i < n) {
    const auto byte = static_cast<unsigned char>(run[i]);
    std::uint32_t codepoint = 0;
    std::size_t length = 0;
    if (byte < 0x80) {
      codepoint = byte;
      length = 1;
    } else if ((byte & 0xE0) == 0xC0) {
      codepoint = byte & 0x1F;
      length = 2;
    } else if ((byte & 0xF0) == 0xE0) {
      codepoint = byte & 0x0F;
      length = 3;
    } else if ((byte & 0xF8) == 0xF0) {
      codepoint = byte & 0x07;
      length = 4;
    } else {
      // Invalid lead byte: 0-advance unit, consume one byte.
      ++i;
      continue;
    }
    if (length == 1) {
      // ASCII: printable range carries the bundled glyph; C0/C1 controls
      // and DEL have no glyph → 0 advance.
      if (codepoint >= 0x20 && codepoint != 0x7F) {
        advance += kAsciiAdvance;
      }
      i += 1;
      continue;
    }
    // Validate continuation bytes without throwing on truncation.
    bool valid = true;
    if (i + length > n) {
      valid = false;
    } else {
      for (std::size_t k = 1; k < length; ++k) {
        const auto cont =
            static_cast<unsigned char>(run[i + k]);
        if ((cont & 0xC0) != 0x80) {
          valid = false;
          break;
        }
        codepoint = (codepoint << 6) | (cont & 0x3F);
      }
    }
    if (!valid) {
      ++i;
      continue;
    }
    // Bundled gallery font carries ASCII only; every multi-byte
    // codepoint (incl. U+E000) is unknown → 0 advance.
    (void)codepoint;
    i += length;
  }

  TextMetrics metrics;
  metrics.ascent = kAscent;
  metrics.descent = kDescent;
  metrics.advance = advance;
  return metrics;
}

}  // namespace runtime
