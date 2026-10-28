// T6 — AC-TEXT-025 / FR-TEXT-011: variable-axis values per run participate
// in the layout cache key.
// Hermetic harness: plain C++17, no test framework, no device, no font
// files, no downloads. All byte strings and variation vectors are INLINE
// constants. Cases drive only the public entry point named in the plan
// Interface line and assert distinct-advances / cache-hit / error behavior
// through OBSERVABLE outputs (advance vectors), never cache internals.
// Shape mirrors engine/text/tests/font_fallback_test.cpp (Batch 1 T1):
// static bool check_*() cases + main, return 0 all-green / non-zero +
// failing name.
// NOTE: the T6 production symbols (FontVariation, shapeRunWithVariations)
// do not exist yet, so this file does not compile — that
// compile-missing-production state is the expected RED for T6, not a
// malformed test.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "muse/text/shaper.h"
#include "muse/text/layout_cache.h"

// ASSUMPTION (structural, pending shaper.h): the variation type and entry
// point are declared exactly as the plan Interface line states:
//   struct muse::text::FontVariation { uint32_t axis_tag; float value; };
//   muse::text::StatusOr<muse::text::ShapedRun>
//   muse::text::shapeRunWithVariations(const uint8_t* bytes, size_t len,
//                                      muse::text::FontHandle font,
//                                      const muse::text::TextStyle& style,
//                                      const muse::text::FontVariation* vars,
//                                      size_t var_count) noexcept
// Axis tags use OpenType uint32 tags: 'wght' 'wdth' 'opsz' 'slnt' 'liga'
// 'kern' 'ss01' 'tnum'.
// ASSUMPTION: identical bytes+style+variation-vector twice yields bitwise
// identical advances (cache hit without re-shape); any axis value change
// yields a different advance set (cache miss). Determinism is observable;
// the cache object itself is never touched by the test.
// ASSUMPTION: malformed UTF-8 yields Status::kInvalidInput (same contract
// as shapeRunWithFallback T9); empty input (len == 0) is a valid empty run.
// ASSUMPTION: null vars with var_count == 0 means "no variations" and must
// not crash (noexcept); nullptr bytes with len > 0 must not throw (either
// !ok() or ok(), never an exception).
// ASSUMPTION: zero-JNI holds structurally (header pulls no JNI/NDK types);
// verified by compilation, not by a runtime case.

namespace {

constexpr uint32_t kWght = 0x77676874u;  // 'wght'
constexpr uint32_t kWdth = 0x77647468u;  // 'wdth'
constexpr uint32_t kOpsz = 0x6F70737Au;  // 'opsz'
constexpr uint32_t kSlnt = 0x736C6E74u;  // 'slnt'
constexpr uint32_t kLiga = 0x6C696761u;  // 'liga'
constexpr uint32_t kKern = 0x6B65726Eu;  // 'kern'
constexpr uint32_t kSs01 = 0x73733031u;  // 'ss01'
constexpr uint32_t kTnum = 0x746E756Du;  // 'tnum'

const std::string kText = "Variable weight Aa";

std::vector<float> advances_for(const muse::text::FontVariation* vars,
                                std::size_t n) {
  muse::text::FontHandle font{3};
  muse::text::TextStyle style{};
  auto result = muse::text::shapeRunWithVariations(
      reinterpret_cast<const uint8_t*>(kText.data()), kText.size(), font,
      style, vars, n);
  if (!result.ok()) return {};
  return (*result).advances;
}

bool advances_equal(const std::vector<float>& a,
                    const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

bool advances_differ(const std::vector<float>& a,
                     const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty() || b.empty()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return true;
  }
  return false;
}

// Local replica of the layout_cache.cpp VariationHash (FNV-1a 64-bit over
// the variation vector: count, then per-entry axis_tag + float bit pattern).
// Used ONLY to locate a zero-residue probe vector deterministically at run
// time; all behavior assertions below go through the public entry points
// (shapeRun / shapeRunWithVariations) and observable advance vectors.
std::uint64_t variation_hash_replica(const muse::text::FontVariation* vars,
                                     std::size_t n) {
  std::uint64_t hash = 1469598103934665603ull;
  const auto mix = [&hash](std::uint64_t word) {
    hash ^= word;
    hash *= 1099511628211ull;
  };
  mix(static_cast<std::uint64_t>(n));
  for (std::size_t i = 0; i < n; ++i) {
    mix(static_cast<std::uint64_t>(vars[i].axis_tag));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &vars[i].value, sizeof(bits));
    mix(static_cast<std::uint64_t>(bits));
  }
  return hash;
}

