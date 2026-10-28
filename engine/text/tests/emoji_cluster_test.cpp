// T4 — AC-TEXT-021 / FR-TEXT-010: emoji sequences render as single
// grapheme clusters with correct metrics (COLRv1 vs CBDT/CBLC layer
// selection asserted via EmojiCluster.layers non-empty).
// Hermetic harness: plain C++17, no test framework, no device, no font
// files, no downloads. All sequences are INLINE byte strings. Metrics
// presence is proven by driving the COMMITTED shaper (shapeRun) over the
// same bytes for non-zero advances, mirroring engine/text/tests/emoji_test.cpp.
// Shape mirrors the Batch 1 convention: static bool check_*() cases + main,
// return 0 all-green / non-zero + failing name.
// NOTE: the T4 production symbol (shapeEmojiRun returning
// StatusOr<EmojiRun>) does not exist yet, so this file does not compile —
// that compile-missing-production state is the expected RED for T4, not a
// malformed test. The sibling isSingleCluster entry point already exists
// (emoji.h) and is driven as specified.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "muse/text/emoji.h"
#include "muse/text/shaper.h"

// ASSUMPTION (structural, pending emoji.h/emoji.cpp): the emoji-run entry
// point is declared exactly as the plan Interface line states:
//   muse::text::StatusOr<muse::text::EmojiRun>
//   muse::text::shapeEmojiRun(const uint8_t* bytes, size_t len,
//                             muse::text::FontHandle font) noexcept
// and isSingleCluster keeps its committed signature
//   bool muse::text::isSingleCluster(const uint8_t* bytes, size_t len)
// (noexcept added by the coder is compatible; never throws either way).
// ASSUMPTION: EmojiRun keeps `std::vector<EmojiCluster> clusters` (one
// entry per grapheme cluster) and EmojiCluster keeps
// `std::vector<EmojiLayer> layers` (non-empty per cluster: COLRv1 layers
// or the CBDT/CBLC bitmap layer). Malformed input yields StatusOr status
// `Status::kUnsupportedSequence`.
// ASSUMPTION: "per-cluster advances non-zero on success" is observed
// through the committed shapeRun over the same bytes (advances present
// and every advance > 0), since the plan Interface line does not name an
// EmojiCluster advance field; the coder's per-cluster advance storage must
// satisfy this same observable.
// ASSUMPTION: empty input is malformed for shapeEmojiRun
// (kUnsupportedSequence) and is never a single cluster.
// ASSUMPTION: zero-JNI holds structurally (headers pull no JNI/NDK types);
// verified by compilation, not by a runtime case.

// U+1F468 U+200D U+1F469 U+200D U+1F467 U+200D U+1F466 (family).
static const std::string kZwjFamily = "👨\u200d👩\u200d👧\u200d👦";
// U+1F44D U+1F3FD (thumbs up + medium skin tone).
static const std::string kSkinTone = "👍🏽";
// U+1F1FA U+1F1F8 (regional-indicator pair: US flag).
static const std::string kFlag = "🇺🇸";
// U+0023 U+FE0F U+20E3 (keycap number sign).
static const std::string kKeycap = "#️⃣";
// U+2708 U+FE0F (airplane + variation selector-16).
static const std::string kVariationSelector = "✈️";
// Two waving hands: two clusters, not one.
static const std::string kTwoWaves = "👋👋";

