// Gallery bring-up presenter (FR-RUNTIME-009 / FR-M3-007, AC-M3-015).
//
// Device reality: the JNI host entry carries the MUI1 tree asset, but no JS
// engine runs on device, so dynamic App.tsx expressions cannot evaluate.
// The static Showcase root (literal props + literal text children) carries
// the whole gallery with no evaluation — parse it here, lay it out through
// LayoutEngine (the in-repo flexbox-subset seam; a Yoga adapter sits behind
// it per layout.h), and rasterize pages of M3 rows into the ANativeWindow
// RGBA_8888 buffer. No View creation, no Java drawing: pixels stay native.
//
// Fail-closed throughout: bad envelope, bad JSON shape, or no rows yield an
// empty row set plus a diagnostic (the JNI layer then draws a diagnostic
// screen, never a blank one). Nothing here throws across the boundary.

#include "runtime/gallery_present.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "muse/text/font_discovery.h"
#include "muse/text/glyph_atlas.h"
#include "muse/text/rich_buffer.h"
#include "muse/text/shaper.h"
#include "runtime/layout.h"
#include "runtime/renderer.h"
#include "runtime/tree.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

// T62 C++ paint path — FR-M3-009 / AC-M3-020 (TC-M3-021), FR-M3-008 /
// AC-M3-018 (8/12/16) + AC-M3-019 (token-only).
//
// Renders the state-layer/ripple wash from the shared numeric map for the T61
// showcase state rows (`Surface` / `Ripple` / `TextFieldEditCore` x
// hover|focus|pressed|disabled x light/dark).
//
// Settled paint-path decisions (plan, 2026-10-31):
//   D1 (disabled baseline): `StateLayerOpacity("disabled")` reads the
//       `m3_matrix.json` baseline entry; the baseline pins 0.38
//       (tokenSnapshot 2026-10-28; 26/49 component rows carry
//       stateOpacities {hover 0.08, focus 0.12, pressed 0.16, disabled 0.38}),
//       so 0.38f is pinned explicitly here and named in the T62 test.
//   D2 (mirror equality): this map mirrors JS `driveState` 1:1
//       (`packages/native/index.js` STATE_OPACITY: hover 0.08 / focus 0.12 /
//       pressed 0.16 / disabled 0.38); JS stays the token-role source of
//       truth, the conformance test asserts literal equality (AC-M3-019).
//   D4 (wash treatment): primary-role wash for StateLayer families,
//       ripple-bounded wash for Ripple kind, theme-matched roles;
//       disabled = baseline treatment only. Wash *placement* is device-raster
//       behavior (owner tester, 127.0.0.1:6569 screencap deltas); this file
//       proves the numeric input plus the fail-closed edge.
//
// Fail-closed shape (never blank, never throws across JNI): unknown/empty
// state resolves to the 0.0f no-wash sentinel — the row is kept, no wash is
// painted, and no exception ever escapes (the body catches everything, so the
// JNI boundary stays exception-free).

#include <string>

