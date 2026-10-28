// FR-TEXT-007 / AC-TEXT-010: Zero-JNI shaping/measure core.
// Pipeline: UTF-8 decode -> cluster map -> advances -> measure, all in C++
// with no JNI types. The glyph backend is a deterministic stub (glyph id
// derived from the codepoint, fixed advance per codepoint class) standing
// in for the HarfBuzz wiring that follows; validation and pipeline shape
// are real so the follow-up only swaps the backend.

#include "muse/text/shaper.h"

#include <cmath>

namespace muse::text {
namespace {

// Strict UTF-8 decode of one codepoint starting at [pos, len).
// Returns false on any malformed/truncated/over-long/surrogate/out-of-range
// sequence. Out: codepoint value and sequence length in bytes.
bool DecodeOne(const std::uint8_t* data, std::size_t len, std::size_t pos,
               std::uint32_t* codepoint, std::size_t* seq_len) {
  const std::uint8_t lead = data[pos];
  if (lead < 0x80) {
    *codepoint = lead;
    *seq_len = 1;
    return true;
  }
  std::uint32_t cp = 0;
  std::size_t want = 0;
  std::uint32_t min = 0;
  if ((lead & 0xE0) == 0xC0) {
    cp = lead & 0x1F;
    want = 2;
    min = 0x80;
  } else if ((lead & 0xF0) == 0xE0) {
    cp = lead & 0x0F;
    want = 3;
    min = 0x800;
  } else if ((lead & 0xF8) == 0xF0) {
    cp = lead & 0x07;
    want = 4;
    min = 0x10000;
  } else {
    return false;  // Lone continuation or impossible lead (0xF8+).
  }
  if (pos + want > len) {
    return false;  // Truncated sequence.
  }
  for (std::size_t k = 1; k < want; ++k) {
    const std::uint8_t cont = data[pos + k];
    if ((cont & 0xC0) != 0x80) {
      return false;
    }
    cp = (cp << 6) | (cont & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    return false;  // Over-long, surrogate, or out of range.
  }
  *codepoint = cp;
  *seq_len = want;
  return true;
}

// Stub backend advance: deterministic per codepoint, scaled by style.
// Real HarfBuzz advances replace this function; keep the call site stable.
float AdvanceFor(std::uint32_t codepoint, const TextStyle& style) {
  float base = 10.0f;
  if (codepoint < 0x80) {
    base = 9.0f;
  } else if (codepoint < 0x800) {
    base = 11.0f;
  }
  const float scale = style.size_px / 16.0f;
  return base * scale + style.letter_spacing_px;
}

}  // namespace

StatusOr<ShapedRun> shapeRun(const std::uint8_t* utf8, std::size_t len,
                             FontHandle /*font*/, const TextStyle& style) {
  if (len == 0) {
    return ShapedRun{};
  }
  if (utf8 == nullptr) {
    return Status::kInvalidScript;
  }
  ShapedRun run;
  run.cluster_map.resize(len);
  std::size_t pos = 0;
  while (pos < len) {
    std::uint32_t codepoint = 0;
    std::size_t seq_len = 0;
    if (!DecodeOne(utf8, len, pos, &codepoint, &seq_len)) {
      return Status::kInvalidScript;
    }
    const auto glyph_index =
        static_cast<std::uint32_t>(run.glyph_ids.size());
    // Stub glyph mapping: stable 1:1 codepoint -> id so shaping is
    // deterministic and non-empty without a font backend.
    run.glyph_ids.push_back(codepoint);
    run.advances.push_back(AdvanceFor(codepoint, style));
    run.offsets.push_back(GlyphOffset{});
    for (std::size_t k = 0; k < seq_len; ++k) {
      run.cluster_map[pos + k] = glyph_index;
    }
    pos += seq_len;
  }
  return run;
}

MeasuredLine measureLine(ShapedRunView view, float maxWidthPx) {
  MeasuredLine line;
  if (view.run() == nullptr) {
    return line;
  }
  float total = 0.0f;
  for (float advance : view.run()->advances) {
    total += advance;
  }
  if (maxWidthPx < 0.0f) {
    maxWidthPx = 0.0f;
  }
  line.width_px = total < maxWidthPx ? total : maxWidthPx;
  line.glyph_count = view.glyph_count();
  return line;
}

namespace {

// Zero-JNI text-path counters. The shaping/measuring path performs no JNI
// calls, so these stay 0 unless a future backend records one; the hook
// below snapshots and resets them for profiled frames.
JniCallCounter g_text_jni_counters;
}  // namespace

JniCallCounter resetTextJniCounters() {
  JniCallCounter previous = g_text_jni_counters;
  g_text_jni_counters = JniCallCounter{};
  return previous;
}

bool advancesMatchReference(ShapedRunView a, const float* refAdvances,
                            std::size_t n, float tolPx) {
  const std::size_t count =
      a.run() != nullptr ? a.run()->advances.size() : 0;
  if (count != n) {
    return false;
  }
  if (n == 0) {
    return true;
  }
  if (refAdvances == nullptr || a.run() == nullptr) {
    return false;
  }
  if (!(tolPx >= 0.0f)) {
    tolPx = 0.0f;
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (std::fabs(a.run()->advances[i] - refAdvances[i]) > tolPx) {
      return false;
    }
  }
  return true;
}

}  // namespace muse::text
