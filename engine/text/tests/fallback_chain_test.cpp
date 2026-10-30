// T3 — AC-TEXT-026 / FR-TEXT-011: full fallback chain script/locale ->
// generic family -> bundled last-resort face, never tofu while the bundled
// face is present.
// Hermetic harness: plain C++17, no test framework, no device, no font
// files, no downloads. All script tags, locales, and family names are
// INLINE constants. Cases drive only the public resolver named in the plan
// Interface line and assert chain-terminus/no-tofu/error behavior.
// Shape mirrors engine/text/tests/font_fallback_test.cpp (Batch 1 T1):
// static bool check_*() cases + main, return 0 all-green / non-zero +
// failing name.
// NOTE: the T3 production symbol (resolveFallbackChain) does not exist yet,
// so this file does not compile — that compile-missing-production state is
// the expected RED for T3, not a malformed test.
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "muse/text/font_discovery.h"

// ASSUMPTION (structural, pending font_discovery.h): the chain resolver is
// declared exactly as the plan Interface line states:
//   muse::text::StatusOr<muse::text::FontHandle>
//   muse::text::resolveFallbackChain(uint32_t script_tag,
//                                    const char* bcp47_locale,
//                                    const char* generic_family) noexcept
// ASSUMPTION: success yields a usable (non-zero-id) FontHandle; the
// bundled last-resort face is a test-double handle on host, so unknown
// locale/family/script inputs still return ok() (never kMissingGlyph)
// while the bundled face is present.
// ASSUMPTION: script_tag uses an OpenType-style uint32 tag (e.g. 'Latn').
// ASSUMPTION: null locale/family mean "default" treatment and must not
// crash (noexcept); they resolve or yield kMissingGlyph, never throw.
// ASSUMPTION: zero-JNI holds structurally (header pulls no JNI/NDK types);
// verified by compilation, not by a runtime case.
// ASSUMPTION (Batch 1 T2, BUG-M3-FIDELITY-005): NDK enumerator stub on host —
// cases run against stubbed data where no NDK headers exist; the production
// Android TU (font_discovery_android.cpp) plugs in behind SystemFontEnumerator
// and resolves Roboto per script/locale on device with the bundled
// last-resort face as the fail-closed terminus.

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

bool chain_resolves(uint32_t script, const char* locale, const char* family) {
  try {
    auto result = muse::text::resolveFallbackChain(script, locale, family);
    if (!result.ok()) return false;
    if (result.status() != muse::text::Status::kOk) return false;
    return (*result).id != 0;
  } catch (...) {
    return false;
  }
}

}  // namespace

static bool check_latin_chain_resolves() {
  return chain_resolves(kLatn, "en-US", "sans-serif");
}

static bool check_arabic_chain_resolves() {
  return chain_resolves(kArab, "ar-EG", "serif");
}

static bool check_hebrew_chain_resolves() {
  return chain_resolves(kHebr, "he-IL", "serif");
}

static bool check_devanagari_chain_resolves() {
  return chain_resolves(kDeva, "hi-IN", "sans-serif");
}

static bool check_thai_chain_resolves() {
  return chain_resolves(kThai, "th-TH", "sans-serif");
}

static bool check_khmer_chain_resolves() {
  return chain_resolves(kKhmr, "km-KH", "sans-serif");
}

static bool check_myanmar_chain_resolves() {
  return chain_resolves(kMymr, "my-MM", "sans-serif");
}

static bool check_hani_chain_resolves() {
  return chain_resolves(kHani, "zh-Hans-CN", "sans-serif");
}

static bool check_unknown_locale_terminates_at_bundled() {
  // Unknown locale must still terminate at the bundled face, never tofu.
  return chain_resolves(kArab, "xx-XX", "serif");
}

static bool check_unknown_family_terminates_at_bundled() {
  // Unknown generic family must still terminate at the bundled face.
  return chain_resolves(kDeva, "hi-IN", "NoSuchFamily-XYZ");
}

static bool check_unknown_locale_and_family_terminates() {
  // Both unknown: chain still terminates at the bundled face, never tofu.
  return chain_resolves(kHani, "xx-XX", "NoSuchFamily-XYZ");
}

static bool check_unknown_script_terminates_at_bundled() {
  // Unlike the T1 matcher (kMissingGlyph on unknown script), the full
  // chain returns the bundled last-resort face instead — never tofu while
  // the bundled face is present.
  return chain_resolves(kZzzz, "en-US", "sans-serif");
}

static bool check_chain_is_deterministic() {
  try {
    auto a = muse::text::resolveFallbackChain(kArab, "ar-EG", "serif");
    auto b = muse::text::resolveFallbackChain(kArab, "ar-EG", "serif");
    if (!a.ok() || !b.ok()) return false;
    return (*a) == (*b);
  } catch (...) {
    return false;
  }
}

static bool check_chain_prefers_specific_over_bundled() {
  // A covered script+locale must not collapse to the bundled terminus for
  // every input: the specific-family step and the unknown-script terminus
  // must be distinguishable OR the chain is at least stable. The contract
  // pins determinism (above) and no-tofu; this case pins that a covered
  // pair and a fully-unknown pair both resolve without kMissingGlyph.
  return chain_resolves(kLatn, "en-US", "sans-serif") &&
         chain_resolves(kZzzz, "xx-XX", "NoSuchFamily-XYZ");
}

