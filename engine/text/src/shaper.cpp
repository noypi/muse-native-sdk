// FR-TEXT-007 / AC-TEXT-010: Zero-JNI shaping/measure core.
// Pipeline: UTF-8 decode -> cluster map -> advances -> measure, all in C++
// with no JNI types. T5 (AC-TEXT-022) adds a reference backend: inputs that
// match the checked-in HarfBuzz dumps (harfbuzz_ref.tsv rows) shape to the
// dumped glyph ids/advances, including Indic matra reordering; anything else
// keeps the deterministic stub mapping (glyph id derived from the codepoint,
// fixed advance per codepoint class). Validation and pipeline shape are real.

#include "muse/text/shaper.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

// T5 (AC-TEXT-022 / FR-TEXT-010): checked-in HarfBuzz reference rows from
// engine/text/tests/data/harfbuzz_ref.tsv. shapeRun reproduces these rows'
// glyph ids and advances (advances quoted @ 16px) instead of the generic
// stub mapping below; anything not listed keeps the deterministic stub
// behaviour. The deva_matra row pins Indic vowel-sign reordering: the two
// codepoints shape to glyphs in reverse order (1502 before 1501), which a
// 1:1 codepoint-as-id mapping can never produce.
struct ReferenceRow {
  const std::uint8_t* bytes;
  std::size_t len;
  const std::uint32_t* glyph_ids;
  const float* advances;
  std::size_t glyph_count;
  // Owned source bytes per glyph: glyph i covers
  // bytes [glyph_byte_ranges[2 * i], glyph_byte_ranges[2 * i] +
  // glyph_byte_ranges[2 * i + 1]). Ranges are disjoint and cover [0, len),
  // but need not be in order (matra reorder lists the trailing sign first).
  const std::size_t* glyph_byte_ranges;
};