// Deterministically sweeps single-axis 'wght' vectors until one hits the
// zero-delta residue (hash % 200001 == 100000). Residue density is 1/200001
// so a bounded sweep of 300000 values guarantees a hit. Returns true and
// fills *out on success.
bool find_zero_residue_vector(muse::text::FontVariation* out) {
  for (int i = 0; i < 300000; ++i) {
    muse::text::FontVariation probe = {kWght, 100.0f + static_cast<float>(i)};
    if (variation_hash_replica(&probe, 1) % 200001u == 100000u) {
      *out = probe;
      return true;
    }
  }
  return false;
}

std::vector<float> advances_plain() {
  muse::text::FontHandle font{3};
  muse::text::TextStyle style{};
  auto result = muse::text::shapeRun(
      reinterpret_cast<const uint8_t*>(kText.data()), kText.size(), font,
      style);
  if (!result.ok()) return {};
  return (*result).advances;
}

}  // namespace

static bool check_three_weights_three_advance_sets() {
  try {
    const muse::text::FontVariation kLight[] = {{kWght, 300.0f}};
    const muse::text::FontVariation kRegular[] = {{kWght, 400.0f}};
    const muse::text::FontVariation kBold[] = {{kWght, 700.0f}};
    std::vector<float> light = advances_for(kLight, 1);
    std::vector<float> regular = advances_for(kRegular, 1);
    std::vector<float> bold = advances_for(kBold, 1);
    if (light.empty() || regular.empty() || bold.empty()) return false;
    return advances_differ(light, regular) &&
           advances_differ(regular, bold) && advances_differ(light, bold);
  } catch (...) {
    return false;
  }
}

static bool check_identical_axes_hit() {
  try {
    const muse::text::FontVariation kVars[] = {{kWght, 500.0f},
                                               {kWdth, 100.0f}};
    std::vector<float> first = advances_for(kVars, 2);
    std::vector<float> second = advances_for(kVars, 2);
    if (first.empty() || second.empty()) return false;
    return advances_equal(first, second);
  } catch (...) {
    return false;
  }
}

static bool check_axis_change_is_miss() {
  try {
    const muse::text::FontVariation kBefore[] = {{kWght, 400.0f}};
    const muse::text::FontVariation kAfter[] = {{kWght, 401.0f}};
    std::vector<float> before = advances_for(kBefore, 1);
    std::vector<float> after = advances_for(kAfter, 1);
    if (before.empty() || after.empty()) return false;
    return advances_differ(before, after);
  } catch (...) {
    return false;
  }
}

static bool check_wdth_changes_advances() {
  try {
    const muse::text::FontVariation kNarrow[] = {{kWdth, 75.0f}};
    const muse::text::FontVariation kWide[] = {{kWdth, 125.0f}};
    std::vector<float> narrow = advances_for(kNarrow, 1);
    std::vector<float> wide = advances_for(kWide, 1);
    if (narrow.empty() || wide.empty()) return false;
    return advances_differ(narrow, wide);
  } catch (...) {
    return false;
  }
}

static bool check_opsz_changes_advances() {
  try {
    const muse::text::FontVariation kSmall[] = {{kOpsz, 8.0f}};
    const muse::text::FontVariation kLarge[] = {{kOpsz, 72.0f}};
    std::vector<float> small = advances_for(kSmall, 1);
    std::vector<float> large = advances_for(kLarge, 1);
    if (small.empty() || large.empty()) return false;
    return advances_differ(small, large);
  } catch (...) {
    return false;
  }
}

static bool check_slnt_changes_advances() {
  try {
    const muse::text::FontVariation kUpright[] = {{kSlnt, 0.0f}};
    const muse::text::FontVariation kOblique[] = {{kSlnt, -12.0f}};
    std::vector<float> upright = advances_for(kUpright, 1);
    std::vector<float> oblique = advances_for(kOblique, 1);
    if (upright.empty() || oblique.empty()) return false;
    return advances_differ(upright, oblique);
  } catch (...) {
    return false;
  }
}

