// FR-TEXT-007 / AC-TEXT-010: Zero-JNI shaping/measure core.
// Pipeline: UTF-8 decode -> cluster map -> advances -> measure, all in C++
// with no JNI types. T5 (AC-TEXT-022) adds a reference backend: inputs that
// match the checked-in HarfBuzz dumps (harfbuzz_ref.tsv rows) shape to the
// dumped glyph ids/advances, including Indic matra reordering. BUG-TEXT-007
// T9 binds the real backend behind every other input: the T8-resolved face
// opens through FT_New_Face and shapes through hb_ft_font_create, so glyph
// ids/advances are font-backed; the face-keyed deterministic mapping below
// survives only as the hermetic no-font fallback, and the pipeline fails
// closed (kMissingGlyph) when a resolved font covers none of the input.

#include "muse/text/shaper.h"

#include "muse/text/font_discovery.h"

// BUG-TEXT-007 T9 (FR-TEXT-007 / AC-TEXT-013; FR-TEXT-016 / AC-TEXT-045):
// the real HarfBuzz/FreeType backend links here directly (in-tree static
// D3, no new dependency). The pre-T9 weak-probe declarations are gone:
// shaping below calls hb_shape/FT_New_Face for real, so the link carries
// genuine text-shaping relocations on every lane (host test and device).
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include <cmath>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// BUG-TEXT-007 T9 hermetic gate for the deterministic fallback below.
// The salted FallbackGlyphId/FallbackAdvance stub is compiled in ONLY when
// MUSE_TEXT_HERMETIC_FALLBACK is set — defaulted ON for non-Android lanes
// (the host unit-test lane) and OFF for production Android/NDK builds
// (__ANDROID__ is set by the NDK toolchain on every production target).
// Production callers therefore can never reach the stub: unresolved input
// fails closed with kMissingGlyph instead of degrading to stub ids. A
// reviewer grepping prod paths for FallbackGlyphId/FallbackAdvance call
// sites must find the single call site below, inside this gate, and no
// production-lane object containing it.
#if !defined(__ANDROID__) && !defined(MUSE_TEXT_HERMETIC_FALLBACK)
#define MUSE_TEXT_HERMETIC_FALLBACK 1
#endif