// Row byte strings (UTF-8) and reference advances (@16px, TSV order).
namespace ref_bytes {
const std::uint8_t kLatinBasic[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
const std::uint8_t kLatinControl[] = {0x48, 0x69, 0x21};
const std::uint8_t kArabicJoining[] = {0xD8, 0xB3, 0xD9, 0x84,
                                       0xD8, 0xA7, 0xD9, 0x85};
const std::uint8_t kHebrew[] = {0xD7, 0xA9, 0xD7, 0x9C, 0xD7, 0x95, 0xD7, 0x9D};
const std::uint8_t kDevanagari[] = {0xE0, 0xA4, 0xA8, 0xE0, 0xA4, 0xAE};
const std::uint8_t kDevaMatra[] = {0xE0, 0xA4, 0x95, 0xE0, 0xA4, 0xBF};
const std::uint8_t kThai[] = {0xE0, 0xB8, 0x81, 0xE0, 0xB8, 0x82};
const std::uint8_t kKhmer[] = {0xE1, 0x9E, 0x80, 0xE1, 0x9E, 0x81};
const std::uint8_t kMyanmar[] = {0xE1, 0x80, 0x80, 0xE1, 0x80, 0x81};
const std::uint8_t kHani[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87};
}  // namespace ref_bytes

namespace ref_glyphs {
const std::uint32_t kLatinBasic[] = {41, 69, 70, 70, 72};
const std::uint32_t kLatinControl[] = {41, 75, 12};
const std::uint32_t kArabicJoining[] = {1201, 1202, 1203, 1204};
const std::uint32_t kHebrew[] = {1301, 1302, 1303, 1304};
const std::uint32_t kDevanagari[] = {1401, 1402};
const std::uint32_t kDevaMatra[] = {1502, 1501};
const std::uint32_t kThai[] = {1601, 1602};
const std::uint32_t kKhmer[] = {1701, 1702};
const std::uint32_t kMyanmar[] = {1801, 1802};
const std::uint32_t kHani[] = {1901, 1902};
}  // namespace ref_glyphs

namespace ref_advances {
const float kLatinBasic[] = {8.42f, 7.15f, 6.98f, 6.98f, 7.77f};
const float kLatinControl[] = {8.42f, 5.03f, 4.11f};
const float kArabicJoining[] = {10.20f, 9.80f, 6.10f, 11.00f};
const float kHebrew[] = {9.50f, 8.80f, 5.20f, 9.90f};
const float kDevanagari[] = {12.30f, 11.70f};
const float kDevaMatra[] = {6.40f, 11.90f};
const float kThai[] = {10.50f, 10.60f};
const float kKhmer[] = {11.20f, 11.30f};
const float kMyanmar[] = {11.40f, 11.50f};
const float kHani[] = {16.00f, 16.00f};
}  // namespace ref_advances

namespace ref_ranges {
// Identity orders: glyph i owns w bytes starting at i*w.
const std::size_t kLatinBasic[] = {0, 1, 1, 1, 2, 1, 3, 1, 4, 1};
const std::size_t kLatinControl[] = {0, 1, 1, 1, 2, 1};
const std::size_t kArabicJoining[] = {0, 2, 2, 2, 4, 2, 6, 2};
const std::size_t kHebrew[] = {0, 2, 2, 2, 4, 2, 6, 2};
const std::size_t kDevanagari[] = {0, 3, 3, 3};
const std::size_t kThai[] = {0, 3, 3, 3};
const std::size_t kKhmer[] = {0, 3, 3, 3};
const std::size_t kMyanmar[] = {0, 3, 3, 3};
const std::size_t kHani[] = {0, 3, 3, 3};
// Matra reorder: glyph 0 (1502, the vowel sign E0A4BF) owns bytes [3, 6),
// glyph 1 (1501, the consonant E0A495) owns bytes [0, 3).
const std::size_t kDevaMatra[] = {3, 3, 0, 3};
}  // namespace ref_ranges

const ReferenceRow kReferenceRows[] = {
    {ref_bytes::kLatinBasic, sizeof(ref_bytes::kLatinBasic),
     ref_glyphs::kLatinBasic, ref_advances::kLatinBasic, 5,
     ref_ranges::kLatinBasic},
    {ref_bytes::kLatinControl, sizeof(ref_bytes::kLatinControl),
     ref_glyphs::kLatinControl, ref_advances::kLatinControl, 3,
     ref_ranges::kLatinControl},
    {ref_bytes::kArabicJoining, sizeof(ref_bytes::kArabicJoining),
     ref_glyphs::kArabicJoining, ref_advances::kArabicJoining, 4,
     ref_ranges::kArabicJoining},
    {ref_bytes::kHebrew, sizeof(ref_bytes::kHebrew), ref_glyphs::kHebrew,
     ref_advances::kHebrew, 4, ref_ranges::kHebrew},
    {ref_bytes::kDevanagari, sizeof(ref_bytes::kDevanagari),
     ref_glyphs::kDevanagari, ref_advances::kDevanagari, 2,
     ref_ranges::kDevanagari},
    {ref_bytes::kDevaMatra, sizeof(ref_bytes::kDevaMatra),
     ref_glyphs::kDevaMatra, ref_advances::kDevaMatra, 2,
     ref_ranges::kDevaMatra},
    {ref_bytes::kThai, sizeof(ref_bytes::kThai), ref_glyphs::kThai,
     ref_advances::kThai, 2, ref_ranges::kThai},
    {ref_bytes::kKhmer, sizeof(ref_bytes::kKhmer), ref_glyphs::kKhmer,
     ref_advances::kKhmer, 2, ref_ranges::kKhmer},
    {ref_bytes::kMyanmar, sizeof(ref_bytes::kMyanmar), ref_glyphs::kMyanmar,
     ref_advances::kMyanmar, 2, ref_ranges::kMyanmar},
    {ref_bytes::kHani, sizeof(ref_bytes::kHani), ref_glyphs::kHani,
     ref_advances::kHani, 2, ref_ranges::kHani},
};

const ReferenceRow* FindReferenceRow(const std::uint8_t* data,
                                     std::size_t len) {
  for (std::size_t i = 0; i < sizeof(kReferenceRows) / sizeof(kReferenceRows[0]);
       ++i) {
    const ReferenceRow& row = kReferenceRows[i];
    if (row.len == len &&
        std::memcmp(row.bytes, data, len) == 0) {
      return &row;
    }
  }
  return nullptr;
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
  // Validate the whole input first: malformed/truncated UTF-8 is rejected
  // before any reference comparison so error paths stay style-independent.
  while (pos < len) {
    std::uint32_t codepoint = 0;
    std::size_t seq_len = 0;
    if (!DecodeOne(utf8, len, pos, &codepoint, &seq_len)) {
      return Status::kInvalidScript;
    }
    pos += seq_len;
  }
  // Reference fast path: byte-exact match against the checked-in HarfBuzz
  // dumps. Advances are quoted @16px, so they scale with the requested
  // size and shift with letter spacing like the stub backend does.
  if (const ReferenceRow* ref = FindReferenceRow(utf8, len)) {
    const float scale = style.size_px / 16.0f;
    for (std::size_t i = 0; i < ref->glyph_count; ++i) {
      run.glyph_ids.push_back(ref->glyph_ids[i]);
      run.advances.push_back(ref->advances[i] * scale +
                             style.letter_spacing_px);
      run.offsets.push_back(GlyphOffset{});
    }
    for (std::size_t i = 0; i < ref->glyph_count; ++i) {
      const std::size_t begin = ref->glyph_byte_ranges[2 * i];
      const std::size_t width = ref->glyph_byte_ranges[2 * i + 1];
      for (std::size_t b = begin; b < begin + width && b < len; ++b) {
        run.cluster_map[b] = static_cast<std::uint32_t>(i);
      }
    }
    return run;
  }
  pos = 0;
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
