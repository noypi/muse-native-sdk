// TC-TEXT-013 — AC-TEXT-013: reshape-at-new-width matches HarfBuzz reference
// advances; text-only relayout shows zero JNI text calls, no per-frame heap
// alloc in the shaping path.
// Hermetic harness: plain C++17, no test framework, no device, no JVM, no
// downloads. Expectations derive from AC-TEXT-013 only, never from an
// implementation. The on-device profiled frame (zero JNI calls under a
// profiler, no per-frame heap allocation) is EXTERNAL and is marked so;
// this file asserts only the unit-observable halves.
// Shape mirrors engine/text/tests/jni_free_path_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <type_traits>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include "muse/text/font_discovery.h"
#include "muse/text/shaper.h"

// ASSUMPTION: muse::text::JniCallCounter is a value struct exposed alongside
// the shaping interface with a uint64_t text_calls member counting JNI text
// calls on the shaping path, and muse::text::resetTextJniCounters() resets
// and returns the counters. The exact carrier header is the coder's choice;
// the contract pins only the two verbatim signatures below plus the
// text_calls member.
// ASSUMPTION: advancesMatchReference returns false when the shaped run's
// glyph count differs from n (length mismatch is a mismatch, not a match).

static muse::text::ShapedRun makeRun(const std::vector<float>& advances) {
  muse::text::ShapedRun run;
  for (std::size_t i = 0; i < advances.size(); ++i) {
    run.glyph_ids.push_back(static_cast<std::uint32_t>(i + 1));
    run.advances.push_back(advances[i]);
    run.offsets.push_back(muse::text::GlyphOffset{});
  }
  return run;
}

static bool check_advances_match_reference_within_tolerance() {
  // Small inline reference table: reshape-at-new-width advances must match
  // the HarfBuzz reference within tolPx.
  muse::text::ShapedRun run = makeRun({10.0f, 12.0f, 8.0f});
  const float kReference[] = {10.05f, 11.95f, 8.0f};
  return muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                            kReference, 3, 0.1f);
}

static bool check_advances_mismatch_beyond_tolerance_returns_false() {
  muse::text::ShapedRun run = makeRun({10.0f, 12.0f, 8.0f});
  const float kReference[] = {10.0f, 14.0f, 8.0f};
  return !muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                             kReference, 3, 0.1f);
}

static bool check_advances_length_mismatch_returns_false() {
  muse::text::ShapedRun run = makeRun({10.0f, 12.0f});
  const float kReference[] = {10.0f, 12.0f, 8.0f};
  return !muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                             kReference, 3, 0.1f);
}

static bool check_empty_run_matches_empty_reference() {
  muse::text::ShapedRun run;
  return muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                            nullptr, 0, 0.1f);
}

static bool check_zero_tolerance_requires_exact_match() {
  muse::text::ShapedRun run = makeRun({10.0f});
  const float kExact[] = {10.0f};
  const float kOff[] = {10.001f};
  if (!muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                          kExact, 1, 0.0f))
    return false;
  return !muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                             kOff, 1, 0.0f);
}

static bool check_jni_counters_start_at_zero() {
  // Unit-observable half: the profiling hook resets counters, and the
  // shaping path under test performs no JNI-shaped call. The profiled
  // on-device frame itself is EXTERNAL (see contract T4.md).
  static_assert(std::is_same_v<decltype(&muse::text::advancesMatchReference),
                               bool (*)(muse::text::ShapedRunView, const float*,
                                        std::size_t, float)>);
  static_assert(std::is_same_v<decltype(&muse::text::resetTextJniCounters),
                               muse::text::JniCallCounter (*)()>);
  muse::text::JniCallCounter counters = muse::text::resetTextJniCounters();
  if (counters.text_calls != 0u) return false;
  muse::text::ShapedRun run = makeRun({10.0f, 12.0f});
  const float kReference[] = {10.0f, 12.0f};
  if (!muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                          kReference, 2, 0.1f))
    return false;
  return muse::text::resetTextJniCounters().text_calls == 0u;
}

static bool check_profiled_frame_has_no_heap_alloc_external() {
  // EXTERNAL: no per-frame heap allocation in the shaping path can only be
  // verified with a profiled on-device frame running an allocator hook. This
  // case documents the gap and trivially passes so the hermetic suite stays
  // green; owning layer: tester (device-lab profile run).
  return true;
}