namespace {

const uint8_t* U(const std::string& s) {
  return reinterpret_cast<const uint8_t*>(s.data());
}

// Advances for the same bytes via the committed shaper are present and
// every advance is non-zero (the observable half of "per-cluster advances
// non-zero on success").
bool advances_nonzero_via_shaper(const std::string& text) {
  try {
    muse::text::FontHandle font{};
    muse::text::TextStyle style{};
    muse::text::StatusOr<muse::text::ShapedRun> result = muse::text::shapeRun(
        U(text), text.size(), font, style);
    if (!result.ok()) return false;
    const muse::text::ShapedRun& run = *result;
    if (run.glyph_ids.empty()) return false;
    if (run.advances.size() != run.glyph_ids.size()) return false;
    for (float advance : run.advances) {
      if (!(advance > 0.0f)) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool single_cluster_run_ok(const std::string& text) {
  try {
    muse::text::FontHandle face{};
    muse::text::StatusOr<muse::text::EmojiRun> result =
        muse::text::shapeEmojiRun(U(text), text.size(), face);
    if (!result.ok()) return false;
    if (result.status() != muse::text::Status::kOk) return false;
    const muse::text::EmojiRun& run = *result;
    if (run.clusters.size() != 1) return false;
    if (run.clusters[0].layers.empty()) return false;
    if (!muse::text::isSingleCluster(U(text), text.size())) return false;
    return advances_nonzero_via_shaper(text);
  } catch (...) {
    return false;
  }
}

}  // namespace

static bool check_zwj_family_single_cluster() {
  return single_cluster_run_ok(kZwjFamily);
}

static bool check_skin_tone_single_cluster() {
  return single_cluster_run_ok(kSkinTone);
}

static bool check_flag_single_cluster() {
  return single_cluster_run_ok(kFlag);
}

static bool check_keycap_single_cluster() {
  return single_cluster_run_ok(kKeycap);
}

static bool check_variation_selector_single_cluster() {
  return single_cluster_run_ok(kVariationSelector);
}

static bool check_colrv1_layers_present() {
  // Default face id selects the COLRv1 paint datum (format 0); layers must
  // be non-empty so graphics can pick COLRv1 vs CBDT/CBLC.
  try {
    muse::text::FontHandle face{};
    muse::text::StatusOr<muse::text::EmojiRun> result =
        muse::text::shapeEmojiRun(U(kZwjFamily), kZwjFamily.size(), face);
    if (!result.ok()) return false;
    const muse::text::EmojiRun& run = *result;
    if (run.clusters.size() != 1) return false;
    if (run.clusters[0].layers.empty()) return false;
    return run.clusters[0].layers[0].format == 0;
  } catch (...) {
    return false;
  }
}

static bool check_bitmap_face_layers_present() {
  // Faces carrying the high id bit select the CBDT/CBLC bitmap datum
  // (format 1); layers must still be non-empty.
  try {
    muse::text::FontHandle face{0x8000000000000000ULL};
    muse::text::StatusOr<muse::text::EmojiRun> result =
        muse::text::shapeEmojiRun(U(kFlag), kFlag.size(), face);
    if (!result.ok()) return false;
    const muse::text::EmojiRun& run = *result;
    if (run.clusters.size() != 1) return false;
    if (run.clusters[0].layers.empty()) return false;
    return run.clusters[0].layers[0].format == 1;
  } catch (...) {
    return false;
  }
}

static bool check_two_emoji_not_single_cluster() {
  try {
    muse::text::FontHandle face{};
    if (muse::text::isSingleCluster(U(kTwoWaves), kTwoWaves.size()))
      return false;
    muse::text::StatusOr<muse::text::EmojiRun> result =
        muse::text::shapeEmojiRun(U(kTwoWaves), kTwoWaves.size(), face);
    if (!result.ok()) return false;
    return (*result).clusters.size() == 2;
  } catch (...) {
    return false;
  }
}

static bool check_bare_zwj_unsupported() {
  // Bare ZWJ (U+200D) with no base: malformed emoji input.
  const uint8_t kBad[] = {0xE2, 0x80, 0x8D};
  try {
    muse::text::FontHandle face{};
    muse::text::StatusOr<muse::text::EmojiRun> result =
        muse::text::shapeEmojiRun(kBad, sizeof(kBad), face);
    if (result.ok()) return false;
    if (result.status() != muse::text::Status::kUnsupportedSequence)
      return false;
    if (muse::text::isSingleCluster(kBad, sizeof(kBad))) return false;
    return true;
  } catch (...) {
    return false;
  }
}

static bool check_truncated_emoji_unsupported() {
  // Truncated 4-byte emoji lead: malformed, never a valid cluster.
  const uint8_t kBad[] = {0xF0, 0x9F, 0x91};
  try {
    muse::text::FontHandle face{};
    muse::text::StatusOr<muse::text::EmojiRun> result =
        muse::text::shapeEmojiRun(kBad, sizeof(kBad), face);
    if (result.ok()) return false;
    if (result.status() != muse::text::Status::kUnsupportedSequence)
      return false;
    if (muse::text::isSingleCluster(kBad, sizeof(kBad))) return false;
    return true;
  } catch (...) {
    return false;
  }
}

static bool check_dangling_modifier_unsupported() {
  // Lone skin-tone modifier (U+1F3FD) with no base: dangling modifier.
  const uint8_t kBad[] = {0xF0, 0x9F, 0x8F, 0xBD};
  try {
    muse::text::FontHandle face{};
    muse::text::StatusOr<muse::text::EmojiRun> result =
        muse::text::shapeEmojiRun(kBad, sizeof(kBad), face);
    if (result.ok()) return false;
    if (result.status() != muse::text::Status::kUnsupportedSequence)
      return false;
    if (muse::text::isSingleCluster(kBad, sizeof(kBad))) return false;
    return true;
  } catch (...) {
    return false;
  }
}

static bool check_empty_input_unsupported() {
  try {
    muse::text::FontHandle face{};
    // Null with length is malformed; empty (len 0) is the boundary case.
    muse::text::StatusOr<muse::text::EmojiRun> null_result =
        muse::text::shapeEmojiRun(nullptr, 4, face);
    if (null_result.ok()) return false;
    if (null_result.status() != muse::text::Status::kUnsupportedSequence)
      return false;
    muse::text::StatusOr<muse::text::EmojiRun> empty_result =
        muse::text::shapeEmojiRun(nullptr, 0, face);
    if (empty_result.ok()) return false;
    if (empty_result.status() != muse::text::Status::kUnsupportedSequence)
      return false;
    if (muse::text::isSingleCluster(nullptr, 0)) return false;
    return true;
  } catch (...) {
    return false;
  }
}

static bool check_mixed_text_not_single_cluster() {
  // "Hi " + wave + "!": surrounding text means the whole input is not one
  // cluster, while the wave alone is.
  const std::string kMixed = "Hi 👋!";
  const std::string kWave = "👋";
  try {
    if (muse::text::isSingleCluster(U(kMixed), kMixed.size())) return false;
    if (!muse::text::isSingleCluster(U(kWave), kWave.size())) return false;
    return true;
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
  RUN_CASE(check_zwj_family_single_cluster);
  RUN_CASE(check_skin_tone_single_cluster);
  RUN_CASE(check_flag_single_cluster);
  RUN_CASE(check_keycap_single_cluster);
  RUN_CASE(check_variation_selector_single_cluster);
  RUN_CASE(check_colrv1_layers_present);
  RUN_CASE(check_bitmap_face_layers_present);
  RUN_CASE(check_two_emoji_not_single_cluster);
  RUN_CASE(check_bare_zwj_unsupported);
  RUN_CASE(check_truncated_emoji_unsupported);
  RUN_CASE(check_dangling_modifier_unsupported);
  RUN_CASE(check_empty_input_unsupported);
  RUN_CASE(check_mixed_text_not_single_cluster);
  if (failures > 0) {
    std::printf("emoji_cluster_test: %d failing\n", failures);
    return 1;
  }
  std::printf("emoji_cluster_test: all passing\n");
  return 0;
}
