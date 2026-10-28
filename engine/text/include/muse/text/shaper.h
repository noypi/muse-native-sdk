#pragma once

// FR-TEXT-007 / AC-TEXT-010: Zero-JNI C++ shaping entry points.
// The per-frame path takes plain buffers and value structs only — no
// JNIEnv*, JavaVM, or any other JVM/JNI type appears in these signatures.
// Real HarfBuzz/ICU backends plug in behind shapeRun/measureLine in a
// follow-up; this core owns the pipeline structure (UTF-8 decode ->
// cluster map -> advances -> measure) with a deterministic stub backend.

#include <cstddef>
#include <cstdint>

#include "muse/text/text_types.h"

namespace muse::text {

// FR-TEXT-011 / AC-TEXT-025: per-run variable-font axis values. axis_tag is
// the OpenType axis tag ('wght', 'wdth', 'opsz', 'slnt', ...) with the value
// for this run; feature axes ('liga', 'kern', 'ss01', 'tnum', ...) ride the
// same vector.
struct FontVariation {
  std::uint32_t axis_tag;
  float value;
};

// Shapes a UTF-8 run under the given variation vector. Identical
// bytes+style with different axis vectors produce distinct cache entries and
// distinct advances; identical vectors hit the cache without re-shape.
// Empty input (len == 0) is a valid empty run. Malformed or truncated UTF-8,
// null bytes with len > 0, or a null vector with var_count > 0 yields
// Status::kInvalidInput; never throws. Pure C++: no JNI types.
StatusOr<ShapedRun> shapeRunWithVariations(
    const std::uint8_t* utf8, std::size_t len, FontHandle font,
    const TextStyle& style, const FontVariation* vars,
    std::size_t var_count) noexcept;

// Shapes a UTF-8 run into glyph ids, advances, offsets, and a per-byte
// cluster map. Empty input (len == 0) is a valid empty run. Malformed or
// truncated UTF-8 yields Status::kInvalidScript; never throws.
StatusOr<ShapedRun> shapeRun(const std::uint8_t* utf8, std::size_t len,
                             FontHandle font, const TextStyle& style);

// FR-TEXT-011 / AC-TEXT-028: end-to-end pipeline request. Carries the
// optional variation vector (variations + variation_count, null/0 means
// "no variations") plus the nullable downloadable-font handle
// (has_downloadable_font + downloadable_font; false means "no download
// attempted"). download_failed is the synchronous test-double outcome
// (true simulates a failed fetch); script_tag/bcp47_locale/generic_family
// select the T1/T3 fallback routing hermetically. Pure value struct.
struct FontRequest {
  const FontVariation* variations = nullptr;
  std::size_t variation_count = 0;
  bool has_downloadable_font = false;
  FontHandle downloadable_font{};
  bool download_failed = false;
  std::uint32_t script_tag = 0;
  const char* bcp47_locale = nullptr;
  const char* generic_family = nullptr;
};

// FR-TEXT-011 / AC-TEXT-028: pipeline entry point over the T1/T3/T6 seams.
// Resolves the fallback chain, applies the variation vector, and shapes in
// a single synchronous layout pass: a failed download degrades to the
// bundled-face shaping in the same call (no second-frame retry on the
// measurement path). Only malformed UTF-8 is an error
// (Status::kInvalidInput); missing fonts and failed downloads degrade to
// ok() bundled-face shaping, never kMissingGlyph. Empty input (len == 0)
// is a valid empty run. Never throws. Pure C++: no JNI types.
StatusOr<ShapedRun> shapeRunWithFallback(const std::uint8_t* bytes,
                                         std::size_t len,
                                         const TextStyle& style,
                                         const FontRequest& req) noexcept;

// Measures a shaped run in C++. The returned width never exceeds
// maxWidthPx (negative budgets clamp to 0).
MeasuredLine measureLine(ShapedRunView run, float maxWidthPx);

// AC-TEXT-013: profiling hook counting JNI calls on the text path. The C++
// shaping/measuring path never touches JNI, so text_calls stays 0; the
// counter exists so a profiled frame can assert it.
struct JniCallCounter {
  std::uint64_t text_calls = 0;
};

// Resets the text-path JNI counters and returns the previous snapshot.
JniCallCounter resetTextJniCounters();

// Returns true iff the shaped run holds exactly n advances and every
// advance is within tolPx of the HarfBuzz reference table. Length mismatch
// (including a null view vs non-empty reference) returns false; a null
// reference table with n == 0 matches only an empty run. Never throws.
bool advancesMatchReference(ShapedRunView a, const float* refAdvances,
                            std::size_t n, float tolPx);

}  // namespace muse::text