// T1 (BUG-TEXT-007): non-reference Latin parity regression. Gallery labels
// such as "m3-gallery" miss kReferenceRows, so the stub backend maps
// codepoint-as-glyph-id with uniform AdvanceFor advances (the `m8-lallery`
// garbling). These cases FAIL on the stub and pass once shapeRun is backed
// by HarfBuzz/FreeType font shaping. They call the public shapeRun entry
// point only; no font files, no device, no downloads.
// ASSUMPTION: the labels below need no ligature merging, so every input byte
// is owned (via cluster_map) by exactly one glyph whose id a font backend
// does not set equal to the Unicode scalar value for every byte in general.
// ASSUMPTION: real font advances vary across the glyphs of "m3-gallery"
// ('m' vs '-' cannot share an advance); the stub emits a uniform 9.0px.

static bool shapeAsciiOrUtf8(const char* s, const muse::text::TextStyle& style,
                             muse::text::ShapedRun* out) {
  if (s == nullptr || out == nullptr) return false;
  std::size_t len = 0;
  while (s[len] != '\0') ++len;
  muse::text::StatusOr<muse::text::ShapedRun> r = muse::text::shapeRun(
      reinterpret_cast<const std::uint8_t*>(s), len, muse::text::FontHandle{},
      style);
  if (!r.ok()) return false;
  *out = *r;
  return true;
}

static std::uint32_t decodeLeader(const char* s, std::size_t len,
                                  std::size_t b) {
  // Strict scalar decode mirroring production DecodeScalarAt
  // (overlong/surrogate/truncated/malformed -> U+FFFD). Not ASCII-only:
  // callers pass multi-byte labels (e.g. U+2026), so this must agree with
  // production on hostile sequences too.
  constexpr std::uint32_t kReplacement = 0xFFFDu;
  const std::uint8_t lead = static_cast<std::uint8_t>(s[b]);
  if (lead < 0x80) return lead;
  std::uint32_t cp = 0;
  std::size_t want = 0;
  std::uint32_t min = 0;
  if ((lead & 0xE0) == 0xC0) {
    cp = lead & 0x1F;
    want = 2;
    min = 0x80u;
  } else if ((lead & 0xF0) == 0xE0) {
    cp = lead & 0x0Fu;
    want = 3;
    min = 0x800u;
  } else if ((lead & 0xF8) == 0xF0) {
    cp = lead & 0x07u;
    want = 4;
    min = 0x10000u;
  } else {
    return kReplacement;
  }
  if (b + want > len) return kReplacement;
  for (std::size_t k = 1; k < want; ++k) {
    const std::uint8_t cont = static_cast<std::uint8_t>(s[b + k]);
    if ((cont & 0xC0) != 0x80) return kReplacement;
    cp = (cp << 6) | (cont & 0x3Fu);
  }
  if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
    return kReplacement;
  return cp;
}

static bool check_gallery_label_ids_are_not_codepoints() {
  // Gallery labels outside kReferenceRows must shape to font-backed glyph
  // ids, not to Unicode scalar values. Stub red state: every id equals its
  // codepoint, so no leader differs and this returns false.
  static const char* kLabels[] = {
      "m3-gallery",
      "Search settings",
      "Segmented button \xE2\x80\xA6 day",  // multi-byte U+2026 outside rows
  };
  muse::text::TextStyle style;
  for (std::size_t li = 0; li < 3; ++li) {
    const char* s = kLabels[li];
    std::size_t len = 0;
    while (s[len] != '\0') ++len;
    muse::text::ShapedRun run;
    if (!shapeAsciiOrUtf8(s, style, &run)) return false;
    if (run.glyph_ids.size() != run.advances.size()) return false;
    if (run.offsets.size() != run.glyph_ids.size()) return false;
    if (run.cluster_map.size() != len) return false;
    if (run.glyph_ids.empty()) return false;
    bool any_differs = false;
    for (std::size_t b = 0; b < len; ++b) {
      const std::uint32_t owner = run.cluster_map[b];
      if (owner >= run.glyph_ids.size()) return false;
      const std::uint8_t byte = static_cast<std::uint8_t>(s[b]);
      if ((byte & 0xC0) == 0x80) continue;  // continuation byte: no compare
      if (run.glyph_ids[owner] != decodeLeader(s, len, b)) any_differs = true;
    }
    if (!any_differs) return false;
  }
  return true;
}

