// TC-TEXT-003 — AC-TEXT-003: color emoji (COLRv1 + CBDT/CBLC) with ZWJ
// sequences, skin-tone modifiers, regional-indicator flags, and keycap
// sequences on all API levels.
// Hermetic harness: plain C++17, no test framework, no device, no font
// files, no golden images, no downloads. All sequences are INLINE byte
// strings. Metrics presence is proven by driving the COMMITTED T1 shaper
// (shapeRun) over the same bytes. Pixel-stability across API levels is
// EXTERNAL (device-lab, TC-TEXT-024, follow-up FR-TEXT-010). Shape mirrors
// engine/text/tests/jni_free_path_test.cpp: static bool check_*() cases +
// main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "muse/text/emoji.h"
#include "muse/text/shaper.h"

// ASSUMPTION (structural, pending emoji.h): EmojiRun exposes
//   `std::vector<EmojiCluster> clusters` (one entry per grapheme cluster)
//   and `Status status`; EmojiCluster exposes `std::vector<EmojiLayer>
//   layers` (at least one layer per cluster: COLRv1 layers or the
//   CBDT/CBLC bitmap layer) and byte-range `byte_start`/`byte_end`.
//   Malformed input yields `status == Status::kUnsupportedSequence`.
//   isSingleCluster returns true iff the whole input is exactly one
//   grapheme cluster. All entry points never throw.

// U+1F468 U+200D U+1F469 U+200D U+1F467 U+200D U+1F466 (family).
static const std::string kZwjFamily =
    "👨\u200d👩\u200d👧\u200d👦";
// U+1F44D U+1F3FD (thumbs up + medium skin tone).
static const std::string kSkinTone = "👍🏽";
// U+1F1FA U+1F1F8 (regional-indicator pair: US flag).
static const std::string kFlag = "🇺🇸";
// U+0023 U+FE0F U+20E3 (keycap number sign).
static const std::string kKeycap = "#️⃣";

static bool metrics_present_via_shaper(const std::string& text) {
  try {
    muse::text::FontHandle font{};
    muse::text::TextStyle style{};
    muse::text::StatusOr<muse::text::ShapedRun> result = muse::text::shapeRun(
        reinterpret_cast<const uint8_t*>(text.data()), text.size(), font,
        style);
    if (!result.ok()) return false;
    const muse::text::ShapedRun& run = *result;
    return !run.glyph_ids.empty() &&
           run.advances.size() == run.glyph_ids.size();
  } catch (...) {
    return false;
  }
}

static bool check_zwj_family_single_cluster() {
  try {
    muse::text::FontHandle face{};
    muse::text::EmojiRun run = muse::text::shapeEmoji(
        reinterpret_cast<const uint8_t*>(kZwjFamily.data()),
        kZwjFamily.size(), face);
    if (run.clusters.size() != 1) return false;
    if (run.clusters[0].layers.empty()) return false;
    if (!muse::text::isSingleCluster(
            reinterpret_cast<const uint8_t*>(kZwjFamily.data()),
            kZwjFamily.size()))
      return false;
    return metrics_present_via_shaper(kZwjFamily);
  } catch (...) {
    return false;
  }
}

static bool check_skin_tone_single_cluster() {
  try {
    muse::text::FontHandle face{};
    muse::text::EmojiRun run = muse::text::shapeEmoji(
        reinterpret_cast<const uint8_t*>(kSkinTone.data()), kSkinTone.size(),
        face);
    if (run.clusters.size() != 1) return false;
    if (run.clusters[0].layers.empty()) return false;
    if (!muse::text::isSingleCluster(
            reinterpret_cast<const uint8_t*>(kSkinTone.data()),
            kSkinTone.size()))
      return false;
    return metrics_present_via_shaper(kSkinTone);
  } catch (...) {
    return false;
  }
}

static bool check_flag_single_cluster() {
  try {
    muse::text::FontHandle face{};
    muse::text::EmojiRun run = muse::text::shapeEmoji(
        reinterpret_cast<const uint8_t*>(kFlag.data()), kFlag.size(), face);
    if (run.clusters.size() != 1) return false;
    if (run.clusters[0].layers.empty()) return false;
    if (!muse::text::isSingleCluster(
            reinterpret_cast<const uint8_t*>(kFlag.data()), kFlag.size()))
      return false;
    return metrics_present_via_shaper(kFlag);
  } catch (...) {
    return false;
  }
}

static bool check_keycap_single_cluster() {
  try {
    muse::text::FontHandle face{};
    muse::text::EmojiRun run = muse::text::shapeEmoji(
        reinterpret_cast<const uint8_t*>(kKeycap.data()), kKeycap.size(),
        face);
    if (run.clusters.size() != 1) return false;
    if (run.clusters[0].layers.empty()) return false;
    if (!muse::text::isSingleCluster(
            reinterpret_cast<const uint8_t*>(kKeycap.data()),
            kKeycap.size()))
      return false;
    return metrics_present_via_shaper(kKeycap);
  } catch (...) {
    return false;
  }
}

static bool check_malformed_sequence_unsupported() {
  // Bare ZWJ (U+200D) with no base: malformed emoji input, must never
  // split into a valid cluster.
  const uint8_t kBad[] = {0xE2, 0x80, 0x8D};
  try {
    muse::text::FontHandle face{};
    muse::text::EmojiRun run =
        muse::text::shapeEmoji(kBad, sizeof(kBad), face);
    if (run.status != muse::text::Status::kUnsupportedSequence) return false;
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
    muse::text::EmojiRun run =
        muse::text::shapeEmoji(kBad, sizeof(kBad), face);
    if (run.status != muse::text::Status::kUnsupportedSequence) return false;
    if (muse::text::isSingleCluster(kBad, sizeof(kBad))) return false;
    return true;
  } catch (...) {
    return false;
  }
}

static bool check_mixed_text_emoji_spans_isolated() {
  // "Hi " + wave 👋 (U+1F44B) + "!": exactly one emoji cluster covering
  // only the wave bytes; surrounding text is not a single cluster.
  const std::string kMixed = "Hi 👋!";
  const std::string kWave = "👋";
  const size_t kWaveStart = 3;
  try {
    muse::text::FontHandle face{};
    muse::text::EmojiRun run = muse::text::shapeEmoji(
        reinterpret_cast<const uint8_t*>(kMixed.data()), kMixed.size(),
        face);
    if (run.clusters.size() != 1) return false;
    if (run.clusters[0].byte_start != kWaveStart) return false;
    if (run.clusters[0].byte_end != kWaveStart + kWave.size()) return false;
    if (muse::text::isSingleCluster(
            reinterpret_cast<const uint8_t*>(kMixed.data()), kMixed.size()))
      return false;
    if (!muse::text::isSingleCluster(
            reinterpret_cast<const uint8_t*>(kWave.data()), kWave.size()))
      return false;
    return true;
  } catch (...) {
    return false;
  }
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
  RUN_CASE(check_zwj_family_single_cluster);
  RUN_CASE(check_skin_tone_single_cluster);
  RUN_CASE(check_flag_single_cluster);
  RUN_CASE(check_keycap_single_cluster);
  RUN_CASE(check_malformed_sequence_unsupported);
  RUN_CASE(check_truncated_emoji_unsupported);
  RUN_CASE(check_mixed_text_emoji_spans_isolated);
  if (failures > 0) {
    std::printf("emoji_test: %d failing\n", failures);
    return 1;
  }
  std::printf("emoji_test: all passing\n");
  return 0;
}