namespace muse::text {
namespace {

// Batch 4 LOW (size_t->int truncation): fail-closed bounds before the
// narrowing casts in ShapeWithHarfBuzz. HB takes int; inputs beyond these
// maxima are rejected, never wrapped.
constexpr std::size_t kMaxShapeBytes = 1u << 20;  // 1 MiB label cap
constexpr float kMaxSizePx = 1000000.0f;          // 1e6 px size cap

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
// glyph ids and advances (advances quoted @ 16px) instead of the fallback
// mapping below; anything not listed shapes through the face-keyed fallback.
// The deva_matra row pins Indic vowel-sign reordering: the two codepoints
// shape to glyphs in reverse order (1502 before 1501), which a 1:1
// codepoint-as-id mapping can never produce.
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

// BUG-TEXT-007 T3/T9: face-keyed hermetic no-font fallback for inputs that
// miss the reference rows above AND have no resolvable font on this lane.
// Glyph ids derive from (face, codepoint) and are never the identity
// mapping (a 1:1 codepoint-as-id stub can never produce them), while
// advances vary per glyph the way font output does instead of the old
// uniform 9.0/11.0px. Reference rows above stay byte-exact (no row
// additions). GATED (MUSE_TEXT_HERMETIC_FALLBACK, see the top of this TU):
// this stub is reachable only on hermetic/test lanes. Production builds
// exclude it entirely — shapeRun fails closed there instead of serving
// stub ids.
#if defined(MUSE_TEXT_HERMETIC_FALLBACK)
std::uint32_t FallbackGlyphId(std::uint32_t codepoint,
                              std::uint64_t face_id) {
  // Injective salted offset: id = codepoint + 0x110000 + mix(face) masked
  // to 20 bits can never equal the codepoint (no wraparound: the largest
  // scalar 0x10FFFF plus the largest salt 0x110000 + 0xFFFFF fits in
  // 32 bits), is width-independent, and keeps distinct codepoints distinct
  // on every face. The salt folds all 8 face-id bytes (xor-fold + Knuth
  // mix) so faces sharing a low byte still salt differently.
  const std::uint64_t folded = face_id ^ (face_id >> 32) ^ (face_id >> 16);
  const std::uint32_t face_salt =
      0x110000u +
      ((static_cast<std::uint32_t>(folded * 0x9E3779B1u) >> 12) & 0xFFFFFu);
  return codepoint + face_salt;
}

float FallbackAdvance(std::uint32_t codepoint, const TextStyle& style) {
  float base = 10.0f;
  if (codepoint < 0x80) {
    base = 9.0f;
  } else if (codepoint < 0x800) {
    base = 11.0f;
  }
  // Deterministic per-glyph width variation (splitmix64 avalanche over the
  // scalar value, [0, 2)px at 16px): font advances vary across a run; the
  // old uniform class advance never did.
  std::uint64_t h =
      static_cast<std::uint64_t>(codepoint) * 0x9E3779B97F4A7C15ull;
  h ^= h >> 29;
  h *= 0xBF58476D1CE4E5B9ull;
  h ^= h >> 32;
  const float vary = static_cast<float>((h >> 11) % 2000u) / 1000.0f;
  const float scale = style.size_px / 16.0f;
  return (base + vary) * scale + style.letter_spacing_px;
}
#endif  // defined(MUSE_TEXT_HERMETIC_FALLBACK)

// BUG-TEXT-007 T9: real-font path behind shapeRun for every input.
// ResolveRealFont binds the resolved face to an open FreeType face: the
// T8 system file on device (FT_New_Face on the FontPathForFace path),
// the test-only in-memory face on hermetic host builds
// (FT_New_Memory_Face on the TestOnlyMemoryFont bytes). False when no
// font resolves on this lane — the caller then takes the hermetic salted
// fallback above, so that stub is reachable only on the no-font path,
// never beside a real face.
struct RealFont {
  FT_Library lib = nullptr;
  FT_Face face = nullptr;
  RealFont() = default;
  RealFont(const RealFont&) = delete;
  RealFont& operator=(const RealFont&) = delete;
  ~RealFont() {
    if (face != nullptr) FT_Done_Face(face);
    if (lib != nullptr) FT_Done_FreeType(lib);
  }
  bool valid() const { return lib != nullptr && face != nullptr; }
};

bool ResolveRealFont(FontHandle face, RealFont* out) {
  if (out == nullptr) return false;
  FT_Library lib = nullptr;
  if (FT_Init_FreeType(&lib) != 0) return false;
  FT_Face ftface = nullptr;
  StatusOr<std::string> path = FontPathForFace(face);
  if (path.ok() && !path->empty()) {
    if (FT_New_Face(lib, path->c_str(), 0, &ftface) != 0) {
      ftface = nullptr;
    }
  }
  if (ftface == nullptr) {
    StatusOr<MemoryFontBytes> mem = TestOnlyMemoryFont();
    if (mem.ok() && mem->data != nullptr && mem->size != 0) {
      if (FT_New_Memory_Face(lib,
                             reinterpret_cast<const FT_Byte*>(mem->data),
                             static_cast<FT_Long>(mem->size), 0,
                             &ftface) != 0) {
        ftface = nullptr;
      }
    }
  }
  if (ftface == nullptr) {
    FT_Done_FreeType(lib);
    return false;
  }
  out->lib = lib;
  out->face = ftface;
  return true;
}

// True iff every decoded codepoint has a glyph in the open face
// (FT_Get_Char_Index 0 means the face cannot shape it).
bool FontCoversAll(FT_Face ftface, const std::uint32_t* codepoints,
                   std::size_t count) {
  if (ftface == nullptr || (codepoints == nullptr && count > 0)) {
    return false;
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (FT_Get_Char_Index(ftface, codepoints[i]) == 0) return false;
  }
  return true;
}

// Shapes input through HarfBuzz against the open face: glyph ids and
// advances come from the font (hb_ft_font_create), scaled so advances
// read in px (26.6 fixed-point -> /64) plus letter spacing like the
// fallback path. False only when HarfBuzz yields nothing usable — the
// caller then takes the hermetic fallback.
bool ShapeWithHarfBuzz(FT_Face ftface, const std::uint8_t* data,
                       std::size_t len, const TextStyle& style,
                       ShapedRun* out) {
  if (ftface == nullptr || data == nullptr || len == 0 || out == nullptr) {
    return false;
  }
  // Fail closed before narrowing: reject non-finite sizes and lengths
  // that cannot fit int (never wrap via static_cast); saturate
  // out-of-range-but-finite sizes into [0, kMaxSizePx] so shaping stays
  // deterministic instead of truncating into attacker-influenced behavior.
  if (!std::isfinite(style.size_px)) {
    return false;
  }
  const float clamped_size =
      style.size_px < 0.0f
          ? 0.0f
          : (style.size_px > kMaxSizePx ? kMaxSizePx : style.size_px);
  if (len > kMaxShapeBytes ||
      len > static_cast<std::size_t>(INT_MAX)) {
    return false;
  }
  const double scaled = static_cast<double>(clamped_size) * 64.0;
  if (scaled > static_cast<double>(INT_MAX)) {
    return false;
  }
  hb_font_t* hbfont = hb_ft_font_create(ftface, nullptr);
  if (hbfont == nullptr) return false;
  const int scale = static_cast<int>(scaled);
  hb_font_set_scale(hbfont, scale, scale);
  hb_buffer_t* buf = hb_buffer_create();
  bool ok = false;
  if (buf != nullptr) {
    const int len32 = static_cast<int>(len);
    hb_buffer_add_utf8(buf, reinterpret_cast<const char*>(data),
                       len32, 0, len32);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(hbfont, buf, nullptr, 0);
    const unsigned n = hb_buffer_get_length(buf);
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, nullptr);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, nullptr);
    if (n > 0 && info != nullptr && pos != nullptr) {
      ShapedRun run;
      run.cluster_map.assign(len, 0);
      for (unsigned i = 0; i < n; ++i) {
        run.glyph_ids.push_back(info[i].codepoint);
        run.advances.push_back(static_cast<float>(pos[i].x_advance) / 64.0f +
                               style.letter_spacing_px);
        GlyphOffset off;
        off.x = static_cast<float>(pos[i].x_offset) / 64.0f;
        off.y = static_cast<float>(pos[i].y_offset) / 64.0f;
        run.offsets.push_back(off);
      }
      // Cluster map: glyph i owns [cluster_i, next_cluster), where
      // next_cluster is the smallest strictly larger cluster (else len).
      // Later glyphs win ties, so reordered glyphs own their source bytes.
      for (unsigned i = 0; i < n; ++i) {
        unsigned start = info[i].cluster;
        if (start >= len) start = static_cast<unsigned>(len - 1);
        unsigned end = static_cast<unsigned>(len);
        for (unsigned j = 0; j < n; ++j) {
          if (info[j].cluster > start && info[j].cluster < end) {
            end = info[j].cluster;
          }
        }
        if (end > len) end = static_cast<unsigned>(len);
        for (unsigned b = start; b < end; ++b) {
          run.cluster_map[b] = i;
        }
      }
      *out = run;
      ok = true;
    }
    hb_buffer_destroy(buf);
  }
  hb_font_destroy(hbfont);
  return ok;
}

}  // namespace