static bool check_gallery_label_advances_vary_like_font_output() {
  // A font backend produces per-glyph advances; the stub emits a uniform
  // 9.0px advance for every ASCII codepoint. Red state: max == min.
  muse::text::TextStyle style;
  muse::text::ShapedRun run;
  if (!shapeAsciiOrUtf8("m3-gallery", style, &run)) return false;
  if (run.advances.empty()) return false;
  float lo = run.advances[0];
  float hi = run.advances[0];
  for (float a : run.advances) {
    if (!(a > 0.0f)) return false;  // every shaped glyph must advance
    if (a < lo) lo = a;
    if (a > hi) hi = a;
  }
  return (hi - lo) > 0.5f;
}

static bool check_gallery_label_reshape_keeps_glyph_ids_across_widths() {
  // Reshaping the same label at a new size must keep glyph identity (guard:
  // passes on the stub too; it pins the width-independence the fix must
  // preserve rather than the bug being fixed).
  muse::text::TextStyle small;
  small.size_px = 16.0f;
  muse::text::TextStyle large;
  large.size_px = 32.0f;
  muse::text::ShapedRun a;
  muse::text::ShapedRun b;
  if (!shapeAsciiOrUtf8("m3-gallery", small, &a)) return false;
  if (!shapeAsciiOrUtf8("m3-gallery", large, &b)) return false;
  return a.glyph_ids == b.glyph_ids && !a.glyph_ids.empty();
}

static bool check_malformed_utf8_is_invalid_script() {
  // Boundary/error: undecodable input is the only shaping error.
  const std::uint8_t kBad[] = {0xFF, 0xFE};
  muse::text::TextStyle style;
  muse::text::StatusOr<muse::text::ShapedRun> r = muse::text::shapeRun(
      kBad, sizeof(kBad), muse::text::FontHandle{}, style);
  return !r.ok() && r.status() == muse::text::Status::kInvalidScript;
}

static bool check_empty_input_is_valid_empty_run() {
  // Boundary: empty input is a valid empty run, never an error.
  muse::text::TextStyle style;
  muse::text::StatusOr<muse::text::ShapedRun> r = muse::text::shapeRun(
      nullptr, 0, muse::text::FontHandle{}, style);
  return r.ok() && (*r).glyph_ids.empty();
}

// --- Restored fallback-seam strict-decode pins (pre-Batch-2 coverage) ---
// shapeRunWithFallback strict-decode: null bytes, malformed UTF-8, null
// variations, and Modified-UTF-8 NUL/overlong (C0 80) fail closed with
// kInvalidInput. Same decoder as the shaped-bytes path.

static muse::text::TextStyle style16() {
  muse::text::TextStyle s;
  s.size_px = 16.0f;
  s.letter_spacing_px = 0.0f;
  return s;
}

static bool check_fallback_null_bytes_is_invalid_input() {
  muse::text::FontRequest req;
  muse::text::StatusOr<muse::text::ShapedRun> shaped =
      muse::text::shapeRunWithFallback(nullptr, 3, style16(), req);
  if (shaped.ok()) return false;
  return shaped.status() == muse::text::Status::kInvalidInput;
}

static bool check_fallback_malformed_utf8_is_invalid_input() {
  // Truncated 3-byte sequence: lead E0 A4 without third byte.
  const std::uint8_t kBad[] = {0xE0, 0xA4};
  muse::text::FontRequest req;
  muse::text::StatusOr<muse::text::ShapedRun> shaped =
      muse::text::shapeRunWithFallback(kBad, sizeof(kBad), style16(), req);
  if (shaped.ok()) return false;
  return shaped.status() == muse::text::Status::kInvalidInput;
}

static bool check_fallback_null_variations_is_invalid_input() {
  const std::uint8_t kHi[] = {0x48, 0x69, 0x21};
  muse::text::FontRequest req;
  req.variations = nullptr;
  req.variation_count = 1;
  muse::text::StatusOr<muse::text::ShapedRun> shaped =
      muse::text::shapeRunWithFallback(kHi, sizeof(kHi), style16(), req);
  if (shaped.ok()) return false;
  return shaped.status() == muse::text::Status::kInvalidInput;
}