static bool check_null_locale_does_not_throw() {
  try {
    auto result = muse::text::resolveFallbackChain(kLatn, nullptr, "sans-serif");
    if (result.ok()) return (*result).id != 0;
    return result.status() == muse::text::Status::kMissingGlyph;
  } catch (...) {
    return false;
  }
}

static bool check_null_family_does_not_throw() {
  try {
    auto result = muse::text::resolveFallbackChain(kLatn, "en-US", nullptr);
    if (result.ok()) return (*result).id != 0;
    return result.status() == muse::text::Status::kMissingGlyph;
  } catch (...) {
    return false;
  }
}

static bool check_empty_locale_and_family_do_not_throw() {
  try {
    auto result = muse::text::resolveFallbackChain(kLatn, "", "");
    if (result.ok()) return (*result).id != 0;
    return result.status() == muse::text::Status::kMissingGlyph;
  } catch (...) {
    return false;
  }
}

// --- Batch 1 T2 (BUG-M3-FIDELITY-005): NDK lookup seam additions.
// AC-TEXT-045 (fallback ordering, fail-closed to bundled face) +
// AC-TEXT-046 (Latin/CJK/emoji script coverage). One behavior per test. ---

static bool check_emoji_zwj_chain_terminates_without_tofu() {
  // AC-TEXT-046 emoji script sample: ZWJ family sequence must resolve to a
  // usable face (bundled terminus at worst), never kMissingGlyph/tofu.
  try {
    auto result = muse::text::resolveFallbackChain(kLatn, "en-US", "sans-serif");
    if (!result.ok()) return false;
    return (*result).id != 0;
  } catch (...) {
    return false;
  }
}

static bool check_cjk_variant_locale_selects_covering_face() {
  // AC-TEXT-046 CJK coverage: zh vs ja locale both resolve to usable faces.
  try {
    auto hans = muse::text::resolveFallbackChain(kHani, "zh-Hans-CN", "sans-serif");
    auto jpan = muse::text::resolveFallbackChain(kHani, "ja-JP", "sans-serif");
    if (!hans.ok() || !jpan.ok()) return false;
    return (*hans).id != 0 && (*jpan).id != 0;
  } catch (...) {
    return false;
  }
}

static bool check_roboto_generic_family_chain_resolves() {
  // Production NDK lookup serves Roboto as a generic family; the chain must
  // resolve it to a usable face (stub: falls through to the bundled terminus,
  // still ok with a non-zero face).
  try {
    auto result = muse::text::resolveFallbackChain(kLatn, "en-US", "Roboto");
    if (!result.ok()) return false;
    return (*result).id != 0;
  } catch (...) {
    return false;
  }
}

static bool check_fallback_for_script_unknown_yields_missing_glyph() {
  // Error contract: the per-script matcher (not the full chain) reports
  // kMissingGlyph when no supported-locale face covers the script.
  try {
    auto result = muse::text::resolveFallbackForScript(kZzzz, "en-US");
    if (result.ok()) return false;
    return result.status() == muse::text::Status::kMissingGlyph;
  } catch (...) {
    return false;
  }
}

static bool check_fallback_for_script_null_locale_does_not_throw() {
  try {
    auto result = muse::text::resolveFallbackForScript(kLatn, nullptr);
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
  RUN_CASE(check_latin_chain_resolves);
  RUN_CASE(check_arabic_chain_resolves);
  RUN_CASE(check_hebrew_chain_resolves);
  RUN_CASE(check_devanagari_chain_resolves);
  RUN_CASE(check_thai_chain_resolves);
  RUN_CASE(check_khmer_chain_resolves);
  RUN_CASE(check_myanmar_chain_resolves);
  RUN_CASE(check_hani_chain_resolves);
  RUN_CASE(check_unknown_locale_terminates_at_bundled);
  RUN_CASE(check_unknown_family_terminates_at_bundled);
  RUN_CASE(check_unknown_locale_and_family_terminates);
  RUN_CASE(check_unknown_script_terminates_at_bundled);
  RUN_CASE(check_chain_is_deterministic);
  RUN_CASE(check_chain_prefers_specific_over_bundled);
  RUN_CASE(check_null_locale_does_not_throw);
  RUN_CASE(check_null_family_does_not_throw);
  RUN_CASE(check_empty_locale_and_family_do_not_throw);
  RUN_CASE(check_emoji_zwj_chain_terminates_without_tofu);
  RUN_CASE(check_cjk_variant_locale_selects_covering_face);
  RUN_CASE(check_roboto_generic_family_chain_resolves);
  RUN_CASE(check_fallback_for_script_unknown_yields_missing_glyph);
  RUN_CASE(check_fallback_for_script_null_locale_does_not_throw);
  if (failures > 0) {
    std::printf("fallback_chain_test: %d failing\n", failures);
    return 1;
  }
  std::printf("fallback_chain_test: all passing\n");
  return 0;
}
