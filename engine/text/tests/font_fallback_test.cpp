// T1 — AC-TEXT-005 / FR-TEXT-003: NDK per-script/per-locale font fallback
// resolver with no-tofu guarantee for shipped locales.
// Hermetic harness: plain C++17, no test framework, no device, no font
// files, no downloads. All script tags and locales are INLINE constants.
// The NDK matcher seam is never touched directly; cases drive only the
// public resolver named in the plan Interface line and assert
// resolution/no-tofu/error behavior.
// Shape mirrors engine/text/tests/emoji_test.cpp: static bool check_*()
// cases + main, return 0 all-green / non-zero + failing name.
// NOTE: the T1 production symbol (resolveFallbackForScript,
// Status::kMissingGlyph) does not exist yet, so this file does not
// compile — that compile-missing-production state is the expected RED
// for T1, not a malformed test.
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "muse/text/font_discovery.h"

// ASSUMPTION (structural, pending font_discovery.h): the resolver is
// declared exactly as the plan Interface line states:
//   muse::text::StatusOr<muse::text::FontHandle>
//   muse::text::resolveFallbackForScript(uint32_t script_tag,
//                                        const char* bcp47_locale) noexcept
// ASSUMPTION: Status gains `kMissingGlyph`, returned iff no
// supported-locale face covers the script; success yields a usable
// (non-zero-id) FontHandle.
// ASSUMPTION: script_tag uses an OpenType-style uint32 tag (e.g. 'Latn').
// ASSUMPTION: a null bcp47_locale is treated as "default locale" and must
// not crash (noexcept); it resolves or yields kMissingGlyph, never throws.
// ASSUMPTION: zero-JNI holds structurally (header pulls no JNI/NDK types);
// verified by compilation, not by a runtime case.

namespace {

constexpr uint32_t kLatn = 0x4C61746Eu;  // 'Latn'
constexpr uint32_t kArab = 0x41726162u;  // 'Arab'
constexpr uint32_t kHebr = 0x48656272u;  // 'Hebr'
constexpr uint32_t kDeva = 0x44657661u;  // 'Deva'
constexpr uint32_t kThai = 0x54686169u;  // 'Thai'
constexpr uint32_t kKhmr = 0x4B686D72u;  // 'Khmr'
constexpr uint32_t kMymr = 0x4D796D72u;  // 'Mymr'
constexpr uint32_t kHani = 0x48616E69u;  // 'Hani'
constexpr uint32_t kZzzz = 0x5A7A7A7Au;  // 'Zzzz' (unknown script)

bool resolves_without_missing_glyph(uint32_t script, const char* locale) {
  try {
    auto result = muse::text::resolveFallbackForScript(script, locale);
    if (!result.ok()) return false;
    if (result.status() != muse::text::Status::kOk) return false;
    return (*result).id != 0;
  } catch (...) {
    return false;
  }
}

}  // namespace

static bool check_latin_en_us_resolves() {
  return resolves_without_missing_glyph(kLatn, "en-US");
}

static bool check_arabic_ar_eg_resolves() {
  return resolves_without_missing_glyph(kArab, "ar-EG");
}

static bool check_hebrew_he_il_resolves() {
  return resolves_without_missing_glyph(kHebr, "he-IL");
}

static bool check_devanagari_hi_in_resolves() {
  return resolves_without_missing_glyph(kDeva, "hi-IN");
}

static bool check_thai_th_th_resolves() {
  return resolves_without_missing_glyph(kThai, "th-TH");
}

static bool check_khmer_km_kh_resolves() {
  return resolves_without_missing_glyph(kKhmr, "km-KH");
}

static bool check_myanmar_my_mm_resolves() {
  return resolves_without_missing_glyph(kMymr, "my-MM");
}

static bool check_hani_zh_hans_resolves() {
  return resolves_without_missing_glyph(kHani, "zh-Hans-CN");
}

static bool check_hani_ja_jp_resolves() {
  return resolves_without_missing_glyph(kHani, "ja-JP");
}

static bool check_unknown_script_yields_missing_glyph() {
  try {
    auto result = muse::text::resolveFallbackForScript(kZzzz, "en-US");
    if (result.ok()) return false;
    return result.status() == muse::text::Status::kMissingGlyph;
  } catch (...) {
    return false;
  }
}

static bool check_resolution_is_deterministic() {
  try {
    auto a = muse::text::resolveFallbackForScript(kArab, "ar-EG");
    auto b = muse::text::resolveFallbackForScript(kArab, "ar-EG");
    if (!a.ok() || !b.ok()) return false;
    return (*a) == (*b);
  } catch (...) {
    return false;
  }
}

static bool check_null_locale_does_not_throw() {
  // Null locale must be noexcept-safe: either resolves or reports
  // kMissingGlyph, but never throws or crashes.
  try {
    auto result = muse::text::resolveFallbackForScript(kLatn, nullptr);
    if (result.ok()) return (*result).id != 0;
    return result.status() == muse::text::Status::kMissingGlyph;
  } catch (...) {
    return false;
  }
}

static bool check_empty_locale_does_not_throw() {
  try {
    auto result = muse::text::resolveFallbackForScript(kLatn, "");
    if (result.ok()) return (*result).id != 0;
    return result.status() == muse::text::Status::kMissingGlyph;
  } catch (...) {
    return false;
  }
}

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_latin_en_us_resolves);
  RUN_CASE(check_arabic_ar_eg_resolves);
  RUN_CASE(check_hebrew_he_il_resolves);
  RUN_CASE(check_devanagari_hi_in_resolves);
  RUN_CASE(check_thai_th_th_resolves);
  RUN_CASE(check_khmer_km_kh_resolves);
  RUN_CASE(check_myanmar_my_mm_resolves);
  RUN_CASE(check_hani_zh_hans_resolves);
  RUN_CASE(check_hani_ja_jp_resolves);
  RUN_CASE(check_unknown_script_yields_missing_glyph);
  RUN_CASE(check_resolution_is_deterministic);
  RUN_CASE(check_null_locale_does_not_throw);
  RUN_CASE(check_empty_locale_does_not_throw);
  if (failures > 0) {
    std::printf("font_fallback_test: %d failing\n", failures);
    return 1;
  }
  std::printf("font_fallback_test: all passing\n");
  return 0;
}