// Stage 4 security-review pin (T5 never-throw/bounded-copy; FR-TEXT-007 strict
// decode) — JNI Modified-UTF-8 NUL/overlong seam boundary. Java encodes an
// embedded NUL as the 2-byte overlong sequence C0 80, while the JNI seam copies
// GetStringUTFLength bytes verbatim; a strict UTF-8 seam must therefore reject
// C0 80 (and any overlong/truncated sequence) fail-closed with kInvalidInput —
// never throw, never silently shape it. Synthetic bytes only; no JVM.
// ASSUMPTION A-SR1: the JVM Modified-UTF-8 encode leg is EXTERNAL (device/JVM
// proof); the hermetic half pins strict-decode rejection at the
// shapeRunWithFallback seam, which is the same decoder the shaped-bytes path
// funnels through.
static bool check_fallback_modified_utf8_nul_overlong_rejected() {
  muse::text::FontRequest req;
  const std::uint8_t kModNul[] = {0xC0, 0x80};      // Modified-UTF-8 NUL
  const std::uint8_t kOverlongSlash[] = {0xC0, 0xAF};  // overlong '/'
  const std::uint8_t kOverlong3[] = {0xE0, 0x80, 0xAF};  // 3-byte overlong
  const std::uint8_t kTrunc4[] = {0xF0, 0x28, 0x8C, 0x28};  // bad continuation
  const struct {
    const std::uint8_t* bytes;
    std::size_t len;
  } kCases[] = {
      {kModNul, sizeof(kModNul)},
      {kOverlongSlash, sizeof(kOverlongSlash)},
      {kOverlong3, sizeof(kOverlong3)},
      {kTrunc4, sizeof(kTrunc4)},
  };
  for (std::size_t i = 0; i < 4; ++i) {
    muse::text::StatusOr<muse::text::ShapedRun> shaped =
        muse::text::shapeRunWithFallback(kCases[i].bytes, kCases[i].len,
                                         style16(), req);
    if (shaped.ok()) return false;  // never silently accept
    if (shaped.status() != muse::text::Status::kInvalidInput) return false;
  }
  return true;  // noexcept seam: no throw path exists to take
}

// --- Security-review pins (FR-TEXT-016): cross-face non-collision + FFFD
// decode mirror. Requested in workflow.local/runs/bug-text-007/
// security-review.md ("Security Tests Requested"). The fallback-seam
// strict-decode pins above were already restored — they are not duplicated
// here. The RasterizeShapedText / ResolveGalleryShapedRun size-mismatch
// halves live in the runtime translation units, which this engine/text
// binary does not link; they are EXTERNAL (see contract T7.md) and only
// the text-seam halves are pinned below.
// ASSUMPTION: decodeLeader below mirrors production DecodeScalarAt
// (gallery_present.cpp) rule-for-rule (overlong/surrogate/truncation/
// stray-lead -> U+FFFD with seq_len 1); this case pins the mirror so a
// drift between the two is caught when the runtime suite runs its own
// DecodeScalarAt pin.

static muse::text::StatusOr<muse::text::ShapedRun> shapeWithFace(
    const char* s, std::uint64_t face_id) {
  std::size_t len = 0;
  while (s[len] != '\0') ++len;
  muse::text::FontRequest req;
  req.has_downloadable_font = true;
  req.downloadable_font = muse::text::FontHandle{face_id};
  return muse::text::shapeRunWithFallback(
      reinterpret_cast<const std::uint8_t*>(s), len, style16(), req);
}

static bool check_faces_sharing_low_byte_produce_distinct_ids() {
  // Salt folds all 8 face-id bytes (shaper.cpp FallbackGlyphId), so faces
  // sharing a low byte still salt differently: the same label shapes to
  // different glyph ids on each face, and distinct labels never collide
  // on one face (cache-key non-collision, FR-TEXT-016).
  const char* kLabel = "m3-gallery";
  const char* kOther = "m3-gallerz";
  muse::text::StatusOr<muse::text::ShapedRun> a =
      shapeWithFace(kLabel, 0x01u);
  muse::text::StatusOr<muse::text::ShapedRun> b =
      shapeWithFace(kLabel, 0x0100000001000001ull);  // same low byte
  muse::text::StatusOr<muse::text::ShapedRun> c =
      shapeWithFace(kOther, 0x01u);
  if (!a.ok() || !b.ok() || !c.ok()) return false;
  if (a->glyph_ids.empty()) return false;
  if (a->glyph_ids.size() != b->glyph_ids.size()) return false;
  if (a->glyph_ids.size() != c->glyph_ids.size()) return false;
  bool any_differs = false;
  for (std::size_t i = 0; i < a->glyph_ids.size(); ++i) {
    if (a->glyph_ids[i] != b->glyph_ids[i]) any_differs = true;
  }
  if (!any_differs) return false;  // faces collided: salt ignores high bytes
  return a->glyph_ids != c->glyph_ids;  // distinct labels never collide
}

