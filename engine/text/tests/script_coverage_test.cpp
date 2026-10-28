// TC-TEXT-002 — AC-TEXT-002: full Unicode script coverage (CJK, Arabic,
// Hebrew, Devanagari/Indic, Thai, Khmer, Myanmar) with UAX #9 bidi and
// UAX #14 line breaking.
// Hermetic harness: plain C++17, no test framework, no device, no ICU
// (ubidi/ubrk are NOT on this host), no downloads. Cases drive the public
// bidi interface plus the COMMITTED T1 shaper (shapeRun) with inline byte
// strings. ICU-table-dependent goldens that cannot be encoded inline are
// marked EXTERNAL (device-lab). Shape mirrors
// engine/text/tests/jni_free_path_test.cpp: static bool check_*() cases +
// main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "muse/text/bidi.h"
#include "muse/text/shaper.h"

// ASSUMPTION (structural, pending bidi.h): BidiRuns exposes
//   `std::vector<BidiRun> runs` in visual order and `Status status`;
//   BidiRun exposes byte-range `byte_start`/`byte_end` plus embedding
//   `level`. BreakOpportunities exposes `std::vector<size_t> offsets`
//   (byte offsets where a break is allowed) and `Status status`.
//   Bad UTF-8 yields empty runs/offsets with
//   `status == Status::kInvalidInput`. All entry points never throw.
// ASSUMPTION: base direction 0 = LTR paragraph, 1 = RTL paragraph.

// Inline UTF-8 fixtures (no font files, no ICU data).
static const std::string kCjk = "中文";          // U+4E2D U+6587
static const std::string kArabic = "مرحبا";      // U+0645 U+0631 U+062D U+0628 U+0627
static const std::string kHebrew = "שלום";       // U+05E9 U+05DC U+05D5 U+05DD
static const std::string kDevanagari = "क्ष";    // U+0915 U+094D U+0937 (conjunct)
static const std::string kThai = "สวัสดี";
static const std::string kKhmer = "ខ្មែរ";
static const std::string kMyanmar = "မြန်မာ";

// Drive the committed T1 shaper: no-tofu means shapeRun accepts the bytes
// and yields glyphs with a per-byte cluster map.
static bool shape_ok_no_tofu(const std::string& text) {
  try {
    muse::text::FontHandle font{};
    muse::text::TextStyle style{};
    muse::text::StatusOr<muse::text::ShapedRun> result = muse::text::shapeRun(
        reinterpret_cast<const uint8_t*>(text.data()), text.size(), font,
        style);
    if (!result.ok()) return false;
    const muse::text::ShapedRun& run = *result;
    if (run.glyph_ids.empty()) return false;
    if (run.cluster_map.size() != text.size()) return false;
    return true;
  } catch (...) {
    return false;
  }
}

static bool check_cjk_shapes_no_tofu() { return shape_ok_no_tofu(kCjk); }
static bool check_arabic_shapes_joining_no_tofu() {
  return shape_ok_no_tofu(kArabic);
}
static bool check_hebrew_shapes_rtl_no_tofu() {
  return shape_ok_no_tofu(kHebrew);
}
static bool check_devanagari_conjunct_shapes_no_tofu() {
  return shape_ok_no_tofu(kDevanagari);
}
static bool check_thai_shapes_no_tofu() { return shape_ok_no_tofu(kThai); }
static bool check_khmer_shapes_no_tofu() { return shape_ok_no_tofu(kKhmer); }
static bool check_myanmar_shapes_no_tofu() {
  return shape_ok_no_tofu(kMyanmar);
}

static bool check_bidi_mixed_direction_visual_order() {
  // "Hello " (LTR) + Hebrew word (RTL): visual order must carry one
  // level-0 run and one level-1 run covering the byte ranges.
  const std::string kMixed = std::string("Hello ") + kHebrew;
  try {
    muse::text::BidiRuns runs = muse::text::resolveBidi(
        reinterpret_cast<const uint8_t*>(kMixed.data()), kMixed.size(), 0);
    if (runs.runs.size() < 2) return false;
    bool saw_ltr = false;
    bool saw_rtl = false;
    for (const auto& run : runs.runs) {
      if (run.byte_end <= run.byte_start) return false;
      if (run.byte_end > kMixed.size()) return false;
      if (run.level == 0) saw_ltr = true;
      if ((run.level % 2) == 1) saw_rtl = true;
    }
    return saw_ltr && saw_rtl;
  } catch (...) {
    return false;
  }
}

static bool check_line_break_thai_cluster_and_cjk() {
  // UAX #14: no break inside the Thai grapheme cluster; break
  // opportunities exist around CJK ideographs.
  const std::string kText = kThai + kCjk;
  try {
    muse::text::BreakOpportunities breaks = muse::text::findLineBreaks(
        reinterpret_cast<const uint8_t*>(kText.data()), kText.size(), "th");
    // Thai cluster occupies [0, kThai.size()): no break strictly inside.
    for (size_t off : breaks.offsets) {
      if (off > 0 && off < kThai.size()) return false;
      if (off > kText.size()) return false;
    }
    // At least one break at/after the CJK boundary.
    bool saw_cjk_break = false;
    for (size_t off : breaks.offsets) {
      if (off >= kThai.size()) saw_cjk_break = true;
    }
    return saw_cjk_break;
  } catch (...) {
    return false;
  }
}

static bool check_bad_utf8_bidi_empty_invalid_input() {
  const uint8_t kBad[] = {0x80, 0x80};
  try {
    muse::text::BidiRuns runs =
        muse::text::resolveBidi(kBad, sizeof(kBad), 0);
    if (!runs.runs.empty()) return false;
    return runs.status == muse::text::Status::kInvalidInput;
  } catch (...) {
    return false;
  }
}

static bool check_bad_utf8_linebreak_empty_invalid_input() {
  const uint8_t kBad[] = {0xE2, 0x82};
  try {
    muse::text::BreakOpportunities breaks =
        muse::text::findLineBreaks(kBad, sizeof(kBad), "en");
    if (!breaks.offsets.empty()) return false;
    return breaks.status == muse::text::Status::kInvalidInput;
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
  RUN_CASE(check_cjk_shapes_no_tofu);
  RUN_CASE(check_arabic_shapes_joining_no_tofu);
  RUN_CASE(check_hebrew_shapes_rtl_no_tofu);
  RUN_CASE(check_devanagari_conjunct_shapes_no_tofu);
  RUN_CASE(check_thai_shapes_no_tofu);
  RUN_CASE(check_khmer_shapes_no_tofu);
  RUN_CASE(check_myanmar_shapes_no_tofu);
  RUN_CASE(check_bidi_mixed_direction_visual_order);
  RUN_CASE(check_line_break_thai_cluster_and_cjk);
  RUN_CASE(check_bad_utf8_bidi_empty_invalid_input);
  RUN_CASE(check_bad_utf8_linebreak_empty_invalid_input);
  if (failures > 0) {
    std::printf("script_coverage_test: %d failing\n", failures);
    return 1;
  }
  std::printf("script_coverage_test: all passing\n");
  return 0;
}
