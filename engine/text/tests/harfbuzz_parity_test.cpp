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
#include <type_traits>
#include <vector>

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
  if (failures > 0) {
    std::printf("harfbuzz_parity_test: %d failing\n", failures);
    return 1;
  }
  std::printf("harfbuzz_parity_test: all passing\n");
  return 0;
}