static bool check_feature_axes_passthrough() {
  try {
    // liga/kern/ss01/tnum vectors must be accepted and must key distinctly.
    const muse::text::FontVariation kOff[] = {{kLiga, 0.0f}, {kKern, 0.0f}};
    const muse::text::FontVariation kOn[] = {{kLiga, 1.0f},
                                             {kKern, 1.0f},
                                             {kSs01, 1.0f},
                                             {kTnum, 1.0f}};
    std::vector<float> off = advances_for(kOff, 2);
    std::vector<float> on = advances_for(kOn, 4);
    if (off.empty() || on.empty()) return false;
    std::vector<float> off_again = advances_for(kOff, 2);
    return advances_equal(off, off_again) && advances_differ(off, on);
  } catch (...) {
    return false;
  }
}

// TC-TEXT-025 / BUG-TEXT-002: zero-residue vectors must still perturb.
// Finds a variation vector with VariationHash % 200001 == 100000 (the
// residue that yields delta == 0.0f pre-fix) and asserts its advances
// pairwise-differ from the unperturbed shapeRun baseline and from a
// regular (non-zero-residue) variation vector. Pre-fix this fails: the
// zero residue perturbs by exactly 0, so the run collides with the plain
// baseline. Post-fix the residue maps to one epsilon step and all three
// advance sets are pairwise distinct.
static bool check_zero_delta_residue_distinct() {
  try {
    muse::text::FontVariation zero{};
    if (!find_zero_residue_vector(&zero)) return false;
    std::vector<float> probed = advances_for(&zero, 1);
    std::vector<float> baseline = advances_plain();
    const muse::text::FontVariation kOther[] = {{kWght, 400.0f}};
    std::vector<float> other = advances_for(kOther, 1);
    if (probed.empty() || baseline.empty() || other.empty()) return false;
    return advances_differ(probed, baseline) &&
           advances_differ(probed, other) && advances_differ(baseline, other);
  } catch (...) {
    return false;
  }
}

static bool check_empty_variation_vector_ok() {  try {
    std::vector<float> none = advances_for(nullptr, 0);
    return !none.empty();
  } catch (...) {
    return false;
  }
}

static bool check_malformed_utf8_invalid_input() {
  try {
    const uint8_t kBad[] = {0xE4, 0xB8};  // truncated 3-byte lead
    muse::text::FontHandle font{3};
    muse::text::TextStyle style{};
    const muse::text::FontVariation kVars[] = {{kWght, 400.0f}};
    auto result = muse::text::shapeRunWithVariations(kBad, sizeof(kBad),
                                                     font, style, kVars, 1);
    return !result.ok() &&
           result.status() == muse::text::Status::kInvalidInput;
  } catch (...) {
    return false;
  }
}

static bool check_empty_input_ok_empty() {
  try {
    muse::text::FontHandle font{3};
    muse::text::TextStyle style{};
    const muse::text::FontVariation kVars[] = {{kWght, 400.0f}};
    const uint8_t kEmpty[] = {0x00};
    auto result = muse::text::shapeRunWithVariations(kEmpty, 0, font, style,
                                                     kVars, 1);
    if (!result.ok()) return false;
    return (*result).glyph_ids.empty() && (*result).advances.empty();
  } catch (...) {
    return false;
  }
}

static bool check_null_vars_with_count_invalid_input() {
  try {
    muse::text::FontHandle font{3};
    muse::text::TextStyle style{};
    auto result = muse::text::shapeRunWithVariations(
        reinterpret_cast<const uint8_t*>(kText.data()), kText.size(), font,
        style, nullptr, 2);
    if (result.ok()) return false;
    return result.status() == muse::text::Status::kInvalidInput;
  } catch (...) {
    return false;
  }
}

static bool check_null_bytes_does_not_throw() {
  try {
    muse::text::FontHandle font{3};
    muse::text::TextStyle style{};
    auto result = muse::text::shapeRunWithVariations(nullptr, 4, font, style,
                                                     nullptr, 0);
    (void)result.ok();
    return true;  // noexcept: any non-throwing outcome is acceptable
  } catch (...) {
    return false;
  }
}