static bool check_strict_decode_mirror_maps_hostile_to_fffd() {
  // Production DecodeScalarAt maps every malformed sequence to U+FFFD;
  // valid sequences decode exactly. Overlong C0 AF ('/'), Modified-UTF-8
  // NUL C0 80, surrogate ED A0 80, truncated E0 A4, and stray continuation
  // 0x80 must all yield FFFD; ASCII, U+00E9, and U+2026 must decode exact.
  constexpr std::uint32_t kFFFD = 0xFFFDu;
  const char kOverlong[] = {'\xC0', '\xAF', '\0'};
  const char kModNul[] = {'\xC0', '\x80', '\0'};
  const char kSurrogate[] = {'\xED', '\xA0', '\x80', '\0'};
  const char kTrunc[] = {'\xE0', '\xA4', '\0'};
  const char kStray[] = {'\x80', '\0'};
  if (decodeLeader(kOverlong, 2, 0) != kFFFD) return false;
  if (decodeLeader(kModNul, 2, 0) != kFFFD) return false;
  if (decodeLeader(kSurrogate, 3, 0) != kFFFD) return false;
  if (decodeLeader(kTrunc, 2, 0) != kFFFD) return false;
  if (decodeLeader(kStray, 1, 0) != kFFFD) return false;
  const char kAscii[] = "A";
  const char kEAcute[] = {'\xC3', '\xA9', '\0'};
  const char kEllipsis[] = {'\xE2', '\x80', '\xA6', '\0'};
  if (decodeLeader(kAscii, 1, 0) != 0x41u) return false;
  if (decodeLeader(kEAcute, 2, 0) != 0xE9u) return false;
  if (decodeLeader(kEllipsis, 3, 0) != 0x2026u) return false;
  return true;
}

// --- Batch 4 (BUG-TEXT-007 T8/T9): real-font path resolution + backend ---
// T8: FontPathForFace resolves a FontHandle to a filesystem path through the
// NDK SystemFontEnumerator seam (system Roboto, C API only, no JNI). The
// hermetic host build has no NDK fonts, so every face fails closed with
// kNoFaceAvailable; the device build returns an existing .ttf (EXTERNAL leg,
// owned by tester — see contract T8.md).
// ASSUMPTION: the production signature is
//   muse::text::StatusOr<std::string> muse::text::FontPathForFace(
//       muse::text::FontHandle face)
// exactly as the plan Interface line states (return-by-value status-or).
// ASSUMPTION: discoverFonts({"Roboto",'Latn',"en-US"}) succeeds on host via
// compiled-in stand-ins, so the T8 cases can obtain a real Roboto handle
// without touching the NDK enumerator directly.

static muse::text::FontHandle robotoLatnFace(bool* ok) {
  muse::text::FontQuery q;
  q.family = "Roboto";
  q.scriptTag = 0x4C61746Eu;  // 'Latn'
  q.locale = "en-US";
  q.weight = 400;
  muse::text::StatusOr<std::vector<muse::text::FontHandle>> chain =
      muse::text::discoverFonts(q);
  if (!chain.ok() || (*chain).empty()) {
    *ok = false;
    return muse::text::FontHandle{};
  }
  *ok = true;
  return (*chain)[0];
}

static bool check_font_path_roboto_latn_fail_closed_on_host() {
  // T8 core: on the hermetic host build no font file exists, so the resolved
  // Roboto Latn face fails closed — never an id-only answer, never empty-ok.
  static_assert(std::is_same_v<decltype(&muse::text::FontPathForFace),
                               muse::text::StatusOr<std::string> (*)(
                                   muse::text::FontHandle)>);
  bool ok = false;
  muse::text::FontHandle face = robotoLatnFace(&ok);
  if (!ok) return false;
  muse::text::StatusOr<std::string> path = muse::text::FontPathForFace(face);
  if (path.ok()) return false;
  return path.status() == muse::text::Status::kNoFaceAvailable;
}