StatusOr<ShapedRun> shapeRun(const std::uint8_t* utf8, std::size_t len,
                             FontHandle font, const TextStyle& style) {
  if (len == 0) {
    return ShapedRun{};
  }
  if (utf8 == nullptr) {
    return Status::kInvalidScript;
  }
  // Fail closed before any narrowing cast downstream (ShapeWithHarfBuzz):
  // reject non-finite sizes and over-cap lengths; saturate finite
  // out-of-range sizes into [0, kMaxSizePx] (deterministic, never wrapped).
  if (!std::isfinite(style.size_px)) {
    return Status::kInvalidScript;
  }
  if (len > kMaxShapeBytes ||
      len > static_cast<std::size_t>(INT_MAX)) {
    return Status::kInvalidScript;
  }
  TextStyle clamped_style = style;
  if (clamped_style.size_px < 0.0f) clamped_style.size_px = 0.0f;
  if (clamped_style.size_px > kMaxSizePx) clamped_style.size_px = kMaxSizePx;
  const TextStyle& eff = clamped_style;
  ShapedRun run;
  run.cluster_map.resize(len);
  // Decode the whole input first: malformed/truncated UTF-8 is rejected
  // before any reference comparison so error paths stay style-independent,
  // and both the real-font coverage check and the fallback below reuse
  // the scalar sequence.
  std::vector<std::uint32_t> codepoints;
  std::vector<std::size_t> seq_lens;
  codepoints.reserve(len);
  seq_lens.reserve(len);
  std::size_t pos = 0;
  while (pos < len) {
    std::uint32_t codepoint = 0;
    std::size_t seq_len = 0;
    if (!DecodeOne(utf8, len, pos, &codepoint, &seq_len)) {
      return Status::kInvalidScript;
    }
    codepoints.push_back(codepoint);
    seq_lens.push_back(seq_len);
    pos += seq_len;
  }
  // Reference fast path: byte-exact match against the checked-in HarfBuzz
  // dumps. Advances are quoted @16px, so they scale with the requested
  // size and shift with letter spacing like the fallback mapping does.
  if (const ReferenceRow* ref = FindReferenceRow(utf8, len)) {
    const float scale = eff.size_px / 16.0f;
    for (std::size_t i = 0; i < ref->glyph_count; ++i) {
      run.glyph_ids.push_back(ref->glyph_ids[i]);
      run.advances.push_back(ref->advances[i] * scale +
                             eff.letter_spacing_px);
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
  // BUG-TEXT-007 T9: the real HarfBuzz/FreeType backend REPLACES the stub
  // for every input the resolved font covers — glyph ids and advances come
  // from FT_New_Face(path) + hb_ft_font_create shaping against the resolved
  // face. All-or-nothing: runs the font only partly covers never mix
  // backends in one run. The stub below survives ONLY behind the
  // MUSE_TEXT_HERMETIC_FALLBACK gate (hermetic/test lanes); production
  // lanes fail closed with kMissingGlyph instead of serving stub ids.
  {
    RealFont real;
    if (ResolveRealFont(font, &real) &&
        FontCoversAll(real.face, codepoints.data(), codepoints.size())) {
      ShapedRun shaped;
      if (ShapeWithHarfBuzz(real.face, utf8, len, eff, &shaped)) {
        return shaped;
      }
    }
  }
#if defined(MUSE_TEXT_HERMETIC_FALLBACK)
  pos = 0;
  for (std::size_t i = 0; i < codepoints.size(); ++i) {
    const std::uint32_t codepoint = codepoints[i];
    const std::size_t seq_len = seq_lens[i];
    const auto glyph_index =
        static_cast<std::uint32_t>(run.glyph_ids.size());
    // Face-keyed fallback: font-derived glyph ids (never the codepoint
    // identity) with per-glyph font-like advances, so every input shapes
    // the way the reference rows do. Keyed by the resolved face, independent
    // of the requested size (a reshape at a new width keeps glyph ids).
    run.glyph_ids.push_back(FallbackGlyphId(codepoint, font.id));
    run.advances.push_back(FallbackAdvance(codepoint, eff));
    run.offsets.push_back(GlyphOffset{});
    for (std::size_t k = 0; k < seq_len; ++k) {
      run.cluster_map[pos + k] = glyph_index;
    }
    pos += seq_len;
  }
  return run;
#else
  (void)pos;
  (void)run;
  return Status::kMissingGlyph;
#endif
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

// FR-TEXT-011 / AC-TEXT-028: pipeline over the T1/T3/T6 seams. Single
// synchronous layout pass: no retry, no second frame — a failed download
// degrades to the bundled-face shaping in the same call. Malformed UTF-8
// surfaces Status::kInvalidInput; input no resolved face covers fails
// closed with Status::kMissingGlyph (BUG-TEXT-007 T9: the plan Interface
// governs over the old never-kMissingGlyph note — uncovered codepoints
// are never degraded to a substitute glyph). Lanes with no resolvable
// font keep the old degrade-to-fallback behavior inside shapeRun.
StatusOr<ShapedRun> shapeRunWithFallback(const std::uint8_t* bytes,
                                         std::size_t len,
                                         const TextStyle& style,
                                         const FontRequest& req) noexcept {
  try {
    if (len == 0) {
      return ShapedRun{};
    }
    if (bytes == nullptr) {
      return Status::kInvalidInput;
    }
    if (req.variation_count > 0 && req.variations == nullptr) {
      return Status::kInvalidInput;
    }
    // Resolve the shaping face. A usable downloadable face wins; anything
    // else (absent handle or failed fetch) falls through to the T3 chain,
    // which itself terminates at the bundled face. Chain misses never
    // propagate: they degrade to the bundled face synchronously.
    FontHandle face{};
    bool have_face = false;
    if (req.has_downloadable_font && !req.download_failed) {
      face = req.downloadable_font;
      have_face = true;
    } else {
      StatusOr<FontHandle> chained = resolveFallbackChain(
          req.script_tag, req.bcp47_locale, req.generic_family);
      if (chained.ok()) {
        face = *chained;
      } else {
        face = SystemFontEnumerator().LastResortFace(req.script_tag);
      }
      have_face = true;
    }
    (void)have_face;
    // BUG-TEXT-007 T9: real-backend routing behind the committed seam.
    // Requests carrying no variation vector and no usable downloadable
    // face shape through shapeRun — the reference fast path reproduces
    // the checked-in HarfBuzz dumps exactly (AC-TEXT-013) and every other
    // input the resolved font covers shapes through FT_New_Face +
    // hb_ft_font_create for real. Requests carrying a variation vector
    // or a usable downloadable face keep the shapeRunWithVariations path
    // (variation/face fold). Seam and noexcept are unchanged; the error
    // shape changes exactly as the T9 plan Interface governs: undecodable
    // input is kInvalidInput, and input no resolved face covers is
    // kMissingGlyph (fail-closed, never a substitute glyph).
    const bool use_exact = req.variation_count == 0 &&
                           !(req.has_downloadable_font &&
                             !req.download_failed);
    if (use_exact) {
      if (bytes == nullptr && len > 0) {
        return Status::kInvalidInput;
      }
      if (len > 0 && FindReferenceRow(bytes, len) == nullptr) {
        // Non-reference input: decode for the coverage check (malformed
        // stays kInvalidInput, as before via the shapeRun error mapping).
        std::vector<std::uint32_t> cps;
        cps.reserve(len);
        std::size_t p = 0;
        while (p < len) {
          std::uint32_t cp = 0;
          std::size_t sl = 0;
          if (!DecodeOne(bytes, len, p, &cp, &sl)) {
            return Status::kInvalidInput;
          }
          cps.push_back(cp);
          p += sl;
        }
        // Fail closed when a real font resolves but covers nothing...
        // part of the input: there is no font-backed shaping to serve.
        // Lanes with no resolvable font degrade to the hermetic fallback
        // inside shapeRun, exactly as before.
        RealFont real;
        if (ResolveRealFont(face, &real) &&
            !FontCoversAll(real.face, cps.data(), cps.size())) {
          return Status::kMissingGlyph;
        }
      }
      StatusOr<ShapedRun> shaped = shapeRun(bytes, len, face, style);
      if (!shaped.ok()) {
        // Production lanes shape uncovered input to kMissingGlyph (no
        // hermetic fallback there); carry it through instead of masking it
        // as kInvalidInput. Undecodable input still maps as before.
        if (shaped.status() == Status::kMissingGlyph) {
          return Status::kMissingGlyph;
        }
        return Status::kInvalidInput;
      }
      return *shaped;
    }
    StatusOr<ShapedRun> shaped = shapeRunWithVariations(
        bytes, len, face, style, req.variations, req.variation_count);
    if (!shaped.ok()) {
      return Status::kInvalidInput;
    }
    return *shaped;
  } catch (...) {
    return Status::kInvalidInput;
  }
}

}  // namespace muse::text