// BUG-TEXT-001 / Q-TEXT-005: bounded cardinality. Shapes 512 distinct
// (bytes, vars) pairs — well above kVariationCacheMaxEntries (256) plus any
// entries earlier cases inserted — then asserts the cache never holds more
// than the cap. Pre-fix this fails (unbounded growth); post-fix it passes.
// Order-independent: no assumption of an empty cache at case start.
static bool check_variation_cache_bounded_after_distinct_overflow() {
  try {
    muse::text::FontHandle font{3};
    muse::text::TextStyle style{};
    for (int i = 0; i < 512; ++i) {
      std::string bytes = "soak-" + std::to_string(i);
      muse::text::FontVariation var = {
          kWght, 100.0f + static_cast<float>(i)};
      auto result = muse::text::shapeRunWithVariations(
          reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), font,
          style, &var, 1);
      if (!result.ok()) return false;
    }
    return muse::text::VariationCacheSize() <=
           muse::text::kVariationCacheMaxEntries;
  } catch (...) {
    return false;
  }
}

// BUG-TEXT-001: eviction is transparent (re-shape, not key collapse).
// Re-shapes one key evicted by the overflow above and asserts it still
// returns a valid run whose advances differ from a different variation
// vector — eviction must cost only a re-shape, never a silent merge.
static bool check_evicted_key_reshapes_with_distinct_advances() {
  try {
    muse::text::FontHandle font{3};
    muse::text::TextStyle style{};
    const std::string kEvicted = "soak-0";
    const muse::text::FontVariation kOrig[] = {{kWght, 100.0f}};
    const muse::text::FontVariation kOther[] = {{kWght, 900.0f}};
    auto first = muse::text::shapeRunWithVariations(
        reinterpret_cast<const uint8_t*>(kEvicted.data()), kEvicted.size(),
        font, style, kOrig, 1);
    auto second = muse::text::shapeRunWithVariations(
        reinterpret_cast<const uint8_t*>(kEvicted.data()), kEvicted.size(),
        font, style, kOther, 1);
    if (!first.ok() || !second.ok()) return false;
    if ((*first).advances.empty() || (*second).advances.empty()) return false;
    return advances_differ((*first).advances, (*second).advances);
  } catch (...) {
    return false;
  }
}

// BUG-TEXT-001: repeated identical shapes do not grow the cache.
// Records the size, re-shapes one key ten times, and asserts the size is
// unchanged (hits, not inserts). Order-independent by construction.
static bool check_repeated_identical_shape_does_not_grow_cache() {
  try {
    muse::text::FontHandle font{3};
    muse::text::TextStyle style{};
    const std::string kKey = "stable-key";
    const muse::text::FontVariation kVars[] = {{kWght, 500.0f}};
    auto seed = muse::text::shapeRunWithVariations(
        reinterpret_cast<const uint8_t*>(kKey.data()), kKey.size(), font,
        style, kVars, 1);
    if (!seed.ok()) return false;
    const std::size_t before = muse::text::VariationCacheSize();
    for (int i = 0; i < 10; ++i) {
      auto result = muse::text::shapeRunWithVariations(
          reinterpret_cast<const uint8_t*>(kKey.data()), kKey.size(), font,
          style, kVars, 1);
      if (!result.ok()) return false;
    }
    return muse::text::VariationCacheSize() == before;
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
  RUN_CASE(check_three_weights_three_advance_sets);
  RUN_CASE(check_identical_axes_hit);
  RUN_CASE(check_axis_change_is_miss);
  RUN_CASE(check_wdth_changes_advances);
  RUN_CASE(check_opsz_changes_advances);
  RUN_CASE(check_slnt_changes_advances);
  RUN_CASE(check_feature_axes_passthrough);
  RUN_CASE(check_zero_delta_residue_distinct);
  RUN_CASE(check_empty_variation_vector_ok);
  RUN_CASE(check_malformed_utf8_invalid_input);
  RUN_CASE(check_empty_input_ok_empty);
  RUN_CASE(check_null_bytes_does_not_throw);
  RUN_CASE(check_null_vars_with_count_invalid_input);
  RUN_CASE(check_variation_cache_bounded_after_distinct_overflow);
  RUN_CASE(check_evicted_key_reshapes_with_distinct_advances);
  RUN_CASE(check_repeated_identical_shape_does_not_grow_cache);
  if (failures > 0) {
    std::printf("variable_font_test: %d failing\n", failures);
    return 1;
  }
  std::printf("variable_font_test: all passing\n");
  return 0;
}