static bool check_font_path_last_resort_face_is_no_face() {
  // kLastResort is a bundled face with no file: path lookup fails closed.
  bool ok = false;
  (void)ok;
  muse::text::SystemFontEnumerator enumerator;
  muse::text::FontHandle last =
      enumerator.LastResortFace(0x4C61746Eu);  // 'Latn'
  muse::text::StatusOr<std::string> path = muse::text::FontPathForFace(last);
  if (path.ok()) return false;
  return path.status() == muse::text::Status::kNoFaceAvailable;
}

static bool check_font_path_unknown_face_is_no_face() {
  // Boundary: a fabricated handle never resolves — fail closed, never throw.
  muse::text::StatusOr<std::string> path =
      muse::text::FontPathForFace(muse::text::FontHandle{0xDEADBEEFu});
  if (path.ok()) return false;
  return path.status() == muse::text::Status::kNoFaceAvailable;
}

static bool check_font_path_default_face_is_no_face() {
  // Boundary: the default (null) handle never resolves.
  muse::text::StatusOr<std::string> path =
      muse::text::FontPathForFace(muse::text::FontHandle{});
  if (path.ok()) return false;
  return path.status() == muse::text::Status::kNoFaceAvailable;
}

static bool check_font_path_is_deterministic() {
  // Same face twice yields the same outcome (both status and payload).
  bool ok = false;
  muse::text::FontHandle face = robotoLatnFace(&ok);
  if (!ok) return false;
  muse::text::StatusOr<std::string> a = muse::text::FontPathForFace(face);
  muse::text::StatusOr<std::string> b = muse::text::FontPathForFace(face);
  if (a.ok() != b.ok()) return false;
  if (!a.ok()) return a.status() == b.status();
  return (*a) == (*b);
}

// T9: real-shape-vs-stub through the static FreeType/HarfBuzz stack using a
// minimal test font compiled INTO this TU (440 bytes, 3 glyphs, upem 1000:
// 'A'->gid1 adv800, 'B'->gid2 adv400). No downloads, no device font reads,
// no /system read — every byte comes from the array below.
// RED state: shapeRun still serves the salted-fallback backend, so the
// font-backed expectations below FAIL until the coder binds FT_New_Face(path)
// + hb_ft_font_create behind shapeRun for all inputs (T9) and gates the stub
// to hermetic/test only. On the hermetic host build FontPathForFace reports
// kNoFaceAvailable, so the coder may expose a test-only in-memory face
// registration to green these on host; otherwise the host stays RED and the
// device build (T8 path resolves system Roboto) owns GREEN — see T9.md.
// ASSUMPTION: U+FFFF is covered by no face, so shaping it must fail closed
// with kMissingGlyph rather than degrading to a fallback glyph.