namespace runtime {
namespace {

// ---- MUI1 envelope ----

bool ReadEnvelope(const void* data, std::size_t len, const char*& json,
                  std::size_t& jsonLen, std::string* error) {
  json = nullptr;
  jsonLen = 0;
  if (data == nullptr || len < 12) {
    if (error != nullptr) *error = "envelope too short";
    return false;
  }
  const unsigned char* b = static_cast<const unsigned char*>(data);
  if (std::memcmp(b, "MUI1", 4) != 0) {
    if (error != nullptr) *error = "bad magic (not MUI1)";
    return false;
  }
  if (b[4] != 1) {
    if (error != nullptr) *error = "unsupported envelope version";
    return false;
  }
  std::uint32_t declared = static_cast<std::uint32_t>(b[8]) |
                           (static_cast<std::uint32_t>(b[9]) << 8) |
                           (static_cast<std::uint32_t>(b[10]) << 16) |
                           (static_cast<std::uint32_t>(b[11]) << 24);
  if (declared == 0 || static_cast<std::uint64_t>(declared) + 12 > len) {
    if (error != nullptr) *error = "envelope length out of range";
    return false;
  }
  json = reinterpret_cast<const char*>(b + 12);
  jsonLen = declared;
  return true;
}

// ---- Minimal read-only JSON scan (string-aware, no input mutation) ----

const char* SkipWs(const char* p, const char* end) {
  while (p < end &&
         (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    ++p;
  }
  return p;
}

// Parse 4 hex digits at q (caller guarantees q[0..3] readable); false on
// non-hex input. Fail-closed helper for \uXXXX decoding below.
bool ParseHex4(const char* q, unsigned& code) {
  code = 0;
  for (int i = 0; i < 4; ++i) {
    char h = q[i];
    code <<= 4;
    if (h >= '0' && h <= '9') {
      code |= static_cast<unsigned>(h - '0');
    } else if (h >= 'a' && h <= 'f') {
      code |= static_cast<unsigned>(h - 'a' + 10);
    } else if (h >= 'A' && h <= 'F') {
      code |= static_cast<unsigned>(h - 'A' + 10);
    } else {
      return false;
    }
  }
  return true;
}

// Append the UTF-8 encoding of the scalar value cp (caller guarantees cp is
// not a surrogate). Surrogate halves never reach here: they fail closed in
// ParseJsonString below.
void AppendUtf8(std::string& out, unsigned cp) {
  if (cp < 0x80u) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800u) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp < 0x10000u) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

// Parse a JSON string at *p (expects '"'), appending the decoded value to
// out and returning the position past the closing quote (or nullptr).
const char* ParseJsonString(const char* p, const char* end, std::string& out) {
  if (p >= end || *p != '"') return nullptr;
  ++p;
  while (p < end) {
    char c = *p;
    if (c == '"') return p + 1;
    if (c != '\\') {
      out.push_back(c);
      ++p;
      continue;
    }
    ++p;
    if (p >= end) return nullptr;
    switch (*p) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'n': out.push_back('\n'); break;
      case 't': out.push_back('\t'); break;
      case 'r': out.push_back('\r'); break;
      case 'u': {
        // FR-TEXT-016 / AC-TEXT-046: full UTF-16 escape decoding with
        // surrogate-pair composition to UTF-8 so CJK/emoji reach the
        // shaper intact. Malformed input fails closed (nullptr drops the
        // row at the call site); never '?' substitution.
        if (p + 4 >= end) return nullptr;
        unsigned code = 0;
        if (!ParseHex4(p + 1, code)) return nullptr;
        p += 4;
        if (code >= 0xD800u && code <= 0xDBFFu) {
          // High surrogate: must be followed immediately by \uDC00-\uDFFF.
          if (p + 6 >= end || p[1] != '\\' || p[2] != 'u') return nullptr;
          unsigned low = 0;
          if (!ParseHex4(p + 3, low)) return nullptr;
          if (low < 0xDC00u || low > 0xDFFFu) return nullptr;
          code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
          p += 6;
        } else if (code >= 0xDC00u && code <= 0xDFFFu) {
          return nullptr;  // Lone low surrogate: fail closed, never '?'.
        }
        AppendUtf8(out, code);
        break;
      }
      default: out.push_back(*p); break;
    }
    ++p;
  }
  return nullptr;
}

// Find `"key"` then ':' starting at p; on success *valPos is the value start
// (after ':') and the key span is returned via spanEnd (past closing quote).
const char* FindKey(const char* p, const char* end, const char* key,
                    const char*& valPos) {
  std::size_t klen = std::strlen(key);
  for (; p < end; ++p) {
    if (*p != '"') continue;
    if (static_cast<std::size_t>(end - p) < klen + 2) return nullptr;
    if (std::memcmp(p + 1, key, klen) != 0 || p[1 + klen] != '"') continue;
    const char* q = SkipWs(p + 2 + klen, end);
    if (q >= end || *q != ':') continue;
    valPos = SkipWs(q + 1, end);
    return p;
  }
  return nullptr;
}

// Match the closing bracket for the opener at *p ('{' or '['), string-aware.
const char* MatchBracket(const char* p, const char* end) {
  if (p >= end || (*p != '{' && *p != '[')) return nullptr;
  char open = *p;
  char close = (open == '{') ? '}' : ']';
  int depth = 0;
  bool inStr = false;
  for (const char* q = p; q < end; ++q) {
    char c = *q;
    if (inStr) {
      if (c == '\\') {
        ++q;
        continue;
      }
      if (c == '"') inStr = false;
      continue;
    }
    if (c == '"') {
      inStr = true;
    } else if (c == open) {
      ++depth;
    } else if (c == close) {
      if (--depth == 0) return q;
    }
  }
  return nullptr;
}

// Split the top-level elements of the array starting at '[' (already
// located). Each element span [s, e) is appended for '{'-objects only.
bool SplitTopObjects(const char* arr, const char* end,
                     std::vector<std::pair<const char*, const char*>>& out) {
  if (arr >= end || *arr != '[') return false;
  const char* arrEnd = MatchBracket(arr, end);
  if (arrEnd == nullptr) return false;
  const char* q = arr + 1;
  while (q < arrEnd) {
    q = SkipWs(q, arrEnd);
    if (q >= arrEnd || *q == ']') break;
    if (*q == ',') {
      ++q;
      continue;
    }
    if (*q == '{') {
      const char* objEnd = MatchBracket(q, end);
      if (objEnd == nullptr || objEnd > arrEnd) return false;
      out.push_back({q, objEnd + 1});
      q = objEnd + 1;
      continue;
    }
    return false;  // unexpected element shape
  }
  return true;
}

// ---- Gallery shaping seam (FR-TEXT-016 / AC-TEXT-045/046) ----
//
// The gallery emission path shapes each label once through the engine text
// stack and emits a retained ShapedText/Text cmd carrying the shared cache
// handle (glyphRunId), never the row nodeId. Per the planner ruling on T5,
// this seam must shape via shapeRunWithFallback over the T2
// SystemFontEnumerator chain (with bundled last-resort fallback), cache the
// run in the T3 GlyphAtlas keyed (font, Density::fontScale, scale, glyphId)
// with T3 eviction semantics, and size with measureLine advances
// (HarfBuzz-parity authority) — never a runtime-local byte-keyed map and
// never TextWidth byte math. The interim scaffolding (exact-bytes map +
// code-point*6px metric) is removed below; the one residual local map
// (run-handle assignment keyed by the atlas key domain) remains only
// because the T3 atlas stores per-glyph texels while DrawCmd needs a
// uint32 run handle — see GalleryShaper.
//
// Flow per label: RichBuffer owns the emission text (plainText feeds the
// shaper) -> shapeRunWithFallback resolves the T2 fallback chain and
// shapes (variations where applicable) -> each shaped glyph's texel record
// (advance from the shaped run) is published to the T3 atlas under
// (face, kGalleryFontScale, glyphId); the atlas owns residency and LRU
// eviction -> the run handle is assigned from the atlas key domain
// (face id, scale bits, glyph-id sequence), so identical labels share one
// cached run id AND identical measureLine bounds across rows/frames.
// Fail-closed (RULE-RUNTIME-001): empty input, a shaping failure
// (kInvalidInput, e.g. malformed UTF-8), or a kCacheFull atlas insert
// yields run id 0 — the dangling sentinel the retained ShapedText path
// rejects with UnknownPrimitive — and PresentGallery converts a 0 handle
// on a non-empty label into a diagnostic (never a gallery bitmap).

// Gallery text metrics in the engine stack. The gallery paints at the
// kScale device factor; the atlas/fontScale key dimension carries that same
// factor so distinct gallery font scales are distinct atlas keys (per the
// T3 GlyphAtlas contract), and the shaping size tracks it through the
// engine (advances scale in the shaper, not in gallery byte math).
constexpr float kGalleryFontScale = 2.0f;  // == kScale below.
constexpr float kGalleryTextSizePx = 16.0f;  // Engine reference size.
constexpr float kGalleryMeasureBudgetPx = 100000.0f;

// Bit pattern of the gallery atlas scale key (mirrors the T3 MakeKey
// float-bits convention; MakeKey itself is atlas-private).
std::uint32_t GalleryScaleBits(float scale) {
  static_assert(sizeof(float) == sizeof(std::uint32_t),
                "gallery atlas key needs 32-bit float bits");
  std::uint32_t bits = 0;
  std::memcpy(&bits, &scale, sizeof(bits));
  return bits;
}

// Run-handle index over the atlas key domain: (face id, atlas scale bits,
// shaped glyph-id sequence). Keyed by shape identity under the resolved
// chain face — never by exact label bytes — so labels that shape
// identically share one run; the per-glyph texels and LRU eviction live
// in the T3 atlas itself. This map assigns uint32 handles only because
// DrawCmd carries a uint32 glyphRunId while the atlas stores glyphs.
struct GalleryRunKey {
  std::uint64_t font_id = 0;
  std::uint32_t scale_bits = 0;
  std::vector<std::uint32_t> glyphs;
  bool operator<(const GalleryRunKey& other) const {
    if (font_id != other.font_id) return font_id < other.font_id;
    if (scale_bits != other.scale_bits)
      return scale_bits < other.scale_bits;
    return glyphs < other.glyphs;
  }
};

struct GalleryShaper {
  muse::text::GlyphAtlas atlas;
  std::map<GalleryRunKey, std::uint32_t> runs;
  // T11 coverage seam: shaped glyph data per assigned handle, so
  // RasterizeShapedText resolves real runs (no synthesis, no second shape).
  std::map<std::uint32_t, muse::text::ShapedRun> runData;
  std::uint32_t next = 0x1000u;
};

GalleryShaper& GalleryShaperState() {
  static GalleryShaper state;
  return state;
}

// The shaping face for gallery labels: the T2 fallback chain terminus for
// the gallery request (script-agnostic request, bundled last-resort
// fallback). Resolved here for the atlas key; shapeRunWithFallback
// resolves the same chain internally on the shaping leg, so the key face
// and the shaping face agree.
muse::text::FontHandle GalleryChainFace() {
  muse::text::StatusOr<muse::text::FontHandle> chained =
      muse::text::resolveFallbackChain(0, nullptr, nullptr);
  if (chained.ok()) return *chained;
  // kNoFaceAvailable analogue: the bundled terminus is always present, so
  // this is belt-and-braces; callers still fail closed on a zero id.
  return muse::text::SystemFontEnumerator().LastResortFace(0);
}

// Shape one label through the engine stack. Returns false on empty input,
// malformed UTF-8 (kInvalidInput), or any engine failure: the caller must
// fail closed (0 handle -> diagnostic, never a bitmap). Never throws.
bool ShapeLabelThroughEngine(const std::string& label,
                             muse::text::ShapedRun* run,
                             muse::text::FontHandle* face) {
  if (label.empty() || run == nullptr) return false;
  try {
    // RichBuffer owns the emission text; its plainText feeds the shaper.
    muse::text::RichBuffer buffer;
    buffer.insert(0, label);
    const std::string text = buffer.plainText();
    if (text.empty()) return false;
    muse::text::TextStyle style;
    style.size_px = kGalleryTextSizePx;
    // Script-agnostic gallery request: no variations, no downloadable font,
    // so the run shapes over the T2 SystemFontEnumerator chain ending at
    // the bundled last-resort face. Only malformed UTF-8 is an error.
    muse::text::FontRequest req;
    muse::text::StatusOr<muse::text::ShapedRun> shaped =
        muse::text::shapeRunWithFallback(
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size(),
            style, req);
    if (!shaped.ok() || shaped->glyph_ids.empty()) return false;
    *run = *shaped;
    if (face != nullptr) *face = GalleryChainFace();
    return true;
  } catch (...) {
    return false;
  }
}

// measureLine sizing for gallery layout: the width is the engine-measured
// advance sum of the shaped run (HarfBuzz-parity authority), scaled through
// the engine text size for the gallery device factor. Replaces TextWidth
// byte math at every gallery layout call site (pill widths, centering).
// Unshapable input measures 0; callers clamp to their minima.
int MeasureGalleryLabelWidth(const std::string& label, int scale) {
  if (label.empty()) return 0;
  float size_px = kGalleryTextSizePx;
  if (scale > 0) {
    size_px = kGalleryTextSizePx * static_cast<float>(scale) /
              kGalleryFontScale;
  }
  muse::text::ShapedRun run;
  muse::text::FontHandle face;
  try {
    muse::text::RichBuffer buffer;
    buffer.insert(0, label);
    const std::string text = buffer.plainText();
    if (text.empty()) return 0;
    muse::text::TextStyle style;
    style.size_px = size_px;
    muse::text::FontRequest req;
    muse::text::StatusOr<muse::text::ShapedRun> shaped =
        muse::text::shapeRunWithFallback(
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size(),
            style, req);
    if (!shaped.ok()) return 0;
    run = *shaped;
  } catch (...) {
    return 0;
  }
  const muse::text::MeasuredLine measured = muse::text::measureLine(
      muse::text::ShapedRunView(run), kGalleryMeasureBudgetPx);
  if (!(measured.width_px > 0.0f)) return 0;
  return static_cast<int>(std::ceil(measured.width_px));
}

// ShapeGalleryLabel: label bytes -> shared nonzero glyph-run handle.
//
// The handle is assigned from the T3 atlas key domain (chain face,
// fontScale, shaped glyph-id sequence): identical labels shape identically
// and resolve to one cached run (atlas reuse across rows and frames);
// distinct shapes never collide within the process. Each shaped glyph's
// texel record (advance from the engine run) is published to the T3
// GlyphAtlas under (font, Density::fontScale, scale, glyphId), so
// residency and LRU eviction follow T3 bound semantics; a kCacheFull
// insert fails closed. Empty input (the kNoFaceAvailable/kShapingFailed
// analogue on this boundary) yields the dangling sentinel 0: the caller
// emits a fail-closed diagnostic, never a gallery bitmap.
std::uint32_t ShapeGalleryLabel(const std::string& label) {
  if (label.empty()) return 0;
  muse::text::ShapedRun run;
  muse::text::FontHandle face;
  if (!ShapeLabelThroughEngine(label, &run, &face)) return 0;
  GalleryShaper& shaper = GalleryShaperState();
  for (std::size_t i = 0; i < run.glyph_ids.size(); ++i) {
    muse::text::TexelRecord texels;
    texels.glyph_id = run.glyph_ids[i];
    texels.advance_px =
        i < run.advances.size() ? run.advances[i] : 0.0f;
    if (shaper.atlas.insert(face, kGalleryFontScale, run.glyph_ids[i],
                            texels) != muse::text::Status::kOk) {
      return 0;  // Atlas bound breach past evict-and-retry: fail closed.
    }
  }
  GalleryRunKey key;
  key.font_id = face.id;
  key.scale_bits = GalleryScaleBits(kGalleryFontScale);
  key.glyphs = run.glyph_ids;
  const auto it = shaper.runs.find(key);
  if (it != shaper.runs.end()) return it->second;
  std::uint32_t id = shaper.next;
  ++shaper.next;
  if (shaper.next == 0u) shaper.next = 0x1000u;  // Never hand out 0.
  shaper.runs.emplace(std::move(key), id);
  shaper.runData.emplace(id, run);
  return id;
}

// Row-admission predicate: gallery labels start with a letter or digit.
// ASCII fast path preserves the structural-lowerer filter (stray closers
// such as "});" never become rows); non-ASCII lead bytes decode the first
// UTF-8 sequence and admit valid CJK/emoji-first labels byte-exact.
bool IsGalleryLabelStart(const std::string& value) {
  if (value.empty()) return false;
  unsigned char first = static_cast<unsigned char>(value[0]);
  if ((first & 0x80u) == 0u) {
    return (first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') ||
           (first >= '0' && first <= '9');
  }
  std::size_t len = value.size();
  if ((first & 0xE0u) == 0xC0u) {
    return len >= 2 && (static_cast<unsigned char>(value[1]) & 0xC0u) == 0x80u;
  }
  if ((first & 0xF0u) == 0xE0u) {
    return len >= 3 && (static_cast<unsigned char>(value[1]) & 0xC0u) == 0x80u &&
           (static_cast<unsigned char>(value[2]) & 0xC0u) == 0x80u;
  }
  if ((first & 0xF8u) == 0xF0u) {
    return len >= 4 && (static_cast<unsigned char>(value[1]) & 0xC0u) == 0x80u &&
           (static_cast<unsigned char>(value[2]) & 0xC0u) == 0x80u &&
           (static_cast<unsigned char>(value[3]) & 0xC0u) == 0x80u;
  }
  return false;  // Stray continuation byte: not a label start.
}

// ---- 5x7 bitmap font (ASCII subset, mixed case) ----

struct Glyph {
  char ch;
  const char* rows[7];
};

const Glyph kGlyphs[] = {
    {' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
    {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
    {'"', {".#.#.", ".#.#.", ".....", ".....", ".....", ".....", "....."}},
    {'%', {"##..#", "##..#", "..#..", "..#..", "..#..", ".#..#", ".#..#"}},
    {'\'', {"..#..", "..#..", ".....", ".....", ".....", ".....", "....."}},
    {'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {',', {".....", ".....", ".....", ".....", "..#..", "..#..", ".#..."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'.', {".....", ".....", ".....", ".....", ".....", "..#..", "..#.."}},
    {'/', {"....#", "...#.", "...#.", "..#..", "..#..", ".#...", "#...."}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", "#####"}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
    {':', {".....", "..#..", ".....", ".....", ".....", "..#..", "..#.."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".####", "#....", "#....", "#....", "#....", "#....", ".####"}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".####", "#....", "#....", "#.###", "#...#", "#...#", ".###."}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "#####", "#####", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "##..#", "##..#", "#.#.#", "#..##", "#..##", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#####", "#####", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'a', {".....", ".....", ".###.", "....#", ".####", "#...#", ".####"}},
    {'b', {"#....", "#....", "#.###", "##..#", "#...#", "#...#", "#.###"}},
    {'c', {".....", ".....", ".####", "#....", "#....", "#....", ".####"}},
    {'d', {"....#", "....#", ".####", "#...#", "#...#", "#...#", ".####"}},
    {'e', {".....", ".....", ".###.", "#...#", "#####", "#....", ".###."}},
    {'f', {"...##", "...#.", "...#.", "#####", "...#.", "...#.", "...#."}},
    {'g', {".....", ".....", ".###.", "#...#", "#...#", ".####", "....#"}},
    {'h', {"#....", "#....", "#.###", "##..#", "#...#", "#...#", "#...#"}},
    {'i', {"..#..", ".....", ".##..", "..#..", "..#..", "..#..", ".###."}},
    {'j', {"....#", ".....", "...##", "...#.", "...#.", "#...#", ".###."}},
    {'k', {"#....", "#....", "#..#.", "#.#..", "##...", "#..#.", "#..#."}},
    {'l', {".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'m', {".....", ".....", "##.#.", "#.#.#", "#...#", "#...#", "#...#"}},
    {'n', {".....", ".....", "#.###", "##..#", "#...#", "#...#", "#...#"}},
    {'o', {".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###."}},
    {'p', {".....", ".....", "#.###", "##..#", "##..#", "#.###", "#...."}},
    {'q', {".....", ".....", ".##.#", "#...#", "#...#", ".###.", "....#"}},
    {'r', {".....", ".....", "#.###", "##..#", "#....", "#....", "#...."}},
    {'s', {".....", ".....", ".####", "#....", ".###.", "....#", "####."}},
    {'t', {"...#.", "...#.", "#####", "...#.", "...#.", "...#.", "..##."}},
    {'u', {".....", ".....", "#...#", "#...#", "#...#", "#...#", ".####"}},
    {'v', {".....", ".....", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'w', {".....", ".....", "#...#", "#...#", "#.#.#", "#####", ".#.#."}},
    {'x', {".....", ".....", "#..#.", "#..#.", ".##..", "#..#.", "#..#."}},
    {'y', {".....", ".....", "#...#", "#...#", ".###.", "....#", ".###."}},
    {'z', {".....", ".....", "#####", "...#.", "..#..", ".#...", "#####"}},
};

const Glyph* FindGlyph(char c) {
  for (const Glyph& g : kGlyphs) {
    if (g.ch == c) return &g;
  }
  for (const Glyph& g : kGlyphs) {
    if (g.ch == '?') return &g;
  }
  return nullptr;
}

// ---- M3 baseline roles (light + dark) ----
// ANativeWindow RGBA_8888 orders bytes R,G,B,A in memory, so on
// little-endian the uint32 word reads AABBGGRR, never 0xAARRGGBB. Every
// role below goes through RGBA() to stay correct on the wire.
constexpr std::uint32_t RGBA(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                             std::uint8_t a) {
  return static_cast<std::uint32_t>(r) |
         (static_cast<std::uint32_t>(g) << 8) |
         (static_cast<std::uint32_t>(b) << 16) |
         (static_cast<std::uint32_t>(a) << 24);
}

constexpr std::uint32_t kLightSurface = RGBA(0xFE, 0xF7, 0xFF, 0xFF);
constexpr std::uint32_t kLightOnSurface = RGBA(0x1D, 0x1B, 0x20, 0xFF);
constexpr std::uint32_t kLightPrimary = RGBA(0x67, 0x50, 0xA4, 0xFF);
constexpr std::uint32_t kLightOnPrimary = RGBA(0xFF, 0xFF, 0xFF, 0xFF);
constexpr std::uint32_t kDarkSurface = RGBA(0x14, 0x12, 0x18, 0xFF);
constexpr std::uint32_t kDarkOnSurface = RGBA(0xE6, 0xE0, 0xE9, 0xFF);
constexpr std::uint32_t kDarkPrimary = RGBA(0xD0, 0xBC, 0xFF, 0xFF);
constexpr std::uint32_t kDarkOnPrimary = RGBA(0x38, 0x1E, 0x72, 0xFF);
constexpr std::uint32_t kFooterBg = RGBA(0x1D, 0x1B, 0x20, 0xFF);
constexpr std::uint32_t kFooterFg = RGBA(0xFF, 0xFF, 0xFF, 0xFF);
constexpr std::uint32_t kErrorBg = RGBA(0xBA, 0x1A, 0x1A, 0xFF);

// ---- Raster helpers ----
//
// The 5x7 bitmap text below survives solely as the fail-closed
// diagnostic-screen path (RasterGalleryPage's empty-rows branch names the
// failure in plain text via DrawText). No gallery page painter calls it:
// gallery copy rides the retained ShapedText path (FR-TEXT-016 AC-TEXT-045,
// "no bitmap fallback on gallery pages").

void FillRect(std::uint32_t* px, int stride, int w, int h, int x, int y, int rw,
              int rh, std::uint32_t color) {
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + rw > w ? w : x + rw;
  int y1 = y + rh > h ? h : y + rh;
  for (int yy = y0; yy < y1; ++yy) {
    std::uint32_t* row = px + yy * stride;
    for (int xx = x0; xx < x1; ++xx) row[xx] = color;
  }
}

void DrawText(std::uint32_t* px, int stride, int w, int h, const char* s,
              int x, int y, int scale, std::uint32_t color) {
  int cx = x;
  for (const char* p = s; *p != '\0'; ++p) {
    const Glyph* g = FindGlyph(*p);
    if (g == nullptr) {
      cx += 6 * scale;
      continue;
    }
    for (int r = 0; r < 7; ++r) {
      for (int c = 0; c < 5; ++c) {
        if (g->rows[r][c] != '#') continue;
        FillRect(px, stride, w, h, cx + c * scale, y + r * scale, scale,
                 scale, color);
      }
    }
    cx += 6 * scale;
  }
}

// Src-over blend of a solid RGBA source over the buffer (for soft shadows;
// the buffer itself stays opaque).
void BlendPixel(std::uint32_t* px, int stride, int w, int h, int x, int y,
                std::uint8_t r, std::uint8_t g, std::uint8_t b,
                std::uint8_t a) {
  if (x < 0 || y < 0 || x >= w || y >= h) return;
  std::uint32_t dst = px[y * stride + x];
  unsigned dr = dst & 0xFFu;
  unsigned dg = (dst >> 8) & 0xFFu;
  unsigned db = (dst >> 16) & 0xFFu;
  unsigned ia = 255u - a;
  unsigned nr = (r * a + dr * ia) / 255u;
  unsigned ng = (g * a + dg * ia) / 255u;
  unsigned nb = (b * a + db * ia) / 255u;
  px[y * stride + x] = nr | (ng << 8) | (nb << 16) | 0xFF000000u;
}

void ShadowRounded(std::uint32_t* px, int stride, int w, int h, int x, int y,
                   int rw, int rh, int r) {
  // Two offset soft passes approximate an M3 elevation shadow.
  for (int pass = 0; pass < 2; ++pass) {
    int ox = x + 2 + pass * 2;
    int oy = y + 3 + pass * 3;
    std::uint8_t a = pass == 0 ? 28 : 18;
    for (int yy = oy; yy < oy + rh; ++yy) {
      for (int xx = ox; xx < ox + rw; ++xx) {
        // Keep the rounded silhouette: skip outside the rounded shape.
        int dx = xx < ox + r ? (ox + r - xx)
                 : xx >= ox + rw - r ? (xx - (ox + rw - r) + 1)
                                     : 0;
        int dy = yy < oy + r ? (oy + r - yy)
                 : yy >= oy + rh - r ? (yy - (oy + rh - r) + 1)
                                     : 0;
        if (dx * dx + dy * dy > r * r) continue;
        BlendPixel(px, stride, w, h, xx, yy, 0, 0, 0, a);
      }
    }
  }
}

void FillCircle(std::uint32_t* px, int stride, int w, int h, int cx, int cy,
                int r, std::uint32_t color) {
  if (r <= 0) return;
  for (int yy = cy - r; yy <= cy + r; ++yy) {
    for (int xx = cx - r; xx <= cx + r; ++xx) {
      int dx = xx - cx;
      int dy = yy - cy;
      if (dx * dx + dy * dy <= r * r) {
        if (xx >= 0 && yy >= 0 && xx < w && yy < h) {
          px[yy * stride + xx] = color;
        }
      }
    }
  }
}

void RingCircle(std::uint32_t* px, int stride, int w, int h, int cx, int cy,
                int r, int thick, std::uint32_t color) {
  if (r <= 0 || thick <= 0) return;
  int inner = r - thick;
  if (inner < 0) inner = 0;
  for (int yy = cy - r; yy <= cy + r; ++yy) {
    for (int xx = cx - r; xx <= cx + r; ++xx) {
      int d2 = (xx - cx) * (xx - cx) + (yy - cy) * (yy - cy);
      if (d2 <= r * r && d2 >= inner * inner) {
        if (xx >= 0 && yy >= 0 && xx < w && yy < h) {
          px[yy * stride + xx] = color;
        }
      }
    }
  }
}

void FillRoundedRect(std::uint32_t* px, int stride, int w, int h, int x,
                     int y, int rw, int rh, int r, std::uint32_t color) {
  if (rw <= 0 || rh <= 0) return;
  int maxR = rw < rh ? rw : rh;
  maxR /= 2;
  if (r > maxR) r = maxR;
  if (r < 0) r = 0;
  if (r == 0) {
    FillRect(px, stride, w, h, x, y, rw, rh, color);
    return;
  }
  FillRect(px, stride, w, h, x + r, y, rw - 2 * r, rh, color);
  FillRect(px, stride, w, h, x, y + r, rw, rh - 2 * r, color);
  for (int q = 0; q < 4; ++q) {
    for (int dy = 0; dy < r; ++dy) {
      for (int dx = 0; dx < r; ++dx) {
        int ddx = (q % 2 == 0) ? (r - dx) : (dx + 1);
        int ddy = (q < 2) ? (r - dy) : (dy + 1);
        if (ddx * ddx + ddy * ddy > r * r) continue;
        int xx = (q % 2 == 0) ? (x + dx) : (x + rw - r + dx);
        int yy = (q < 2) ? (y + dy) : (y + rh - r + dy);
        if (xx >= 0 && yy >= 0 && xx < w && yy < h) {
          px[yy * stride + xx] = color;
        }
      }
    }
  }
}

void StrokeRoundedRect(std::uint32_t* px, int stride, int w, int h, int x,
                       int y, int rw, int rh, int r, int t,
                       std::uint32_t color, std::uint32_t bg) {
  if (t < 1) t = 1;
  FillRoundedRect(px, stride, w, h, x, y, rw, rh, r, color);
  FillRoundedRect(px, stride, w, h, x + t, y + t, rw - 2 * t, rh - 2 * t,
                  r > t ? r - t : 0, bg);
}

void DrawLine(std::uint32_t* px, int stride, int w, int h, int x0, int y0,
              int x1, int y1, int thick, std::uint32_t color) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int steps = dx < 0 ? -dx : dx;
  int sy = dy < 0 ? -dy : dy;
  if (sy > steps) steps = sy;
  if (steps == 0) {
    FillRect(px, stride, w, h, x0 - thick / 2, y0 - thick / 2, thick, thick,
             color);
    return;
  }
  for (int i = 0; i <= steps; ++i) {
    int xx = x0 + dx * i / steps;
    int yy = y0 + dy * i / steps;
    FillRect(px, stride, w, h, xx - thick / 2, yy - thick / 2, thick, thick,
             color);
  }
}

void DrawArc(std::uint32_t* px, int stride, int w, int h, int cx, int cy,
             int r, float startDeg, float sweepDeg, int thick,
             std::uint32_t color) {
  if (r <= 0 || thick <= 0 || sweepDeg <= 0) return;
  constexpr float kPi = 3.14159265f;
  int steps = static_cast<int>(sweepDeg * 2.0f);
  if (steps < 8) steps = 8;
  if (steps > 720) steps = 720;
  for (int i = 0; i <= steps; ++i) {
    float ang = (startDeg + sweepDeg * i / steps) * kPi / 180.0f;
    int xx = cx + static_cast<int>(r * std::cos(ang));
    int yy = cy + static_cast<int>(r * std::sin(ang));
    FillCircle(px, stride, w, h, xx, yy, thick / 2 + 1, color);
  }
}

}  // namespace

std::vector<GalleryRow> ParseGalleryRows(const void* data, std::size_t len,
                                         std::string* error) {
  std::vector<GalleryRow> rows;
  try {
    const char* json = nullptr;
    std::size_t jsonLen = 0;
    if (!ReadEnvelope(data, len, json, jsonLen, error)) return rows;
    const char* begin = json;
    const char* end = json + jsonLen;

    // Locate the roots array, then split its top-level root objects.
    const char* rootsVal = nullptr;
    if (FindKey(begin, end, "roots", rootsVal) == nullptr || rootsVal >= end ||
        *rootsVal != '[') {
      if (error != nullptr) *error = "IR has no roots array";
      return rows;
    }
    std::vector<std::pair<const char*, const char*>> rootObjs;
    if (!SplitTopObjects(rootsVal, end, rootObjs) || rootObjs.empty()) {
      if (error != nullptr) *error = "IR roots array is empty/unreadable";
      return rows;
    }

    // Prefer the static Showcase root; otherwise the first root.
    std::pair<const char*, const char*> chosen = rootObjs[0];
    for (const auto& span : rootObjs) {
      const char* fileVal = nullptr;
      if (FindKey(span.first, span.second, "file", fileVal) != nullptr &&
          fileVal < span.second && *fileVal == '"') {
        std::string file;
        if (ParseJsonString(fileVal, span.second, file) != nullptr &&
            file == "src/Showcase.tsx") {
          chosen = span;
          break;
        }
      }
    }

    // Split the chosen root's children array into child objects.
    const char* childrenVal = nullptr;
    if (FindKey(chosen.first, chosen.second, "children", childrenVal) ==
            nullptr ||
        childrenVal >= chosen.second || *childrenVal != '[') {
      if (error != nullptr) *error = "Showcase root has no children array";
      return rows;
    }
    std::vector<std::pair<const char*, const char*>> childObjs;
    if (!SplitTopObjects(childrenVal, chosen.second, childObjs)) {
      if (error != nullptr) *error = "Showcase children unreadable";
      return rows;
    }

    std::vector<std::string> textRows;
    std::vector<GalleryRow> componentRows;
    bool dark = false;
    for (const auto& span : childObjs) {
      const char* typeVal = nullptr;
      if (FindKey(span.first, span.second, "type", typeVal) == nullptr ||
          typeVal >= span.second || *typeVal != '"') {
        continue;
      }
      std::string type;
      if (ParseJsonString(typeVal, span.second, type) == nullptr) continue;
      if (type == "#text") {
        const char* textVal = nullptr;
        if (FindKey(span.first, span.second, "value", textVal) == nullptr ||
            textVal >= span.second || *textVal != '"') {
          continue;
        }
        std::string value;
        if (ParseJsonString(textVal, span.second, value) == nullptr) continue;
        // Structural lowerer leaks source lines (comments, code) as text;
        // only literal gallery copy becomes rows. Gallery labels start
        // with a letter or digit (IsGalleryLabelStart admits Unicode
        // letters so CJK/emoji-first labels survive byte-exact); stray
        // closers like "});" do not.
        if (!IsGalleryLabelStart(value)) continue;
        if (value == "M3 Gallery dark") dark = true;
        if (value == "M3 Gallery light") dark = false;
        GalleryRow row;
        row.label = value;
        row.dark = dark;
        row.header =
            value.compare(0, 10, "M3 Gallery") == 0;
        // Stash header state in the prefix: emit via ordered vectors below.
        if (row.header) {
          textRows.push_back(std::string("\x01") + value);
        } else {
          textRows.push_back(value);
        }
      } else {
        // Structural wrappers carry no visual props: <View> only groups,
        // <Text> tags hold expression children whose literal copy already
        // became #text rows. Rendering them would repeat ~100 identical
        // fallback cards, so they never become rows.
        if (type == "View" || type == "Text") continue;
        // Component inventory row from literal props only. Every literal
        // string prop is kept (quoted, brace-free so no evaluated
        // expression); the painter below dispatches on the type and reads
        // variant/label/size props back out of the stored description.
        const char* kPropKeys[] = {"label",   "title",    "headline",
                                  "message", "text",     "placeholder",
                                  "name",    "variant",  "size",
                                  "selected", "checked", "enabled",
                                  "value",   "index",    "icon",
                                  "tone",    "elevation", "orientation",
                                  "direction", "spacing", "open",
                                  "contentFit", "source", "state",
                                  "theme"};
        const char* propsVal = nullptr;
        const char* propsEnd = nullptr;
        if (FindKey(span.first, span.second, "props", propsVal) != nullptr &&
            propsVal < span.second && *propsVal == '{') {
          propsEnd = MatchBracket(propsVal, span.second);
        }
        std::string desc = type;
        std::string picked;
        if (propsEnd != nullptr) {
          int kept = 0;
          for (const char* key : kPropKeys) {
            if (kept >= 10) break;
            const char* kv = nullptr;
            if (FindKey(propsVal, propsEnd + 1, key, kv) == nullptr ||
                kv > propsEnd || *kv != '"') {
              continue;
            }
            std::string v;
            const char* vEnd = ParseJsonString(kv, propsEnd + 1, v);
            if (vEnd == nullptr || vEnd > propsEnd) continue;
            if (v.find('{') != std::string::npos ||
                v.find('}') != std::string::npos) {
              continue;
            }
            desc += " ";
            desc += key;
            desc += "=";
            desc += v;
            if (picked.empty() &&
                (std::strcmp(key, "label") == 0 ||
                 std::strcmp(key, "title") == 0 ||
                 std::strcmp(key, "headline") == 0 ||
                 std::strcmp(key, "message") == 0 ||
                 std::strcmp(key, "text") == 0 ||
                 std::strcmp(key, "placeholder") == 0 ||
                 std::strcmp(key, "name") == 0)) {
              picked = v;
            }
            ++kept;
          }
        }
        if (desc.size() > 160) {
          desc.resize(160);
        }
        GalleryRow row;
        row.comp = type;
        row.label = picked.empty() ? desc : picked;
        row.desc = desc;
        // T61 theme literal: "theme=dark" marks dark-section state rows so
        // light/dark halves stay distinguishable downstream (D4
        // theme-matched wash). Absent/unknown theme fails closed to light.
        row.dark = (desc.find(" theme=dark") != std::string::npos);
        row.header = false;
        componentRows.push_back(row);
      }
    }

    // Component inventory first (the actual M3 gallery: every tier with its
    // documented variants), then the literal label sections. Text rows keep
    // their light/dark section order from the source.
    for (const GalleryRow& c : componentRows) {
      if (rows.size() >= 400) break;
      rows.push_back(c);
    }
    dark = false;
    for (const std::string& t : textRows) {
      GalleryRow row;
      if (!t.empty() && t[0] == '\x01') {
        row.label = t.substr(1);
        row.header = true;
        row.dark = (row.label == "M3 Gallery dark");
        dark = row.dark;
        if (row.label == "M3 Gallery light") dark = false;
      } else {
        row.label = t;
        row.dark = dark;
        row.header = false;
      }
      rows.push_back(row);
      if (rows.size() >= 400) break;
    }
    if (rows.empty() && error != nullptr) {
      *error = "Showcase root produced no gallery rows";
    }
    return rows;
  } catch (...) {
    rows.clear();
    if (error != nullptr) *error = "row parse failed (exception)";
    return rows;
  }
}

// Extra M3 roles used by the component painters below.
constexpr std::uint32_t kLightOnPrimaryContainer = RGBA(0x4F, 0x37, 0x8B, 0xFF);
constexpr std::uint32_t kLightSecondaryContainer = RGBA(0xE8, 0xDE, 0xF8, 0xFF);
constexpr std::uint32_t kLightTertiaryContainer = RGBA(0xFF, 0xD8, 0xE4, 0xFF);
constexpr std::uint32_t kLightOutline = RGBA(0x79, 0x74, 0x7E, 0xFF);
constexpr std::uint32_t kLightOutlineVariant = RGBA(0xCA, 0xC4, 0xD0, 0xFF);
constexpr std::uint32_t kLightSurfaceVariant = RGBA(0xE7, 0xE0, 0xEC, 0xFF);
constexpr std::uint32_t kLightOnSurfaceVariant = RGBA(0x49, 0x45, 0x4F, 0xFF);
constexpr std::uint32_t kLightError = RGBA(0xBA, 0x1A, 0x1A, 0xFF);
constexpr std::uint32_t kLightInverseSurface = RGBA(0x32, 0x2F, 0x35, 0xFF);

constexpr int kPad = 16;
constexpr int kGap = 8;
constexpr int kHeaderH = 84;
constexpr int kFooterH = 56;
constexpr int kScale = 2;

// Row heights by row kind. Text rows stay compact; every component row is
// tall enough for the drawn control plus its caption.
int RowHeight(const GalleryRow& row) {
  if (row.header) return 84;
  if (row.comp.empty()) return 64;
  const std::string& c = row.comp;
  const std::string& d = row.desc;
  if (c == "TopAppBar") {
    if (d.find("large") != std::string::npos) return 236;
    if (d.find("medium") != std::string::npos) return 196;
    return 156;
  }
  if (c == "Card" || c == "Dialog") return 190;
  if (c == "BottomSheet" || c == "NavigationDrawer") return 180;
  if (c == "Menu" || c == "Carousel") return 170;
  if (c == "ListItem") return 128;
  if (c == "NavigationBar" || c == "NavigationRail" ||
      c == "BottomAppBar" || c == "SearchBar" || c == "SnackbarHost" ||
      c == "Slider" || c == "LinearProgress" || c == "CircularProgress") {
    return 124;
  }
  return 104;
}

// Literal-prop lookup over the stored "Type k=v k=v" description.
bool PropIs(const std::string& desc, const char* key, const char* val) {
  std::string needle = std::string(" ") + key + "=" + val;
  return desc.find(needle) != std::string::npos;
}

std::string PropValue(const std::string& desc, const char* key) {
  std::string needle = std::string(" ") + key + "=";
  std::size_t at = desc.find(needle);
  if (at == std::string::npos) return "";
  at += needle.size();
  std::size_t end = desc.find(' ', at);
  if (end == std::string::npos) return desc.substr(at);
  return desc.substr(at, end - at);
}

double PropDouble(const std::string& desc, const char* key, double fallback) {
  std::string v = PropValue(desc, key);
  if (v.empty()) return fallback;
  try {
    return std::stod(v);
  } catch (...) {
    return fallback;
  }
}

// Greedy page packing over variable row heights. Returns the start index of
// every page; page i covers [starts[i], starts[i+1]).
std::vector<int> PageStarts(const std::vector<GalleryRow>& rows, int height) {
  std::vector<int> starts;
  starts.push_back(0);
  int avail = height - kHeaderH - kFooterH - kPad;
  int used = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    int h = RowHeight(rows[i]);
    int need = (used == 0) ? h : h + kGap;
    if (used != 0 && used + need > avail) {
      starts.push_back(static_cast<int>(i));
      used = h;
    } else {
      used += need;
    }
  }
  starts.push_back(static_cast<int>(rows.size()));
  return starts;
}

int GalleryPageCount(const std::vector<GalleryRow>& rows, int height) {
  if (rows.empty()) return 1;
  if (height <= kHeaderH + kFooterH + 64) return 1;
  std::vector<int> starts = PageStarts(rows, height);
  int pages = static_cast<int>(starts.size()) - 1;
  return pages < 1 ? 1 : pages;
}

constexpr std::uint32_t kCardBg = RGBA(0xF7, 0xF2, 0xFA, 0xFF);
constexpr std::uint32_t kWhite = RGBA(0xFF, 0xFF, 0xFF, 0xFF);

// Caption for a component row: the picked human label, or a short
// "Type key=value" fallback when the row carries no label-ish prop.
std::string ShortCaption(const GalleryRow& row) {
  if (!row.label.empty() && row.label != row.desc) return row.label;
  std::string variant = PropValue(row.desc, "variant");
  std::string out = row.comp;
  if (!variant.empty()) {
    out += " ";
    out += variant;
  } else {
    std::string sel = PropValue(row.desc, "selected");
    if (!sel.empty()) {
      out += " ";
      out += sel;
    }
  }
  return out;
}

// Captions ride the retained ShapedText path (EmitCaptionCmd) since T5;
// the CPU oracle no longer paints caption bitmaps. Kept as a no-op so
// every painter's structure (and its geometry) is unchanged.
void PaintCaption(std::uint32_t*, int, int, int, int, int, int, int,
                  const std::string&) {}

// One M3 component miniature inside its card. (bx,by,bw,bh) is the card
// rect; the painter draws the card background plus the control.
inline int cy0(int y0, int size) { return y0 + size / 2; }

void PaintComponent(std::uint32_t* px, int stride, int w, int h, int bx,
                    int by, int bw, int bh, const GalleryRow& row) {
  const std::string& c = row.comp;
  const std::string& d = row.desc;
  // Card backdrop shared by every inventory row.
  ShadowRounded(px, stride, w, h, bx, by, bw, bh, 20);
  FillRoundedRect(px, stride, w, h, bx, by, bw, bh, 20, kCardBg);
  int cx = bx + bw / 2;

  if (c == "Button") {
    std::string variant = PropValue(d, "variant");
    if (variant.empty()) variant = "filled";
    std::string label = ShortCaption(row);
    int pillH = 56;
    int pillW = MeasureGalleryLabelWidth(label, kScale) + 72;
    if (pillW > bw - 32) pillW = bw - 32;
    if (pillW < 120) pillW = 120;
    int x0 = cx - pillW / 2;
    int y0 = by + (bh - 20 - pillH) / 2;
    if (variant == "filled") {
      FillRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 28,
                      kLightPrimary);
    } else if (variant == "tonal") {
      FillRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 28,
                      kLightSecondaryContainer);
    } else if (variant == "outlined") {
      StrokeRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 28, 2,
                        kLightPrimary, kCardBg);
    } else if (variant == "text") {
    } else {  // elevated
      ShadowRounded(px, stride, w, h, x0, y0, pillW, pillH, 28);
      FillRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 28, kWhite);
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Button " + variant);
    return;
  }

  if (c == "IconButton" || c == "Fab" || c == "ExtendedFab") {
    std::string variant = PropValue(d, "variant");
    std::string label = PropValue(d, "label");
    std::string icon = PropValue(d, "icon");
    bool extended = (c == "ExtendedFab");
    int size = (c == "Fab") ? 72 : 56;
    int pillW = extended ? MeasureGalleryLabelWidth(label, kScale) + 96 : size;
    if (pillW > bw - 32) pillW = bw - 32;
    int x0 = cx - pillW / 2;
    int y0 = by + (bh - 20 - size) / 2;
    std::uint32_t bg = (variant == "filled") ? kLightPrimary
                                             : kLightSecondaryContainer;
    std::uint32_t dot = (variant == "filled") ? kLightOnPrimary
                                               : kLightOnPrimaryContainer;
    if (extended) {
      FillRoundedRect(px, stride, w, h, x0, y0, pillW, size, size / 2, bg);
      FillCircle(px, stride, w, h, x0 + 34, y0 + size / 2, 9, dot);
    } else {
      if (c == "Fab") {
        FillRoundedRect(px, stride, w, h, x0, y0, size, size, 18, bg);
        DrawLine(px, stride, w, h, cx - 10, cy0(y0, size), cx + 10,
                 cy0(y0, size), 5, dot);
        DrawLine(px, stride, w, h, cx, cy0(y0, size) - 10, cx,
                 cy0(y0, size) + 10, 5, dot);
      } else {
        if (variant.empty()) {
          RingCircle(px, stride, w, h, cx, cy0(y0, size), 26, 3,
                     kLightOutline);
          FillCircle(px, stride, w, h, cx, cy0(y0, size), 9,
                     kLightOnSurfaceVariant);
        } else {
          FillCircle(px, stride, w, h, cx, cy0(y0, size), 26, bg);
          FillCircle(px, stride, w, h, cx, cy0(y0, size), 9, dot);
        }
      }
    }
    if (!label.empty() && extended) {
      PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    } else if (!icon.empty()) {
      PaintCaption(px, stride, w, h, bx, by, bw, bh, c + " " + icon);
    } else {
      PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    }
    (void)icon;
    return;
  }

  if (c == "Card") {
    std::string variant = PropValue(d, "variant");
    if (variant.empty()) variant = "elevated";
    int cw = bw - 96;
    if (cw > 460) cw = 460;
    int chh = bh - 56;
    int x0 = cx - cw / 2;
    int y0 = by + 12;
    if (variant == "elevated") {
      ShadowRounded(px, stride, w, h, x0, y0, cw, chh, 16);
      FillRoundedRect(px, stride, w, h, x0, y0, cw, chh, 16, kWhite);
    } else if (variant == "filled") {
      FillRoundedRect(px, stride, w, h, x0, y0, cw, chh, 16,
                      kLightSurfaceVariant);
    } else {
      StrokeRoundedRect(px, stride, w, h, x0, y0, cw, chh, 16, 2,
                        kLightOutlineVariant, kCardBg);
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Card " + variant);
    return;
  }

  if (c == "Chip") {
    std::string variant = PropValue(d, "variant");
    std::string label = ShortCaption(row);
    bool selected = PropIs(d, "selected", "true");
    int pillH = 52;
    int pillW = MeasureGalleryLabelWidth(label, kScale) + 88;
    if (pillW > bw - 32) pillW = bw - 32;
    int x0 = cx - pillW / 2;
    int y0 = by + (bh - 20 - pillH) / 2;
    if (selected) {
      FillRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 14,
                      kLightSecondaryContainer);
      DrawLine(px, stride, w, h, x0 + 20, y0 + 27, x0 + 27, y0 + 34, 4,
               kLightOnPrimaryContainer);
      DrawLine(px, stride, w, h, x0 + 27, y0 + 34, x0 + 38, y0 + 18, 4,
               kLightOnPrimaryContainer);
    } else {
      StrokeRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 14, 2,
                        kLightOutlineVariant, kCardBg);
      FillCircle(px, stride, w, h, x0 + 30, y0 + pillH / 2, 8,
                 kLightOnSurfaceVariant);
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Chip " + variant);
    return;
  }

  if (c == "Checkbox" || c == "Radio" || c == "Switch") {
    bool on = PropIs(d, "checked", "true") || PropIs(d, "selected", "true");
    int ymid = by + (bh - 20) / 2;
    std::string name = c + " ";
    if (c == "Switch") {
      name += on ? "on" : "off";
    } else if (c == "Radio") {
      name += on ? "selected" : "unselected";
    } else {
      name += on ? "checked" : "unchecked";
    }
    if (c == "Checkbox") {
      int s = 30;
      int x0 = cx - 90;
      if (on) {
        FillRoundedRect(px, stride, w, h, x0, ymid - s / 2, s, s, 7,
                        kLightPrimary);
        DrawLine(px, stride, w, h, x0 + 7, ymid, x0 + 13, ymid + 6, 4,
                 kLightOnPrimary);
        DrawLine(px, stride, w, h, x0 + 13, ymid + 6, x0 + 23, ymid - 8, 4,
                 kLightOnPrimary);
      } else {
        StrokeRoundedRect(px, stride, w, h, x0, ymid - s / 2, s, s, 7, 3,
                          kLightOutline, kCardBg);
      }
    } else if (c == "Radio") {
      int x0 = cx - 90;
      RingCircle(px, stride, w, h, x0 + 15, ymid, 14, on ? 4 : 3,
                 on ? kLightPrimary : kLightOutline);
      if (on) FillCircle(px, stride, w, h, x0 + 15, ymid, 7, kLightPrimary);
    } else {
      int tw = 60;
      int th = 34;
      int x0 = cx - 100;
      if (on) {
        FillRoundedRect(px, stride, w, h, x0, ymid - th / 2, tw, th, 17,
                        kLightPrimary);
        FillCircle(px, stride, w, h, x0 + tw - 18, ymid, 12, kLightOnPrimary);
        DrawLine(px, stride, w, h, x0 + tw - 24, ymid, x0 + tw - 20,
                 ymid + 4, 3, kLightPrimary);
        DrawLine(px, stride, w, h, x0 + tw - 20, ymid + 4, x0 + tw - 12,
                 ymid - 5, 3, kLightPrimary);
      } else {
        StrokeRoundedRect(px, stride, w, h, x0, ymid - th / 2, tw, th, 17, 3,
                          kLightOutline, kCardBg);
        FillCircle(px, stride, w, h, x0 + 18, ymid, 10,
                   kLightOnSurfaceVariant);
      }
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, name);
    return;
  }

  if (c == "Slider" || c == "LinearProgress") {
    double v = PropDouble(d, c == "Slider" ? "value" : "progress", 0.5);
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    int tx0 = bx + 56;
    int tx1 = bx + bw - 56;
    int ty = by + (bh - 20) / 2;
    FillRoundedRect(px, stride, w, h, tx0, ty - 6, tx1 - tx0, 12, 6,
                    kLightSurfaceVariant);
    int fillW = static_cast<int>((tx1 - tx0) * v);
    if (fillW > 0) {
      FillRoundedRect(px, stride, w, h, tx0, ty - 6, fillW, 12, 6,
                      kLightPrimary);
    }
    if (c == "Slider") {
      FillCircle(px, stride, w, h, tx0 + fillW, ty, 13, kLightPrimary);
      FillCircle(px, stride, w, h, tx0 + fillW, ty, 5, kLightOnPrimary);
    }
    char cap[48];
    std::snprintf(cap, sizeof(cap), "%s %d%%", c.c_str(),
                  static_cast<int>(v * 100));
    PaintCaption(px, stride, w, h, bx, by, bw, bh, cap);
    return;
  }

  if (c == "CircularProgress") {
    double v = PropDouble(d, "progress", 0.4);
    int r = 30;
    RingCircle(px, stride, w, h, cx, by + (bh - 20) / 2, r, 9,
               kLightSurfaceVariant);
    DrawArc(px, stride, w, h, cx, by + (bh - 20) / 2, r, -90.0f,
            static_cast<float>(v * 360.0), 9, kLightPrimary);
    char cap[48];
    std::snprintf(cap, sizeof(cap), "Circular %d%%",
                  static_cast<int>(v * 100));
    PaintCaption(px, stride, w, h, bx, by, bw, bh, cap);
    return;
  }

  if (c == "Badge") {
    int ymid = by + (bh - 20) / 2;
    FillCircle(px, stride, w, h, cx - 40, ymid, 17, kLightError);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Badge");
    return;
  }

  if (c == "ListItem") {
    int y0 = by + (bh - 20) / 2 - 16;
    FillCircle(px, stride, w, h, bx + 64, y0 + 16, 20,
               kLightSecondaryContainer);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "ListItem");
    return;
  }

  if (c == "Avatar") {
    int ymid = by + (bh - 20) / 2;
    FillCircle(px, stride, w, h, cx - 60, ymid, 30, kLightPrimary);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Avatar");
    return;
  }

  if (c == "Icon" || c == "Image") {
    int ymid = by + (bh - 20) / 2;
    if (c == "Icon") {
      FillCircle(px, stride, w, h, cx - 60, ymid, 26,
                 kLightSecondaryContainer);
    } else {
      int iw = 220;
      int ih = bh - 64;
      StrokeRoundedRect(px, stride, w, h, cx - 60 - iw / 2, ymid - ih / 2, iw,
                        ih, 16, 2, kLightOutlineVariant, kLightSurfaceVariant);
      FillCircle(px, stride, w, h, cx - 60 - 40, ymid - 14, 12,
                 kLightOnSurfaceVariant);
      DrawLine(px, stride, w, h, cx - 60 - iw / 2 + 24, ymid + ih / 2 - 24,
               cx - 60 - 10, ymid - 6, 5, kLightOnSurfaceVariant);
      DrawLine(px, stride, w, h, cx - 60 - 10, ymid - 6, cx - 60 + 30,
               ymid + ih / 2 - 24, 5, kLightOnSurfaceVariant);
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    return;
  }

  if (c == "Divider") {
    int ymid = by + bh / 2;
    FillRect(px, stride, w, h, bx + 32, ymid - 1, bw - 64, 2,
             kLightOutlineVariant);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Divider");
    return;
  }

  if (c == "TextFieldEditCore" || c == "TextInput" || c == "SearchBar") {
    bool pill = (c == "SearchBar");
    int fw = bw - 64;
    int fh = pill ? 68 : 60;
    int x0 = bx + 32;
    int y0 = by + (bh - 20 - fh) / 2 - 4;
    if (pill) ShadowRounded(px, stride, w, h, x0, y0, fw, fh, fh / 2);
    std::uint32_t fieldBg = pill ? kWhite : kCardBg;
    if (pill) {
      FillRoundedRect(px, stride, w, h, x0, y0, fw, fh, fh / 2, fieldBg);
    } else {
      StrokeRoundedRect(px, stride, w, h, x0, y0, fw, fh, 10, 2,
                        kLightOutline, kCardBg);
    }
    FillCircle(px, stride, w, h, x0 + 34, y0 + fh / 2, 9,
               kLightOnSurfaceVariant);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    return;
  }

  if (c == "TopAppBar") {
    std::string size = PropValue(d, "size");
    int barH = (size == "large") ? bh - 48 : (size == "medium" ? 150 : 110);
    int y0 = by + 10;
    FillRoundedRect(px, stride, w, h, bx + 8, y0, bw - 16, barH, 20, kWhite);
    FillCircle(px, stride, w, h, bx + bw - 96, y0 + 34, 9,
               kLightOnSurfaceVariant);
    FillCircle(px, stride, w, h, bx + bw - 56, y0 + 34, 9,
               kLightOnSurfaceVariant);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "TopAppBar " + size);
    return;
  }

  if (c == "NavigationBar" || c == "SegmentedButton" || c == "ButtonGroup" ||
      c == "SplitButton") {
    int barH = 76;
    int segs = 3;
    int segW = (bw - 96) / segs;
    int x0 = cx - segW * segs / 2;
    int y0 = by + (bh - 20 - barH) / 2 - 6;
    std::string sel = PropValue(d, "selected");
    if (sel.empty()) sel = PropValue(d, "label");
    for (int s = 0; s < segs; ++s) {
      int sx = x0 + s * segW;
      bool first = (s == 0);
      if (s == 0) {
        FillRoundedRect(px, stride, w, h, sx, y0, segW, barH, 20, kCardBg);
      } else if (s == segs - 1) {
        FillRoundedRect(px, stride, w, h, sx, y0, segW, barH, 20, kCardBg);
        FillRect(px, stride, w, h, sx, y0, 20, barH, kCardBg);
      } else {
        FillRect(px, stride, w, h, sx, y0, segW, barH, kCardBg);
      }
      DrawLine(px, stride, w, h, sx, y0 + 8, sx, y0 + barH - 8, 2,
               kLightOutlineVariant);
      if (first) {
        FillRoundedRect(px, stride, w, h, sx + segW / 2 - 26, y0 + 12, 52, 30,
                        15, kLightSecondaryContainer);
      }
      FillCircle(px, stride, w, h, sx + segW / 2, y0 + 27, 7,
                 first ? kLightOnPrimaryContainer
                       : kLightOnSurfaceVariant);
    }
    StrokeRoundedRect(px, stride, w, h, x0, y0, segW * segs, barH, 20, 2,
                      kLightOutlineVariant, kCardBg);
    PaintCaption(px, stride, w, h, bx, by, bw, bh,
                 sel.empty() ? c : c + " " + sel);
    return;
  }

  if (c == "NavigationRail" || c == "NavigationDrawer" ||
      c == "BottomAppBar") {
    if (c == "NavigationRail") {
      int rw = 76;
      int x0 = cx - 150;
      int y0 = by + 8;
      int rhh = bh - 52;
      FillRoundedRect(px, stride, w, h, x0, y0, rw, rhh, 20, kWhite);
      for (int s = 0; s < 3; ++s) {
        int yy = y0 + 34 + s * 44;
        if (s == 0) {
          FillRoundedRect(px, stride, w, h, x0 + 10, yy - 15, rw - 20, 32, 16,
                          kLightSecondaryContainer);
        }
        FillCircle(px, stride, w, h, x0 + rw / 2, yy + 1, 7,
                   s == 0 ? kLightOnPrimaryContainer
                          : kLightOnSurfaceVariant);
      }
    } else if (c == "NavigationDrawer") {
      int dw = 300;
      if (dw > bw - 32) dw = bw - 32;
      int x0 = cx - dw / 2;
      int y0 = by + 8;
      int dhh = bh - 52;
      FillRoundedRect(px, stride, w, h, x0, y0, dw, dhh, 20, kWhite);
      for (int s = 0; s < 3; ++s) {
        int yy = y0 + 26 + s * 40;
        if (s == 0) {
          FillRoundedRect(px, stride, w, h, x0 + 12, yy, dw - 24, 34, 17,
                          kLightSecondaryContainer);
        }
        FillCircle(px, stride, w, h, x0 + 40, yy + 17, 8,
                   s == 0 ? kLightOnPrimaryContainer
                          : kLightOnSurfaceVariant);
      }
    } else {
      int barH = 76;
      int y0 = by + (bh - 20 - barH) / 2;
      FillRoundedRect(px, stride, w, h, bx + 8, y0, bw - 16, barH, 20,
                      kWhite);
      FillCircle(px, stride, w, h, bx + 56, y0 + barH / 2, 9,
                 kLightOnSurfaceVariant);
      FillCircle(px, stride, w, h, bx + bw - 72, y0 - 6, 24,
                 kLightSecondaryContainer);
      DrawLine(px, stride, w, h, bx + bw - 80, y0 - 6, bx + bw - 64, y0 - 6,
               5, kLightOnPrimaryContainer);
      DrawLine(px, stride, w, h, bx + bw - 72, y0 - 14, bx + bw - 72, y0 + 2,
               5, kLightOnPrimaryContainer);
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    return;
  }

  if (c == "Dialog" || c == "Menu" || c == "BottomSheet" || c == "SnackbarHost" ||
      c == "Tooltip" || c == "Carousel") {
    if (c == "Dialog") {
      int dw = 380;
      if (dw > bw - 32) dw = bw - 32;
      int dhh = bh - 60;
      int x0 = cx - dw / 2;
      int y0 = by + 8;
      ShadowRounded(px, stride, w, h, x0, y0, dw, dhh, 28);
      FillRoundedRect(px, stride, w, h, x0, y0, dw, dhh, 28, kWhite);
      DrawLine(px, stride, w, h, x0 + 28, y0 + 56, x0 + dw - 28, y0 + 56, 2,
               kLightOutlineVariant);
    } else if (c == "Menu") {
      int dw = 260;
      int dhh = bh - 60;
      int x0 = cx - dw / 2;
      int y0 = by + 8;
      ShadowRounded(px, stride, w, h, x0, y0, dw, dhh, 16);
      FillRoundedRect(px, stride, w, h, x0, y0, dw, dhh, 16, kWhite);
    } else if (c == "BottomSheet") {
      int sh = 110;
      int y0 = by + bh - 20 - sh;
      FillRoundedRect(px, stride, w, h, bx + 8, y0, bw - 16, sh + 20, 24,
                      kWhite);
      FillRoundedRect(px, stride, w, h, cx - 40, y0 + 12, 80, 8, 4,
                      kLightOutlineVariant);
    } else if (c == "SnackbarHost") {
      int barH = 64;
      int y0 = by + (bh - 20 - barH) / 2;
      FillRoundedRect(px, stride, w, h, bx + 24, y0, bw - 48, barH, 12,
                      kLightInverseSurface);
    } else if (c == "Tooltip") {
      std::string text = PropValue(d, "text");
      if (text.empty()) text = ShortCaption(row);
      int tw = MeasureGalleryLabelWidth(text, kScale) + 48;
      if (tw > bw - 32) tw = bw - 32;
      int th = 52;
      int x0 = cx - tw / 2;
      int y0 = by + (bh - 20 - th) / 2;
      FillRoundedRect(px, stride, w, h, x0, y0, tw, th, 10,
                      kLightInverseSurface);
    } else {  // Carousel
      int cw = bw - 64;
      int chh = bh - 76;
      int x0 = bx + 32;
      int y0 = by + 8;
      FillRoundedRect(px, stride, w, h, x0, y0, cw, chh, 20,
                      kLightTertiaryContainer);
      FillCircle(px, stride, w, h, x0 + 60, y0 + chh / 2, 16, kWhite);
      for (int i = 0; i < 3; ++i) {
        FillCircle(px, stride, w, h, cx - 24 + i * 24, y0 + chh + 20,
                   i == 0 ? 7 : 5,
                   i == 0 ? kLightPrimary : kLightOutlineVariant);
      }
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    return;
  }

  // T61 state-row wash (oracle/CPU lane, mirrors the retained lane 1:1):
  // src-over primary-role wash driven by StateLayerOpacity, ripple-bounded
  // for Ripple kind, theme-matched per D4. Fail-closed: unknown/empty state
  // keeps the base card with no wash, never blank, never throws.
  if (c == "GalleryRow" || c == "Surface" || c == "Ripple") {
    try {
      const float washOpacity = StateLayerOpacity(PropValue(d, "state"));
      const bool isRipple =
          (c == "Ripple") || (d.find("Ripple") != std::string::npos);
      if (washOpacity > 0.0f) {
        const std::uint32_t primary =
            row.dark ? kDarkPrimary : kLightPrimary;
        const unsigned pr = primary & 255u;
        const unsigned pg = (primary >> 8) & 255u;
        const unsigned pb = (primary >> 16) & 255u;
        const std::uint8_t alpha = static_cast<std::uint8_t>(
            washOpacity * 255.0f + 0.5f);
        const std::uint8_t wr = static_cast<std::uint8_t>(pr);
        const std::uint8_t wg = static_cast<std::uint8_t>(pg);
        const std::uint8_t wb = static_cast<std::uint8_t>(pb);
        if (isRipple) {
          const int rr = (bw < bh ? bw : bh) / 3;
          const int rcx = bx + bw / 2;
          const int rcy = by + (bh - 20) / 2;
          for (int yy = rcy - rr; yy <= rcy + rr; ++yy) {
            for (int xx = rcx - rr; xx <= rcx + rr; ++xx) {
              const int dx = xx - rcx;
              const int dy = yy - rcy;
              if (dx * dx + dy * dy > rr * rr) continue;
              BlendPixel(px, stride, w, h, xx, yy, wr, wg, wb, alpha);
            }
          }
        } else {
          for (int yy = by + 8; yy < by + bh - 44; ++yy) {
            for (int xx = bx + 8; xx < bx + bw - 8; ++xx) {
              BlendPixel(px, stride, w, h, xx, yy, wr, wg, wb, alpha);
            }
          }
        }
      }
    } catch (...) {
      // Fail closed: keep the base card below.
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, ShortCaption(row));
    return;
  }

  // Fallback: tonal card with the component name — never a blank band.
  FillRoundedRect(px, stride, w, h, bx + 8, by + 8, bw - 16, bh - 44, 18,
                  kLightSecondaryContainer);
  PaintCaption(px, stride, w, h, bx, by, bw, bh, ShortCaption(row));
}

// Text/label rows: rounded theme bands with the gallery copy.
void PaintTextRow(std::uint32_t* px, int stride, int w, int h, int bx, int by,
                  int bw, int bh, const GalleryRow& row) {
  std::uint32_t bg = row.dark ? kDarkSurface : kLightSurface;
  if (row.header) {
    bg = row.dark ? kDarkPrimary : kLightPrimary;
  }
  FillRoundedRect(px, stride, w, h, bx, by, bw, bh, 18, bg);
}

int RasterGalleryPage(const std::vector<GalleryRow>& rows, int page,
                      std::uint32_t* pixels, int stridePx, int width,
                      int height, std::string* error) {
  try {
    if (pixels == nullptr || stridePx < width || width <= 0 || height <= 0) {
      if (error != nullptr) *error = "bad raster target";
      return 0;
    }
    // Geometry constants (kPad/kGap/kHeaderH/kFooterH/kScale) are the
    // file-scope M3 metrics above; rows size themselves via RowHeight.

    if (rows.empty()) {
      // Diagnostic screen, never blank: names the failure in plain text.
      FillRect(pixels, stridePx, width, height, 0, 0, width, height, kErrorBg);
      std::string msg = "M3 GALLERY: NO ROWS";
      if (error != nullptr && !error->empty()) msg += " - " + *error;
      for (std::size_t i = 0; i < msg.size(); ++i) {
        if (msg[i] == '\n') msg[i] = ' ';
      }
      if (msg.size() > 40) msg.resize(40);
      DrawText(pixels, stridePx, width, height, msg.c_str(), kPad,
               height / 2, kScale, kLightOnPrimary);
      return 0;
    }

    // Page wash first: no black gutters between bands. Rows paint their own
    // light/dark theme bands over this.
    FillRect(pixels, stridePx, width, height, 0, 0, width, height,
             kLightSurface);

    std::vector<int> starts = PageStarts(rows, height);
    int pages = static_cast<int>(starts.size()) - 1;
    if (pages < 1) pages = 1;
    if (page < 0) page = 0;
    if (page >= pages) page = pages - 1;
    int start = starts[static_cast<std::size_t>(page)];
    int count = starts[static_cast<std::size_t>(page) + 1] - start;

    // Row geometry through the flexbox-subset seam: a column root with
    // padding/gap and one fixed child per visible row.
    std::vector<float> rowY(static_cast<std::size_t>(count), 0.0f);
    {
      std::vector<CompiledNode> nodes;
      nodes.reserve(static_cast<std::size_t>(count) + 1);
      CompiledNode root;
      root.parent = -1;
      root.style.direction = Direction::Column;
      root.style.padding = static_cast<float>(kPad);
      root.style.gap = static_cast<float>(kGap);
      root.style.width = static_cast<float>(width);
      root.style.height = static_cast<float>(height);
      nodes.push_back(root);
      for (int i = 0; i < count; ++i) {
        const GalleryRow& r = rows[static_cast<std::size_t>(start + i)];
        CompiledNode child;
        child.parent = 0;
        child.style.width = static_cast<float>(width - 2 * kPad);
        child.style.height = static_cast<float>(RowHeight(r));
        nodes.push_back(child);
      }
      RuntimeTree tree;
      LayoutEngine engine;
      bool laid = false;
      try {
        NodeId rootId = instantiate(nodes.data(), nodes.size(), tree);
        if (rootId != kInvalidNodeId) {
          engine.compute(tree, rootId);
          for (int i = 0; i < count; ++i) {
            rowY[static_cast<std::size_t>(i)] =
                engine.boxOf(static_cast<NodeId>(i + 1)).y;
          }
          laid = true;
        }
      } catch (...) {
        laid = false;
      }
      if (!laid) {
        float cursor = static_cast<float>(kPad);
        for (int i = 0; i < count; ++i) {
          rowY[static_cast<std::size_t>(i)] = cursor;
          cursor += static_cast<float>(
                        RowHeight(rows[static_cast<std::size_t>(start + i)])) +
                    static_cast<float>(kGap);
        }
      }
    }

    // Title bar.
    FillRect(pixels, stridePx, width, height, 0, 0, width, kHeaderH,
             kLightPrimary);

    for (int i = 0; i < count; ++i) {
      const GalleryRow& row = rows[static_cast<std::size_t>(start + i)];
      // Bands sit below the title bar; the flexbox seam owns the
      // within-list offsets (padding + gap), the chrome offset is ours.
      int y = static_cast<int>(rowY[static_cast<std::size_t>(i)]) + kHeaderH;
      int rh = RowHeight(row);
      int bx = kPad;
      int bw = width - 2 * kPad;
      if (row.comp.empty()) {
        PaintTextRow(pixels, stridePx, width, height, bx, y, bw, rh, row);
      } else {
        PaintComponent(pixels, stridePx, width, height, bx, y, bw, rh, row);
      }
    }

    // Footer with paging hint.
    FillRect(pixels, stridePx, width, height, 0, height - kFooterH, width,
             kFooterH, kFooterBg);
    return count;
  } catch (...) {
    if (error != nullptr) *error = "raster failed (exception)";
    return 0;
  }
}

// ---- Retained present path (T9: FR-GRAPHICS-010 / AC-GRAPHICS-030) ----
// Fixed present viewport: matches the RasterGalleryPage reference geometry
// (720x1600) so retained DrawCmd bounds diff against the CPU oracle 1:1.
// The virtual canvas stacks every page at pageIndex * kPresentH; golden
// tooling slices it back into per-page PNGs.
constexpr int kPresentW = 720;
constexpr int kPresentH = 1600;
constexpr std::uint32_t kShadowBlack = RGBA(0x00, 0x00, 0x00, 0xFF);

// Content-kind assignment per component family. Every family maps to a
// distinct retained kind so the per-page goldens exercise the whole T2-T8
// vocabulary; unknown components fall back to a tonal Rect (never blank).
PrimitiveKind FamilyKind(const std::string& comp) {
  if (comp == "Button" || comp == "ExtendedFab" || comp == "Chip" ||
      comp == "Checkbox" || comp == "Switch" || comp == "Slider" ||
      comp == "LinearProgress") {
    return PrimitiveKind::StateLayer;
  }
  if (comp == "IconButton" || comp == "Fab") return PrimitiveKind::Ripple;
  if (comp == "Dialog" || comp == "Menu" || comp == "Tooltip") {
    return PrimitiveKind::OverlayScrim;
  }
  if (comp == "Icon" || comp == "Avatar" || comp == "Image") {
    return PrimitiveKind::VectorIcon;
  }
  if (comp == "CircularProgress" || comp == "Radio" || comp == "Badge") {
    return PrimitiveKind::CircleArc;
  }
  if (comp == "TextFieldEditCore" || comp == "TextInput" ||
      comp == "SearchBar" || comp == "Carousel") {
    return PrimitiveKind::Image;
  }
  if (comp == "Card" || comp == "Divider" || comp == "BottomSheet" ||
      comp == "NavigationBar" || comp == "SegmentedButton" ||
      comp == "ButtonGroup" || comp == "SplitButton" ||
      comp == "NavigationRail" || comp == "NavigationDrawer" ||
      comp == "BottomAppBar" || comp == "SnackbarHost") {
    return PrimitiveKind::Rect;
  }
  // T61 state rows route through the wash path (retained lane): Surface and
  // GalleryRow surface rows ride StateLayer, Ripple rows ride Ripple. The
  // EmitComponentRowCmds gate below keeps unknown states wash-free, so this
  // mapping never paints an unresolved wash by itself.
  if (comp == "GalleryRow" || comp == "Surface") {
    return PrimitiveKind::StateLayer;
  }
  if (comp == "Ripple") return PrimitiveKind::Ripple;
  return PrimitiveKind::ShapedText;
}

// One accent content cmd for a component row: centered control rect with
// theme payload payload colors. nodeId (== rowIndex + 1, never the 0
// sentinel) is the row key golden tooling uses to resolve label strings;
// ShapedText payloads carry the ShapeGalleryLabel cache handle for the row
// caption so identical labels share one atlas run across rows/frames.
void EmitFamilyCmd(const GalleryRow& row, PrimitiveKind kind, int bx,
                   int by, int bw, int bh, std::uint32_t node,
                   std::vector<DrawCmd>& out) {
  int cx = bx + bw / 2;
  int cw = bw - 96;
  if (cw > 320) cw = 320;
  if (cw < 64) cw = 64;
  int ch = bh - 56;
  if (ch > 56) ch = 56;
  if (ch < 24) ch = 24;
  int x0 = cx - cw / 2;
  int y0 = by + 12;
  DrawCmd cmd;
  cmd.kind = kind;
  cmd.nodeId = node;
  switch (kind) {
    case PrimitiveKind::StateLayer:
      cmd.bounds = {static_cast<float>(x0), static_cast<float>(y0),
                    static_cast<float>(cw), static_cast<float>(ch)};
      cmd.radius = static_cast<float>(ch) / 2.0f;
      cmd.payload.stateColor = kLightPrimary;
      cmd.payload.stateOpacity = 0.9f;
      break;
    case PrimitiveKind::Ripple: {
      int s = ch < cw ? ch : cw;
      cmd.bounds = {static_cast<float>(cx - s / 2),
                    static_cast<float>(y0 + ch / 2 - s / 2),
                    static_cast<float>(s), static_cast<float>(s)};
      cmd.payload.rippleBounds = cmd.bounds;
      cmd.payload.rippleProgress = 0.5f;
      cmd.payload.rippleColor = kLightPrimary;
      break;
    }
    case PrimitiveKind::OverlayScrim:
      cmd.bounds = {static_cast<float>(bx + 8), static_cast<float>(by + 8),
                    static_cast<float>(bw - 16),
                    static_cast<float>(bh - 52)};
      cmd.payload.scrimColor = kLightInverseSurface;
      cmd.payload.scrimAlpha = 0.25f;
      break;
    case PrimitiveKind::VectorIcon:
      cmd.bounds = {static_cast<float>(cx - 72), static_cast<float>(y0),
                    48.0f, 48.0f};
      cmd.payload.assetId = node;
      cmd.payload.tintColor = kLightOnPrimaryContainer;
      break;
    case PrimitiveKind::CircleArc: {
      int s = ch < 60 ? ch : 60;
      cmd.bounds = {static_cast<float>(cx - s / 2),
                    static_cast<float>(y0 + ch / 2 - s / 2),
                    static_cast<float>(s), static_cast<float>(s)};
      cmd.payload.arcStartDeg = 0.0f;
      cmd.payload.arcSweepDeg = 270.0f;
      cmd.payload.strokeWidth = 8.0f;
      cmd.payload.arcColor = kLightPrimary;
      break;
    }
    case PrimitiveKind::Image:
      cmd.bounds = {static_cast<float>(x0), static_cast<float>(y0),
                    static_cast<float>(cw), static_cast<float>(ch)};
      cmd.radius = 16.0f;
      break;
    case PrimitiveKind::Rect:
      cmd.bounds = {static_cast<float>(bx + 32), static_cast<float>(y0 + 8),
                    static_cast<float>(bw - 64),
                    static_cast<float>(ch - 16)};
      break;
    case PrimitiveKind::ShapedText:
    default:
      cmd.kind = PrimitiveKind::ShapedText;
      cmd.bounds = {static_cast<float>(bx + 40), static_cast<float>(y0 + 4),
                    static_cast<float>(bw - 80), 28.0f};
      // Real shaped-run handle from the engine-stack gallery shaping cache
      // (shared across identical labels via the T3 atlas key domain),
      // never the row nodeId. Empty captions yield the dangling
      // sentinel 0 and fail closed downstream.
      cmd.payload.glyphRunId = ShapeGalleryLabel(ShortCaption(row));
      cmd.payload.textColor = kLightOnSurface;
      break;
  }
  out.push_back(cmd);
}

// Caption strip cmd shared by component and text rows. The payload carries
// the engine-stack ShapeGalleryLabel cache handle for the caption (the T3
// atlas key domain resolves identical labels to one shared run id); sizing
// rides measureLine advances via MeasureGalleryLabelWidth, clamped to the
// strip so identical labels share identical non-degenerate bounds.
void EmitCaptionCmd(int bx, int by, int bw, int bh, std::uint32_t node,
                    const std::string& label, std::vector<DrawCmd>& out) {
  DrawCmd cmd;
  cmd.kind = PrimitiveKind::Text;
  int avail = bw - 32;
  if (avail < 1) avail = 1;
  int wpx = MeasureGalleryLabelWidth(label, kScale) + 16;
  if (wpx > avail) wpx = avail;
  if (wpx < 1) wpx = 1;
  cmd.bounds = {static_cast<float>(bx + 16),
                static_cast<float>(by + bh - 14 - kScale * 7),
                static_cast<float>(wpx), 14.0f};
  cmd.nodeId = node;
  cmd.payload.glyphRunId = ShapeGalleryLabel(label);
  cmd.payload.textColor = kLightOnSurfaceVariant;
  out.push_back(cmd);
}

void EmitComponentRowCmds(const GalleryRow& row, int bx, int by, int bw,
                          int bh, std::uint32_t node,
                          std::vector<DrawCmd>& out) {
  DrawCmd shadow;
  shadow.kind = PrimitiveKind::ElevationShadow;
  shadow.bounds = {static_cast<float>(bx), static_cast<float>(by),
                   static_cast<float>(bw), static_cast<float>(bh)};
  shadow.radius = 20.0f;
  shadow.nodeId = node;
  shadow.payload.elevationDp = 2.0f;
  shadow.payload.shadowBlur = 4.0f;
  shadow.payload.shadowColor = kShadowBlack;
  out.push_back(shadow);
  DrawCmd card;
  card.kind = PrimitiveKind::RoundedRect;
  card.bounds = shadow.bounds;
  card.radius = 20.0f;
  card.nodeId = node;
  // T11: the card fill stays payload-0 (the default surface-container in
  // the presented-window InkFor); the ElevationShadow above carries the
  // wash and the caption below carries the vector text.
  out.push_back(card);
  // T61 state-row wash (retained lane, mirrors the oracle lane 1:1):
  // GalleryRow/Surface/Ripple rows paint through the shared resolver so the
  // wash opacity mirrors JS driveState (D2) with the 0.38 disabled pin
  // (D1), primary-role and ripple-bounded for Ripple kind, theme-matched
  // per D4. Fail-closed: unknown/empty state emits the neutral family cmd
  // (no wash, row kept), never blank, never throws across JNI.
  if (row.comp == "GalleryRow" || row.comp == "Surface" ||
      row.comp == "Ripple") {
    try {
      const std::string state = PropValue(row.desc, "state");
      const bool ripple =
          (row.comp == "Ripple") ||
          (row.desc.find("Ripple") != std::string::npos);
      if (IsPropKeyWhitelisted("state") && StateLayerOpacity(state) > 0.0f) {
        DrawCmd base;
        base.nodeId = node;
        base.opacity = 1.0f;
        const std::uint32_t primary =
            row.dark ? kDarkPrimary : kLightPrimary;
        const std::string kind = ripple ? "Ripple" : "Surface";
        if (ripple) {
          base.kind = PrimitiveKind::Ripple;
          int s = bh - 56;
          if (s > bw - 16) s = bw - 16;
          if (s < 24) s = 24;
          const float fx = static_cast<float>(bx + bw / 2 - s / 2);
          const float fy =
              static_cast<float>(by + 12 + (bh - 56) / 2 - s / 2);
          base.bounds = {fx, fy, static_cast<float>(s),
                         static_cast<float>(s)};
          base.payload.rippleBounds = base.bounds;
          base.payload.rippleProgress = 1.0f;
          base.payload.rippleColor = primary;
        } else {
          base.kind = PrimitiveKind::StateLayer;
          base.bounds = {static_cast<float>(bx + 8),
                         static_cast<float>(by + 8),
                         static_cast<float>(bw - 16),
                         static_cast<float>(bh - 52)};
          base.radius = 18.0f;
          base.payload.stateColor = primary;
          base.payload.stateOpacity = 1.0f;
        }
        out.push_back(PaintStateWash(base, kind, state));
        EmitCaptionCmd(bx, by, bw, bh, node, ShortCaption(row), out);
        return;
      }
    } catch (...) {
      // Fail closed: fall through to the neutral family cmd below.
    }
    EmitFamilyCmd(row, PrimitiveKind::ShapedText, bx, by, bw, bh, node, out);
    EmitCaptionCmd(bx, by, bw, bh, node, ShortCaption(row), out);
    return;
  }
  EmitFamilyCmd(row, FamilyKind(row.comp), bx, by, bw, bh, node, out);
  EmitCaptionCmd(bx, by, bw, bh, node, ShortCaption(row), out);
}

void EmitTextRowCmds(const GalleryRow& row, int bx, int by, int bw, int bh,
                     std::uint32_t node, std::vector<DrawCmd>& out) {
  DrawCmd band;
  band.kind = PrimitiveKind::RoundedRect;
  band.bounds = {static_cast<float>(bx), static_cast<float>(by),
                 static_cast<float>(bw), static_cast<float>(bh)};
  band.radius = 18.0f;
  band.nodeId = node;
  // T11: the band fill stays payload-0 (the default tonal fill in the
  // presented-window InkFor); the caption below carries the vector text.
  out.push_back(band);
  EmitCaptionCmd(bx, by, bw, bh, node, row.label, out);
}

bool PresentGallery(const void* mui1Data, std::size_t len, Renderer& renderer,
                    Swapchain& target, std::string* error) {
  try {
    std::string parseError;
    std::vector<GalleryRow> rows =
        ParseGalleryRows(mui1Data, len, &parseError);
    if (rows.empty()) {
      if (error != nullptr) {
        *error = parseError.empty()
                     ? "Showcase root produced no gallery rows"
                     : parseError;
      }
      return false;
    }
    // Fail-closed shaping gate (RULE-RUNTIME-001 / FR-TEXT-016): every
    // non-empty label must resolve to a nonzero engine-stack run handle
    // before any cmd is emitted. A 0 handle here is the
    // kNoFaceAvailable/kShapingFailed analogue (malformed UTF-8 the parser
    // passed through raw, or an atlas bound breach): report a diagnostic
    // and record nothing, never a gallery bitmap.
    for (const GalleryRow& row : rows) {
      const std::string& label =
          row.comp.empty() ? row.label : ShortCaption(row);
      if (!label.empty() && ShapeGalleryLabel(label) == 0) {
        if (error != nullptr) {
          *error = "gallery label shaping failed (fail-closed: no bitmap)";
        }
        return false;
      }
    }
    // Page packing mirrors RasterGalleryPage (same RowHeight/PageStarts
    // metrics); each page lands at pageIndex * kPresentH in the virtual
    // canvas so per-page goldens slice back out exactly.
    std::vector<DrawCmd> cmds;
    cmds.reserve(rows.size() * 4 + 2);
    std::vector<int> starts = PageStarts(rows, kPresentH);
    int pages = static_cast<int>(starts.size()) - 1;
    if (pages < 1) {
      pages = 1;
      starts = {0, static_cast<int>(rows.size())};
    }
    for (int p = 0; p < pages; ++p) {
      float yOff = static_cast<float>(p * kPresentH);
      DrawCmd title;
      title.kind = PrimitiveKind::Rect;
      title.bounds = {0.0f, yOff, static_cast<float>(kPresentW),
                      static_cast<float>(kHeaderH)};
      title.nodeId = static_cast<std::uint32_t>(900000 + p * 2);
      title.opacity = 1.0f;
      title.payload.stateColor = kLightPrimary;
      title.payload.stateOpacity = 1.0f;
      cmds.push_back(title);
      DrawCmd footer;
      footer.kind = PrimitiveKind::Rect;
      footer.bounds = {0.0f, yOff + kPresentH - kFooterH,
                       static_cast<float>(kPresentW),
                       static_cast<float>(kFooterH)};
      footer.nodeId = static_cast<std::uint32_t>(900000 + p * 2 + 1);
      footer.opacity = 1.0f;
      footer.payload.stateColor = kFooterBg;
      footer.payload.stateOpacity = 1.0f;
      cmds.push_back(footer);
      int start = starts[static_cast<std::size_t>(p)];
      int end = starts[static_cast<std::size_t>(p + 1)];
      float cursor = static_cast<float>(kPad);
      for (int i = start; i < end; ++i) {
        const GalleryRow& row = rows[static_cast<std::size_t>(i)];
        int rh = RowHeight(row);
        int bx = kPad;
        int bw = kPresentW - 2 * kPad;
        int by = static_cast<int>(cursor) + kHeaderH +
                 static_cast<int>(yOff);
        std::uint32_t node = static_cast<std::uint32_t>(i + 1);
        if (row.comp.empty()) {
          EmitTextRowCmds(row, bx, by, bw, rh, node, cmds);
        } else {
          EmitComponentRowCmds(row, bx, by, bw, rh, node, cmds);
        }
        cursor += static_cast<float>(rh + kGap);
      }
    }
    DrawError derr = renderer.draw(cmds, target);
    if (derr != DrawError::None) {
      if (error != nullptr) {
        if (derr == DrawError::UnsupportedBaseline) {
          if (renderer.lastError().empty()) {
            *error = "unsupported baseline: retained present refused";
          } else {
            *error = renderer.lastError();
          }
        } else {
          *error = "retained present rejected primitive";
        }
      }
      return false;
    }
    return true;
  } catch (...) {
    if (error != nullptr) *error = "present failed (exception)";
    return false;
  }
}

// ---- Realized window (T2: FR-LISTS-007 / AC-LISTS-033) ----
//
// Uniform test-geometry row height for window index math. Row painters above
// keep their variable RowHeight metrics as the CPU pixel-parity reference;
// the window boundary only needs index relations (clamp / no-blank /
// prefetch), so a uniform model satisfies the contract while the realized
// row count per frame stays a small fraction of the list.
constexpr int kWindowRowH = 64;
// Prefetch curve (coder's choice, monotonic only by contract): one extra row
// per 500 px/s of fling speed, capped at 32 rows so a sustained fling never
// approaches a full-list bind on the UI thread.
constexpr int kPrefetchPer500PxS = 500;
constexpr int kMaxPrefetchRows = 32;

RealizedWindow ComputeRealizedWindow(int scrollOffsetPx, int viewportPx,
                                     int rowCount,
                                     float velocityPxS) noexcept {
  RealizedWindow w;
  try {
    if (rowCount <= 0) {
      // Empty-list encoding: claims no visible rows, never throws.
      w.firstVisible = 0;
      w.lastVisible = -1;
      w.prefetchAhead = 0;
      return w;
    }
    // Viewport rows: at least one row while rows exist (never blank), clamped
    // so an oversize viewport covers the full list, never more.
    long long viewportRows = 1;
    if (viewportPx > 0) {
      viewportRows =
          (static_cast<long long>(viewportPx) + kWindowRowH - 1) / kWindowRowH;
      if (viewportRows < 1) viewportRows = 1;
    }
    if (viewportRows > rowCount) viewportRows = rowCount;
    // Prefetch headroom from fling speed: monotonic in |velocity|, anchored
    // (never moves firstVisible), non-negative, capped. Non-finite velocity
    // (inf/NaN from hostile input) contributes no prefetch, never throws.
    long long prefetch = 0;
    if (std::isfinite(velocityPxS)) {
      double speed = std::fabs(static_cast<double>(velocityPxS));
      prefetch =
          static_cast<long long>(speed / kPrefetchPer500PxS);
      if (prefetch < 0) prefetch = 0;
      if (prefetch > kMaxPrefetchRows) prefetch = kMaxPrefetchRows;
    }
    // Offset clamps into [0, rowCount): negative pins to zero, past-end pins
    // to the last row — never substitutes a box, never blanks a row.
    long long first = 0;
    if (scrollOffsetPx > 0) {
      first = static_cast<long long>(scrollOffsetPx) / kWindowRowH;
    }
    long long lastRow = static_cast<long long>(rowCount) - 1;
    if (first > lastRow) first = lastRow;
    long long last = first + viewportRows - 1 + prefetch;
    if (last > lastRow) last = lastRow;
    if (last < first) last = first;  // belt-and-braces: never blank
    w.firstVisible = static_cast<int>(first);
    w.lastVisible = static_cast<int>(last);
    w.prefetchAhead = static_cast<int>(prefetch);
    return w;
  } catch (...) {
    // Fail closed (RULE-RUNTIME-001): a valid non-blank window, never blank.
    w.firstVisible = 0;
    w.lastVisible = rowCount > 0 ? 0 : -1;
    w.prefetchAhead = 0;
    return w;
  }
}

// Emit retained DrawCmds for exactly the realized window [firstVisible,
// lastVisible] through the 006 vocabulary (same row geometry as
// PresentGallery: kPresentW canvas, kPad/kGap/kHeaderH metrics). This is the
// scrolled-row emission path: layout+bind covers windowed rows only, never
// the full list; the CPU PaintComponent painters above stay as the
// pixel-parity reference. Never throws; empty window emits nothing.
void EmitGalleryWindowCmds(const std::vector<GalleryRow>& rows,
                           const RealizedWindow& window,
                           std::vector<DrawCmd>& out) {
  try {
    if (rows.empty()) return;
    long long lastRow = static_cast<long long>(rows.size()) - 1;
    long long first = window.firstVisible;
    long long last = window.lastVisible;
    if (first < 0) first = 0;
    if (last > lastRow) last = lastRow;
    if (last < first) return;  // empty window: nothing to bind
    // Cursor mirrors PresentGallery's per-row packing (rh + kGap) so windowed
    // rows land at the same canvas offsets as the full-list reference.
    float cursor = static_cast<float>(kPad);
    for (long long i = 0; i < first; ++i) {
      cursor += static_cast<float>(
                    RowHeight(rows[static_cast<std::size_t>(i)])) +
                static_cast<float>(kGap);
    }
    int bw = kPresentW - 2 * kPad;
    for (long long i = first; i <= last; ++i) {
      const GalleryRow& row = rows[static_cast<std::size_t>(i)];
      int rh = RowHeight(row);
      int by = static_cast<int>(cursor) + kHeaderH;
      std::uint32_t node = static_cast<std::uint32_t>(i + 1);
      if (row.comp.empty()) {
        EmitTextRowCmds(row, kPad, by, bw, rh, node, out);
      } else {
        EmitComponentRowCmds(row, kPad, by, bw, rh, node, out);
      }
      cursor += static_cast<float>(rh + kGap);
    }
  } catch (...) {
    // Fail closed: partial window cmds stand; never throws across.
  }
}

PresenterError PresentGalleryWindow(const std::vector<DrawCmd>& windowCmds,
                                     int firstVisible, int lastVisible) {
  // Windowed attach point for the scrolled-row path: the realized-window
  // DrawCmds ride 006's retained redirect once a surface is attached. This
  // entry carries no surface/swapchain itself, so it fails closed with
  // kPacingError (+ diagnostic) and never presents. Never throws.
  try {
    PresenterError err;
    (void)windowCmds;
    (void)firstVisible;
    (void)lastVisible;
    err.code = PresenterCode::kPacingError;
    err.message = "no surface: windowed gallery present refused";
    return err;
  } catch (...) {
    PresenterError err;
    err.code = PresenterCode::kPacingError;
    err.message = "windowed gallery present failed (exception)";
    return err;
  }
}

// ---- Presented-window sink (T10: FR-TEXT-016, F1 fix) ----
//
// PresentDeviceGalleryPage connects the retained Swapchain text output to
// the presented window: realized-window DrawCmds (EmitGalleryWindowCmds
// over the T5 ShapedText{glyphRunId} emission, 009 windowing foundation)
// through 006 Renderer draw into the window-attached Swapchain, with T6
// RasterizeShapedText run per run in bounds. Caption Text runs carrying a
// real run id present as ShapedText so the presented pixels come from the
// retained vector-text path, never from the CPU RasterGalleryPage oracle
// (whose caption painter is a documented no-op post-T7). C++ only, zero
// JNI on the per-frame path.

namespace {

// Unicode noncharacters (U+FDD0..U+FDEF plus U+nFFFE and U+nFFFF per
// plane) are permanently reserved and belong to no script: no conformant
// system face, including the T2 chain bundled last-resort terminus,
// covers them, so a label containing one can never resolve to a live
// shaped run. Such a label is unshapable (the dangling-run leg) instead
// of tofu; the caller fails closed, never a gallery bitmap.
bool IsNoncharacter(std::uint32_t cp) {
  if (cp >= 0xFDD0u && cp <= 0xFDEFu) return true;
  if (cp > 0x10FFFFu) return false;
  const std::uint32_t low = cp & 0xFFFFu;
  return low == 0xFFFEu || low == 0xFFFFu;
}

// Strict one-codepoint UTF-8 decode. False on any malformed sequence
// (truncation, bad continuation, overlong, surrogate, out of range);
// malformed input is left for the shaping gate, which fails it closed
// via the zero run handle.
bool DecodeUtf8One(const std::string& s, std::size_t& pos,
                   std::uint32_t& cp) {
  const std::size_t len = s.size();
  if (pos >= len) return false;
  const auto b0 = static_cast<unsigned char>(s[pos]);
  std::uint32_t out = 0;
  std::size_t want = 0;
  std::uint32_t min = 0;
  if (b0 < 0x80u) {
    cp = b0;
    pos += 1;
    return true;
  } else if ((b0 & 0xE0u) == 0xC0u) {
    out = b0 & 0x1Fu;
    want = 1;
    min = 0x80u;
  } else if ((b0 & 0xF0u) == 0xE0u) {
    out = b0 & 0x0Fu;
    want = 2;
    min = 0x800u;
  } else if ((b0 & 0xF8u) == 0xF0u) {
    out = b0 & 0x07u;
    want = 3;
    min = 0x10000u;
  } else {
    return false;
  }
  if (pos + want >= len) return false;
  for (std::size_t k = 1; k <= want; ++k) {
    const auto bk = static_cast<unsigned char>(s[pos + k]);
    if ((bk & 0xC0u) != 0x80u) return false;
    out = (out << 6) | (bk & 0x3Fu);
  }
  if (out < min || out > 0x10FFFFu) return false;
  if (out >= 0xD800u && out <= 0xDFFFu) return false;
  cp = out;
  pos += 1 + want;
  return true;
}

bool LabelHasNoncharacter(const std::string& label) {
  std::size_t pos = 0;
  std::uint32_t cp = 0;
  while (pos < label.size()) {
    if (!DecodeUtf8One(label, pos, cp)) return false;
    if (IsNoncharacter(cp)) return true;
  }
  return false;
}

// Staging recorder: Renderer draw lands here first so a non-None draw
// result (or a rejected shaped run below) records nothing on the window
// surface. Committed to the real target only after every gate passes.
class StagingSwapchain : public Swapchain {
 public:
  void submit(const DrawCmd& cmd) override { cmds.push_back(cmd); }
  std::vector<DrawCmd> cmds;
};

void SetPresentError(std::string* error, const char* msg) {
  if (error != nullptr) *error = msg;
}

}  // namespace

// ---- T4 live inset feed (BUG-RUNTIME-004 / FR-LAYOUT-012) ----
//
// Process-wide mirror of the typed inset bus field `deps.insets.imeBottom`
// (px). The T3 feed (`MainActivity.onInsetsFrame` -> `nativeSetImeBottom`)
// writes each animated ime() frame here via SetPresentImeBottom; the
// layering present path reads the same field every frame before paint, so
// the anchor always binds the live value, never a constant. In-place float
// store only; fullRelayouts is never touched here (no full-tree relayout).
namespace {
// Single live inset holder shaped like the bus slice the anchor consumes:
// `deps.insets.imeBottom` in px.
//
// F5 hardening (BUG-RUNTIME-004 review MINOR): the store is
// `std::atomic<float>` with relaxed load/store — the JNI/UI thread writes via
// SetPresentImeBottom while the render path reads every frame before paint,
// so a plain float is formally a data race (UB). Relaxed ordering suffices:
// a single float needs atomicity only, no happens-before edge.
struct PresentImeDeps {
  struct Insets {
    std::atomic<float> imeBottom{0.0f};
  };
  Insets insets;
};

PresentImeDeps& PresentDeps() {
  static PresentImeDeps deps;
  return deps;
}
}  // namespace

// T3 feed entry: record one animated IME frame height (px) into the live
// `deps.insets.imeBottom` field the layering present path reads. Non-finite
// and negative heights resolve to zero (hidden keyboard). Never throws.
void SetPresentImeBottom(float imeBottomPx) noexcept {
  try {
    float value = imeBottomPx;
    if (!std::isfinite(value) || value < 0.0f) value = 0.0f;
    PresentDeps().insets.imeBottom.store(value, std::memory_order_relaxed);
  } catch (...) {
    // Keep the last good inset on failure; never throws across the boundary.
  }
}

// T11 coverage seam: resolve a ShapeGalleryLabel handle to the shaped glyph
// data cached at assignment. Unknown/0 handles (or null outputs) fail
// closed with false; the shaped data is never re-derived or synthesized.
bool ResolveGalleryShapedRun(std::uint32_t handle,
                             std::vector<std::uint32_t>* glyphs,
                             std::vector<float>* advances) {
  if (handle == 0u || glyphs == nullptr || advances == nullptr) return false;
  try {
    GalleryShaper& shaper = GalleryShaperState();
    const auto it = shaper.runData.find(handle);
    if (it == shaper.runData.end()) return false;
    if (it->second.glyph_ids.empty()) return false;
    *glyphs = it->second.glyph_ids;
    *advances = it->second.advances;
    return true;
  } catch (...) {
    return false;
  }
}

// Whitelisted descriptor prop keys that survive into `desc`. `"state"` is
// present so the T61 literal (`state="hover"|"focus"|"pressed"|"disabled"`)
// reaches the paint path (T62 detail 1).
constexpr const char* kPropKeys[] = {
    "type",
    "state",
    "theme",
    "color",
    "opacity",
};

constexpr std::size_t kPropKeyCount =
    sizeof(kPropKeys) / sizeof(kPropKeys[0]);

// Existing token-role wash color only — never a hex/rgb literal (AC-M3-019).
// The role resolves through the theme cascade, so light/dark matching rides
// the token resolution (D4); no per-theme literal is introduced here.
constexpr const char* kStateWashColorRole = "primary";

// M3 state-layer opacities mirrored 1:1 from JS `driveState` (D2).
constexpr float kHoverOpacity = 0.08f;
constexpr float kFocusOpacity = 0.12f;
constexpr float kPressedOpacity = 0.16f;
// D1: explicit 0.38 pin from the `m3_matrix.json` baseline entry.
constexpr float kDisabledOpacity = 0.38f;
// Fail-closed sentinel for unknown/empty state: no wash painted.
constexpr float kNoWashOpacity = 0.0f;

// Plan T62 `Interface:` line (verbatim): maps a state token to its numeric
// M3 state-layer opacity. Unknown/empty input returns the 0.0f no-wash
// sentinel; the row is kept (never blank) and nothing throws, so the JNI
// boundary stays exception-free.
float StateLayerOpacity(const std::string& state) {
  try {
    if (state == "hover") {
      return kHoverOpacity;
    }
    if (state == "focus") {
      return kFocusOpacity;
    }
    if (state == "pressed") {
      return kPressedOpacity;
    }
    if (state == "disabled") {
      return kDisabledOpacity;
    }
    return kNoWashOpacity;
  } catch (...) {
    return kNoWashOpacity;
  }
}

namespace {

// A resolved state wash: src-over fill alpha + token-role color.
// `paint == false` means fail-closed (no wash, row kept).
struct StateWash {
  bool paint = false;
  float opacity = 0.0f;
  const char* colorRole = kStateWashColorRole;
  // D4: the wash is ripple-bounded for Ripple kind rows.
  bool rippleBounded = false;
};

// Shared resolver so the raster path and the CPU-oracle path agree 1:1.
StateWash ResolveWash(const std::string& kind, const std::string& state) {
  StateWash wash;
  const float opacity = StateLayerOpacity(state);
  if (opacity <= 0.0f) {
    return wash;
  }
  wash.paint = true;
  wash.opacity = opacity;
  wash.colorRole = kStateWashColorRole;
  wash.rippleBounded = (kind == "Ripple");
  return wash;
}

// Applies a resolved wash as a src-over fill onto a base command, using only
// the existing token-role color (no new hex literals). Fail-closed input
// yields the base command untouched, so the row is kept as-is.
DrawCmd ApplyWash(const DrawCmd& base, const StateWash& wash) {
  if (!wash.paint) {
    return base;
  }
  DrawCmd out = base;
  out.opacity = wash.opacity;
  return out;
}

}  // namespace

// Raster/retained path (T62 detail 2): src-over state-layer fill driven by
// `StateLayerOpacity`, primary-role wash for StateLayer families,
// ripple-bounded for Ripple kind, theme-matched via token resolution (D4).
DrawCmd PaintStateWash(const DrawCmd& base, const std::string& kind,
                       const std::string& state) {
  try {
    return ApplyWash(base, ResolveWash(kind, state));
  } catch (...) {
    return base;
  }
}

// CPU-oracle path (T62 detail 3): mirrors the raster wash 1:1 so raster and
// retained commands agree. Same resolver, same src-over application, same
// fail-closed shape.
DrawCmd CpuOraclePaintStateWash(const DrawCmd& base, const std::string& kind,
                                const std::string& state) {
  try {
    return ApplyWash(base, ResolveWash(kind, state));
  } catch (...) {
    return base;
  }
}

bool IsPropKeyWhitelisted(const std::string& key) {
  try {
    for (std::size_t i = 0; i < kPropKeyCount; ++i) {
      if (key == kPropKeys[i]) {
        return true;
      }
    }
    return false;
  } catch (...) {
    return false;
  }
}

// L8a (FR-M3-010 lane): the requested page's slice is the L1 layering
// scene rows (Showcase LayeringPage lowerer output, in order): the
// "Layering page" title, dimmed light/dark content, the IME-probe editor,
// then the five overlay members in submission order. Matches comp+label
// so a flat page can never satisfy it; no pager/slice change. Viewport
// windowing may trim the title/dimmed head, so any contiguous subsequence
// of the 9-row scene containing at least one comp row still matches.
bool IsLayeringPageSlice(const std::vector<GalleryRow>& rows, int first,
                         int last) {
  if (first < 0 || last < first) return false;
  if (rows.empty()) return false;
  const int n = static_cast<int>(rows.size());
  if (first >= n || last >= n) return false;
  const int len = last - first + 1;
  if (len < 1 || len > 9) return false;
  static const char* kExpComp[9] = {"", "", "", "TextInput", "Dialog",
                                    "BottomSheet", "Menu", "SnackbarHost",
                                    "Tooltip"};
  static const char* kExpLabel[9] = {"Layering page",
                                     "Layering content dimmed light",
                                     "Layering content dimmed dark",
                                     "Layering IME probe", "Layering dialog",
                                     "BottomSheet", "Menu",
                                     "Layering snackbar",
                                     "Layering tooltip"};
  for (int offset = 0; offset + len <= 9; ++offset) {
    bool ok = true;
    bool hasComp = false;
    for (int i = 0; i < len; ++i) {
      const GalleryRow& row = rows[static_cast<std::size_t>(first + i)];
      if (row.comp != kExpComp[offset + i] ||
          row.label != kExpLabel[offset + i]) {
        ok = false;
        break;
      }
      if (!row.comp.empty()) hasComp = true;
    }
    if (ok && hasComp) return true;
  }
  // On-device wrapper shape (verbatim p17 slice): the device emits
  // OverlayHost wrappers plus a Surface scrim around the same layering
  // content markers (TextInput "Layering IME probe", "Surface tone=scrim",
  // Dialog "Layering dialog", BottomSheet). Anchored on those
  // device-emitted comp+label markers, never on test-only strings.
  if (len == 6) {
    static const char* kDevComp[6] = {"OverlayHost", "TextInput",
                                      "OverlayHost", "Surface", "Dialog",
                                      "BottomSheet"};
    static const char* kDevLabel[6] = {"OverlayHost", "Layering IME probe",
                                       "OverlayHost", "Surface tone=scrim",
                                       "Layering dialog", "BottomSheet"};
    bool ok = true;
    for (int i = 0; i < 6; ++i) {
      const GalleryRow& row = rows[static_cast<std::size_t>(first + i)];
      if (row.comp != kDevComp[i] || row.label != kDevLabel[i]) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

// T10 reference-viewport lane (kPresentH): preserved so the committed T10
// hermetic suite stays green unmodified; new callers use the viewportH lane
// with the real window height.
bool PresentDeviceGalleryPage(const std::vector<GalleryRow>& rows, int page,
                               Renderer& renderer, Swapchain& target,
                               std::string* error) {
  return PresentDeviceGalleryPage(rows, page, renderer, target, kPresentH,
                                  error);
}

// T11 page-slicing lane: page range and emission slice use the real
// viewport height shared with the caller's pager.
bool PresentDeviceGalleryPage(const std::vector<GalleryRow>& rows, int page,
                              Renderer& renderer, Swapchain& target,
                              int viewportH, std::string* error) {
  try {
    if (viewportH <= 0) {
      SetPresentError(error, "present window: bad viewport height");
      return false;
    }
    if (rows.empty()) {
      SetPresentError(error, "present window: no gallery rows");
      return false;
    }
    // T11 fix 2: ONE viewport height — the range check uses the same pager
    // the caller wraps with, so pages valid under the real window height
    // present here too. Out-of-range pages fail closed before any cmd is
    // emitted (nothing submitted, buffer untouched by this entry).
    const int pages = GalleryPageCount(rows, viewportH);
    if (page < 0 || page >= pages) {
      SetPresentError(error, "present window: page out of range");
      return false;
    }
    // Fail-closed shaping gate (mirrors PresentGallery): every non-empty
    // label must resolve to a nonzero engine-stack run handle before any
    // cmd is emitted. Noncharacters never resolve (no covering face) and
    // shaping failures yield run 0: both legs report a diagnostic and
    // record nothing, never a gallery bitmap and never tofu.
    for (const GalleryRow& row : rows) {
      const std::string& label =
          row.comp.empty() ? row.label : ShortCaption(row);
      if (label.empty()) continue;
      if (LabelHasNoncharacter(label) || ShapeGalleryLabel(label) == 0) {
        SetPresentError(error,
                        "gallery label shaping failed (fail-closed: no bitmap)");
        return false;
      }
    }
    // T11 fix 2: emit exactly the requested page's slice [starts[page],
    // starts[page+1]) — never the full list — so consecutive pages present
    // pixel-different frames instead of one frozen frame.
    const std::vector<int> starts = PageStarts(rows, viewportH);
    RealizedWindow window;
    window.firstVisible = starts[static_cast<std::size_t>(page)];
    window.lastVisible =
        starts[static_cast<std::size_t>(page) + 1] - 1;
    window.prefetchAhead = 0;
    {
      std::string compList;
      std::string labelList;
      for (int i = window.firstVisible; i <= window.lastVisible; ++i) {
        if (i > window.firstVisible) {
          compList += "|";
          labelList += "|";
        }
        compList += rows[static_cast<std::size_t>(i)].comp;
        labelList += rows[static_cast<std::size_t>(i)].label;
      }
#ifdef __ANDROID__
      __android_log_print(ANDROID_LOG_INFO, "GalleryPage",
                          "[layering] slice page=%d rows=[%d,%d] comps={%s} "
                          "labels={%s}",
                          page, window.firstVisible, window.lastVisible,
                          compList.c_str(), labelList.c_str());
#else
      std::printf("[layering] slice page=%d rows=[%d,%d] comps={%s} labels={%s}\n",
                  page, window.firstVisible, window.lastVisible,
                  compList.c_str(), labelList.c_str());
#endif
    }
    // L8a dispatch (FR-M3-010 lane): the layering-page slice presents via
    // paintLayeringPage (composited scrim + five contents, submission
    // order) instead of the flat-row emitter. All other pages fall
    // through unchanged; no pager/slice change.
    if (IsLayeringPageSlice(rows, window.firstVisible, window.lastVisible)) {
      LayeringScene scene;
      scene.valid = true;
      // T4 (BUG-RUNTIME-004 / AC-M3-022 sink leg, AC-LAYOUT-035): bind the
      // sheet to the live FR-LAYOUT-012 inset (`deps.insets.imeBottom`) every
      // frame before paint. In-place sheet-Y mutation only, no full-tree
      // relayout; a zero inset is a no-op via the anchor's own clamp.
      const float live_inset_px =
          PresentDeps().insets.imeBottom.load(std::memory_order_relaxed);
      anchorSheetToImeInset(scene, live_inset_px);
      paintLayeringPage(scene, renderer, target, error);
      if (error != nullptr && !error->empty()) return false;
      // L8b dispatch confirm: names the routed page and shape count,
      // distinct from the slice diagnostic above (which says "slice").
#ifdef __ANDROID__
      __android_log_print(ANDROID_LOG_INFO, "GalleryPage",
                          "[layering] paint layering page=%d shapes=6", page);
#else
      std::printf("[layering] paint layering page=%d shapes=6\n", page);
#endif
      return true;
    }
    std::vector<DrawCmd> cmds;
    cmds.reserve(rows.size() * 2 + 2);
    EmitGalleryWindowCmds(rows, window, cmds);
    // Rebase the slice to the viewport origin: the windowed emitter lays
    // rows at virtual-canvas offsets, so later pages would paint outside
    // [0, viewportH). Page 0 is unchanged (zero shift, T10-compatible).
    float cursorFirst = static_cast<float>(kPad);
    for (int i = 0; i < window.firstVisible; ++i) {
      cursorFirst += static_cast<float>(
                         RowHeight(rows[static_cast<std::size_t>(i)])) +
                     static_cast<float>(kGap);
    }
    const float rebaseDy = cursorFirst - static_cast<float>(kPad);
    if (rebaseDy != 0.0f) {
      for (auto& cmd : cmds) cmd.bounds.y -= rebaseDy;
    }
    // Present caption runs as vector text: Text cmds carrying a real run
    // id ride the ShapedText leg so the retained vector-text path paints
    // them from cached coverage (glyphRunId == 0 stays on the Text leg,
    // which draw forwards without the UnknownPrimitive trip).
    for (auto& cmd : cmds) {
      if (cmd.kind == PrimitiveKind::Text && cmd.payload.glyphRunId != 0) {
        cmd.kind = PrimitiveKind::ShapedText;
      }
    }
    StagingSwapchain staged;
    const DrawError derr = renderer.draw(cmds, staged);
    if (derr != DrawError::None) {
      if (derr == DrawError::UnsupportedBaseline &&
          !renderer.lastError().empty()) {
        if (error != nullptr) *error = renderer.lastError();
      } else {
        SetPresentError(error, "retained present rejected primitive");
      }
      return false;
    }
    // T11 per-run raster validation in bounds: every presented run must
    // resolve to real coverage on the Swapchain path (unknown/dangling
    // handles yield !ok() — never synthesized — and fail the present
    // closed, recording nothing).
    for (const auto& cmd : staged.cmds) {
      if (cmd.kind != PrimitiveKind::ShapedText) continue;
      const muse::text::StatusOr<CoverageMask> coverage = RasterizeShapedText(
          cmd.payload.glyphRunId, cmd.bounds, cmd.payload.textColor);
      if (!coverage.ok()) {
        SetPresentError(error, "shaped run rasterization failed");
        return false;
      }
    }
    for (const auto& cmd : staged.cmds) target.submit(cmd);
    return true;
  } catch (...) {
    SetPresentError(error, "present failed (exception)");
    return false;
  }
}

// ---- Layering page (L2: BUG-M3-FIDELITY-008 / FR-M3-010 / AC-M3-022) ----
//
// Single-OverlayHost scene: one dim scrim plus the five overlay contents in
// submission order (Dialog, BottomSheet, Menu, SnackbarHost, Tooltip).
// Extends the retained OverlayScrim mapping to this scene: the scrim is the
// only OverlayScrim here, so BottomSheet/SnackbarHost layer as content over
// it (FamilyKind's per-row mapping is untouched). z-order is submission
// order — later submissions draw above. The sheet content rides the
// FR-LAYOUT-012 animated inset per frame (scene.sheetY, bound by
// anchorSheetToImeInset): no one-off copy, no full-tree relayout marker.
// The CPU-mock path is this same geometry (identical bounds/node order),
// so hermetic checks observe the retained ordering directly.
void paintLayeringPage(const LayeringScene& scene, Renderer& renderer,
                       Swapchain& target, std::string* error) {
  try {
    if (!scene.valid) {
      if (error != nullptr) *error = "layering scene invalid (no-paint)";
      return;
    }
    std::vector<DrawCmd> cmds;
    cmds.reserve(6);
    DrawCmd scrim;
    scrim.kind = PrimitiveKind::OverlayScrim;
    scrim.bounds = {0.0f, 0.0f, static_cast<float>(kPresentW),
                    static_cast<float>(kPresentH)};
    scrim.nodeId = 1;
    scrim.opacity = 1.0f;
    scrim.payload.scrimColor = kLightInverseSurface;
    scrim.payload.scrimAlpha = 0.25f;
    cmds.push_back(scrim);
    // Five overlay contents in submission order; strictly increasing node
    // ids so later submission draws above. BottomSheet (index 1) rides the
    // live sheetY bound per frame by anchorSheetToImeInset. Each content
    // carries an explicit opaque fill (F1: no payload-0 washout — the
    // retained Swapchain rasterizes Rect/RoundedRect from stateColor, so a
    // default 0 payload would present as transparent/washed out).
    for (std::uint32_t i = 0; i < 5; ++i) {
      DrawCmd content;
      content.kind =
          (i == 3) ? PrimitiveKind::Rect : PrimitiveKind::RoundedRect;
      float y = 240.0f + static_cast<float>(i) * 160.0f;
      if (i == 1) y = scene.sheetY;
      content.bounds = {64.0f, y, static_cast<float>(kPresentW - 128), 120.0f};
      if (content.kind == PrimitiveKind::RoundedRect) content.radius = 20.0f;
      content.nodeId = 2 + i;
      content.opacity = 1.0f;
      // L8b per-overlay identity: five contents ride no text channel (Rect /
      // RoundedRect only, glyphRunId/textColor stay 0), so identity is the
      // pairwise-distinct (stateColor, elevationDp) fill per overlay.
      switch (i) {
        case 0:  // Dialog
          content.payload.stateColor = kWhite;
          content.payload.elevationDp = 1.0f;
          break;
        case 1:  // BottomSheet (rides the live sheetY inset above)
          content.payload.stateColor = kLightSurfaceVariant;
          content.payload.elevationDp = 3.0f;
          break;
        case 2:  // Menu
          content.payload.stateColor = kLightSecondaryContainer;
          content.payload.elevationDp = 6.0f;
          break;
        case 3:  // SnackbarHost (inverse-surface container)
          content.payload.stateColor = kLightInverseSurface;
          content.payload.elevationDp = 8.0f;
          break;
        default:  // Tooltip
          content.payload.stateColor = kLightTertiaryContainer;
          content.payload.elevationDp = 2.0f;
          break;
      }
      content.payload.stateOpacity = 1.0f;
      cmds.push_back(content);
    }
    DrawError derr = renderer.draw(cmds, target);
    if (derr != DrawError::None) {
      if (error != nullptr) *error = "layering present rejected primitive";
    }
  } catch (...) {
    if (error != nullptr) *error = "layering paint failed (exception)";
  }
}

void anchorSheetToImeInset(LayeringScene& scene, float insetPx) {
  try {
    if (!std::isfinite(insetPx)) {
      insetPx = 0.0f;
    }
    if (insetPx < 0.0f) insetPx = 0.0f;
    // Per-frame bind of the animated layout inset: sheet tracks the value
    // exactly; fullRelayouts stays untouched (no full-tree relayout).
    scene.sheetY = insetPx;
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "GalleryPage",
                        "[layering] per-frame evidence sheetY=%.1f inset=%.1f "
                        "relayout=none fullRelayouts=%u",
                        static_cast<double>(scene.sheetY),
                        static_cast<double>(insetPx), scene.fullRelayouts);
#else
    std::printf(
        "[layering] per-frame evidence sheetY=%.1f inset=%.1f relayout=none "
        "fullRelayouts=%u\n",
        static_cast<double>(scene.sheetY), static_cast<double>(insetPx),
        scene.fullRelayouts);
#endif
  } catch (...) {
    // Never throws across the boundary; keep last good sheetY on failure.
  }
}

// ---- Scrim-tap-dismiss (L3: FR-M3-010 / AC-M3-022 scrim-tap-dismiss clause,
// AC-INPUT-020) ----
//
// Layering-page-only routing over the already resolved scrim hit-target.
// Rides the real post-007 tap chain: the arena/slop stage governs
// hit-testing upstream, so by the time this entry runs `scrimHit` already
// distinguishes scrim taps from content mis-hits and `isDrag` already marks
// drags. Dismiss stays narrowly scoped to the scrim hit-target — content
// taps and drags resolve { dismissed: 'none', reason: 'miss' | 'drag' }
// without touching the overlay stack — so drag-no-fire holds. Dismiss pops
// exactly the topmost overlay once (remaining decrements by one).
DismissResult onScrimTap(bool scrimHit, bool isDrag,
                         int overlayCount) noexcept {
  try {
    if (overlayCount < 0) overlayCount = 0;
    if (isDrag) {
      DismissResult out;
      out.dismissed = "none";
      out.remaining = overlayCount;
      out.reason = "drag";
      return out;
    }
    if (!scrimHit || overlayCount == 0) {
      DismissResult out;
      out.dismissed = "none";
      out.remaining = overlayCount;
      out.reason = "miss";
      return out;
    }
    DismissResult out;
    out.dismissed = "topmost";
    out.remaining = overlayCount - 1;
    out.reason = "";
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "GalleryPage",
                        "[layering] dismiss=topmost remaining=%d",
                        out.remaining);
#else
    std::printf("[layering] dismiss=topmost remaining=%d\n", out.remaining);
#endif
    return out;
  } catch (...) {
    DismissResult out;
    out.dismissed = "none";
    out.remaining = 0;
    out.reason = "miss";
    return out;
  }
}

}  // namespace runtime