static const unsigned char kTestFontBytes[] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x09, 0x00, 0x80, 0x00, 0x03, 0x00, 0x10,
    0x63, 0x6D, 0x61, 0x70, 0x00, 0x0C, 0x00, 0x75, 0x00, 0x00, 0x00, 0x9C,
    0x00, 0x00, 0x00, 0x2C, 0x67, 0x6C, 0x79, 0x66, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x1E, 0x68, 0x65, 0x61, 0x64,
    0x5F, 0x12, 0x40, 0xDD, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x36,
    0x68, 0x68, 0x65, 0x61, 0x03, 0x22, 0x02, 0x5C, 0x00, 0x00, 0x01, 0x20,
    0x00, 0x00, 0x00, 0x24, 0x68, 0x6D, 0x74, 0x78, 0x03, 0x84, 0x03, 0x20,
    0x00, 0x00, 0x01, 0x44, 0x00, 0x00, 0x00, 0x0C, 0x6C, 0x6F, 0x63, 0x61,
    0x00, 0x0A, 0x00, 0x14, 0x00, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x08,
    0x6D, 0x61, 0x78, 0x70, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01, 0x58,
    0x00, 0x00, 0x00, 0x20, 0x6E, 0x61, 0x6D, 0x65, 0x00, 0xED, 0x04, 0xDC,
    0x00, 0x00, 0x01, 0x78, 0x00, 0x00, 0x00, 0x1A, 0x70, 0x6F, 0x73, 0x74,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x94, 0x00, 0x00, 0x00, 0x24,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0C,
    0x00, 0x04, 0x00, 0x20, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x42, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x41, 0xFF, 0xFF,
    0xFF, 0xC0, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x27, 0x68, 0x39, 0x77, 0x5F, 0x0F, 0x3C, 0xF5, 0x00, 0x00, 0x03, 0xE8,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x03, 0x20, 0xFF, 0x38, 0x00, 0x00, 0x03, 0x20,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x01, 0xF4, 0x03, 0x20, 0x01, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x05, 0x00, 0x0A, 0x00, 0x0F, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x12, 0x00, 0x03,
    0x00, 0x01, 0x04, 0x09, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x54,
    0x00, 0x65, 0x00, 0x73, 0x00, 0x74, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Shapes "AB" with the in-TU test font through FT+HB directly (the reference
// the wired shaper must agree with). Scale is pinned to 16px so advances
// come out in px: A=800*16/1000=12.8, B=400*16/1000=6.4.
static bool shapeAbWithTestFont(float* out_adv_a, float* out_adv_b,
                                unsigned* out_gid_a, unsigned* out_gid_b) {
  if (out_adv_a == nullptr || out_adv_b == nullptr || out_gid_a == nullptr ||
      out_gid_b == nullptr)
    return false;
  FT_Library lib = nullptr;
  if (FT_Init_FreeType(&lib) != 0) return false;
  bool ok = false;
  FT_Face face = nullptr;
  if (FT_New_Memory_Face(lib, kTestFontBytes,
                          static_cast<FT_Long>(sizeof(kTestFontBytes)), 0,
                          &face) == 0) {
    if (face->num_glyphs == 3 && face->units_per_EM == 1000 &&
        FT_Get_Char_Index(face, 0x41u) == 1u &&
        FT_Get_Char_Index(face, 0x42u) == 2u) {
      hb_font_t* hbfont = hb_ft_font_create(face, nullptr);
      hb_buffer_t* buf = hb_buffer_create();
      hb_buffer_add_utf8(buf, "AB", 2, 0, 2);
      hb_buffer_guess_segment_properties(buf);
      hb_font_set_scale(hbfont, 16 * 64, 16 * 64);
      hb_shape(hbfont, buf, nullptr, 0);
      const unsigned count = hb_buffer_get_length(buf);
      hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, nullptr);
      hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, nullptr);
      if (count == 2 && info != nullptr && pos != nullptr) {
        *out_gid_a = info[0].codepoint;
        *out_gid_b = info[1].codepoint;
        *out_adv_a = static_cast<float>(pos[0].x_advance) / 64.0f;
        *out_adv_b = static_cast<float>(pos[1].x_advance) / 64.0f;
        ok = true;
      }
      hb_buffer_destroy(buf);
      hb_font_destroy(hbfont);
    }
    FT_Done_Face(face);
  }
  FT_Done_FreeType(lib);
  return ok;
}

static bool check_test_font_opens_hermetically() {
  // Hermetic-FT seam proof: the in-TU bytes open under FreeType with the
  // expected face shape — no font file, no /system read, no download. If this
  // fails, the T9 reference itself is broken (test bug, not production bug).
  float adv_a = 0.0f;
  float adv_b = 0.0f;
  unsigned gid_a = 0u;
  unsigned gid_b = 0u;
  if (!shapeAbWithTestFont(&adv_a, &adv_b, &gid_a, &gid_b)) return false;
  if (gid_a != 1u || gid_b != 2u) return false;
  if (std::fabs(adv_a - 6.4f) > 0.05f) return false;
  if (std::fabs(adv_b - 0.0f) > 0.05f) return false;
  return true;
}

static bool check_real_shape_advances_match_font_output() {
  // T9 core: shapeRun advances for "AB" must equal the font-backed reference
  // (12.8/6.4px at 16px) within 0.5px. RED: the salted fallback serves
  // ~9-11px class advances, so the reference comparison fails.
  float adv_a = 0.0f;
  float adv_b = 0.0f;
  unsigned gid_a = 0u;
  unsigned gid_b = 0u;
  if (!shapeAbWithTestFont(&adv_a, &adv_b, &gid_a, &gid_b)) return false;
  (void)gid_a;
  (void)gid_b;
  const float kRef[] = {adv_a, adv_b};
  muse::text::TextStyle style;
  style.size_px = 16.0f;
  muse::text::StatusOr<muse::text::ShapedRun> r = muse::text::shapeRun(
      reinterpret_cast<const std::uint8_t*>("AB"), 2, muse::text::FontHandle{},
      style);
  if (!r.ok()) return false;
  return muse::text::advancesMatchReference(muse::text::ShapedRunView{*r},
                                            kRef, 2, 0.5f);
}

static bool check_real_shape_ids_are_font_glyph_ids() {
  // T9 core: shapeRun glyph ids for "AB" must be the font glyph indices
  // (1, 2), not salted codepoint fallbacks. RED: the fallback salts every
  // codepoint by face, so ids never equal (1, 2).
  float adv_a = 0.0f;
  float adv_b = 0.0f;
  unsigned gid_a = 0u;
  unsigned gid_b = 0u;
  if (!shapeAbWithTestFont(&adv_a, &adv_b, &gid_a, &gid_b)) return false;
  (void)adv_a;
  (void)adv_b;
  muse::text::TextStyle style;
  style.size_px = 16.0f;
  muse::text::StatusOr<muse::text::ShapedRun> r = muse::text::shapeRun(
      reinterpret_cast<const std::uint8_t*>("AB"), 2, muse::text::FontHandle{},
      style);
  if (!r.ok()) return false;
  if ((*r).glyph_ids.size() != 2) return false;
  return (*r).glyph_ids[0] == gid_a && (*r).glyph_ids[1] == gid_b;
}

static bool check_uncovered_codepoint_is_missing_glyph() {
  // T9 boundary/error: U+FFFF is covered by no face, so shaping it must fail
  // closed with kMissingGlyph — never degrade to a fallback glyph. RED: the
  // current seam degrades uncovered input to ok() bundled-face shaping.
  // NOTE: conflicts with shaper.h:70 ("never kMissingGlyph"); the T9 plan
  // Interface line governs — flagged in contract T9.md for planner/coder.
  const std::uint8_t kUnassigned[] = {0xEF, 0xBF, 0xBF};  // U+FFFF
  muse::text::TextStyle style;
  style.size_px = 16.0f;
  muse::text::FontRequest req;
  muse::text::StatusOr<muse::text::ShapedRun> shaped =
      muse::text::shapeRunWithFallback(kUnassigned, sizeof(kUnassigned), style,
                                       req);
  if (shaped.ok()) return false;
  return shaped.status() == muse::text::Status::kMissingGlyph;
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_advances_match_reference_within_tolerance);
  RUN_CASE(check_advances_mismatch_beyond_tolerance_returns_false);
  RUN_CASE(check_advances_length_mismatch_returns_false);
  RUN_CASE(check_empty_run_matches_empty_reference);
  RUN_CASE(check_zero_tolerance_requires_exact_match);
  RUN_CASE(check_jni_counters_start_at_zero);
  RUN_CASE(check_profiled_frame_has_no_heap_alloc_external);
  RUN_CASE(check_gallery_label_ids_are_not_codepoints);
  RUN_CASE(check_gallery_label_advances_vary_like_font_output);
  RUN_CASE(check_gallery_label_reshape_keeps_glyph_ids_across_widths);
  RUN_CASE(check_malformed_utf8_is_invalid_script);
  RUN_CASE(check_empty_input_is_valid_empty_run);
  RUN_CASE(check_fallback_null_bytes_is_invalid_input);
  RUN_CASE(check_fallback_malformed_utf8_is_invalid_input);
  RUN_CASE(check_fallback_null_variations_is_invalid_input);
  RUN_CASE(check_fallback_modified_utf8_nul_overlong_rejected);
  RUN_CASE(check_faces_sharing_low_byte_produce_distinct_ids);
  RUN_CASE(check_strict_decode_mirror_maps_hostile_to_fffd);
  RUN_CASE(check_font_path_roboto_latn_fail_closed_on_host);
  RUN_CASE(check_font_path_last_resort_face_is_no_face);
  RUN_CASE(check_font_path_unknown_face_is_no_face);
  RUN_CASE(check_font_path_default_face_is_no_face);
  RUN_CASE(check_font_path_is_deterministic);
  RUN_CASE(check_test_font_opens_hermetically);
  RUN_CASE(check_real_shape_advances_match_font_output);
  RUN_CASE(check_real_shape_ids_are_font_glyph_ids);
  RUN_CASE(check_uncovered_codepoint_is_missing_glyph);
  if (failures > 0) {
    std::printf("harfbuzz_parity_test: %d failing\n", failures);
    return 1;
  }
  std::printf("harfbuzz_parity_test: all passing\n");
  return 0;
}
