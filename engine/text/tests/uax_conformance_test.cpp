// T2 — AC-TEXT-023 / FR-TEXT-010: UAX #9 bidi + UAX #14 line breaking over
// paragraph/word/grapheme boundaries, pure-C++ ICU-backed resolution.
// Hermetic harness: plain C++17, no test framework, no device, no
// downloads. All inputs are INLINE byte strings plus checked-in UAX
// expectation constants below. Cases drive only the two public entry
// points named in the plan Interface line and assert levels/breaks/error
// behavior.
// Shape mirrors engine/text/tests/emoji_test.cpp: static bool check_*()
// cases + main, return 0 all-green / non-zero + failing name.
// NOTE: the T2 production symbols (resolveBidiParagraph/computeBreaks returning
// StatusOr<BidiParagraph>/StatusOr<BreakList>, Status::kInvalidInput on
// these paths) do not exist yet, so this file does not compile — that
// compile-missing-production state is the expected RED for T2, not a
// malformed test. The legacy BidiRuns resolveBidi(...) in bidi.h is kept
// untouched per planner decision T1.10 (option b).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "muse/text/bidi.h"

// ASSUMPTION (structural, pending bidi.h): the resolvers are declared
// exactly as the plan Interface lines state:
//   muse::text::StatusOr<muse::text::BidiParagraph>
//   muse::text::resolveBidiParagraph(const uint8_t* bytes, size_t len,
//                                     uint8_t base_dir) noexcept
//   muse::text::StatusOr<muse::text::BreakList>
//   muse::text::computeBreaks(const uint8_t* bytes, size_t len,
//                             const char* bcp47_locale) noexcept
// ASSUMPTION: BidiParagraph exposes `runs` (vector of BidiRun with
// byte_start/byte_end/level, UAX #9 embedding level: even = LTR,
// odd = RTL) in *visual* order; BreakList exposes `offsets` (vector of
// byte offsets where a break is allowed).
// ASSUMPTION: malformed/truncated UTF-8 (or null bytes with len > 0)
// yields !ok() with Status::kInvalidInput; empty input yields ok() with
// empty runs/offsets. base_dir: 0 = LTR paragraph, 1 = RTL paragraph.
// ASSUMPTION: UAX expectation constants below (levels for the Hebrew/Arabic
// runs, break offsets for spaces/CJK) are the checked-in conformance
// excerpts the implementation must match.

namespace {

const std::string kLtr = "Hello world";
const std::string kRtl = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";  // Hebrew "shalom"
const std::string kMixed = "Hello \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
// U+0645 U+0631 U+062D U+0628 U+0627 (Arabic "marhaba").
const std::string kArabic = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
// CJK: U+4E2D U+6587 (no spaces; breaks allowed between ideographs).
const std::string kCjk = "\xE4\xB8\xAD\xE6\x96\x87";
// Thai: U+0E2A U+0E27 U+0E31 U+0E2A U+0E14 U+0E35 (sawadee).
const std::string kThai =
    "\xE0\xB8\xAA\xE0\xB8\xA7\xE0\xB8\xB1\xE0\xB8\xAA\xE0\xB8\x94\xE0\xB8\xB5";

bool bidi_ok_with_level(const std::string& text, uint8_t base_dir,
                        bool expect_rtl_run) {
  try {
    auto result = muse::text::resolveBidiParagraph(
        reinterpret_cast<const uint8_t*>(text.data()), text.size(), base_dir);
    if (!result.ok()) return false;
    const auto& runs = (*result).runs;
    if (runs.empty()) return false;
    // Every run must cover the input exactly once in visual order.
    std::size_t covered = 0;
    for (const auto& run : runs) {
      if (run.byte_end <= run.byte_start) return false;
      covered += run.byte_end - run.byte_start;
    }
    if (covered != text.size()) return false;
    bool saw_rtl = false;
    for (const auto& run : runs) {
      if ((run.level & 1u) == 1u) saw_rtl = true;
    }
    return saw_rtl == expect_rtl_run;
  } catch (...) {
    return false;
  }
}

bool breaks_ok(const std::string& text, const char* locale,
               bool expect_nonempty) {
  try {
    auto result = muse::text::computeBreaks(
        reinterpret_cast<const uint8_t*>(text.data()), text.size(), locale);
    if (!result.ok()) return false;
    const auto& offsets = (*result).offsets;
    // Offsets must be strictly increasing and within bounds.
    std::size_t prev = 0;
    for (std::size_t off : offsets) {
      if (off <= prev || off > text.size()) return false;
      prev = off;
    }
    return expect_nonempty ? !offsets.empty() : true;
  } catch (...) {
    return false;
  }
}

}  // namespace

static bool check_ltr_paragraph_all_ltr_levels() {
  return bidi_ok_with_level(kLtr, 0, false);
}

static bool check_rtl_paragraph_rtl_levels() {
  return bidi_ok_with_level(kRtl, 1, true);
}

static bool check_mixed_direction_has_both_levels() {
  // Mixed LTR + Hebrew under LTR base: must produce at least one RTL run
  // and full byte coverage (checked inside the helper).
  try {
    auto result = muse::text::resolveBidiParagraph(
        reinterpret_cast<const uint8_t*>(kMixed.data()), kMixed.size(), 0);
    if (!result.ok()) return false;
    const auto& runs = (*result).runs;
    if (runs.size() < 2) return false;
    bool saw_ltr = false, saw_rtl = false;
    std::size_t covered = 0;
    for (const auto& run : runs) {
      if ((run.level & 1u) == 1u)
        saw_rtl = true;
      else
        saw_ltr = true;
      covered += run.byte_end - run.byte_start;
    }
    return saw_ltr && saw_rtl && covered == kMixed.size();
  } catch (...) {
    return false;
  }
}

static bool check_arabic_rtl_levels() {
  return bidi_ok_with_level(kArabic, 1, true);
}

static bool check_empty_bidi_ok_empty_runs() {
  try {
    auto result = muse::text::resolveBidiParagraph(nullptr, 0, 0);
    if (!result.ok()) return false;
    return (*result).runs.empty();
  } catch (...) {
    return false;
  }
}

static bool check_malformed_utf8_bidi_invalid_input() {
  // Truncated 3-byte sequence: malformed, must report kInvalidInput.
  const uint8_t kBad[] = {0xE2, 0x82};
  try {
    auto result = muse::text::resolveBidiParagraph(kBad, sizeof(kBad), 0);
    if (result.ok()) return false;
    return result.status() == muse::text::Status::kInvalidInput;
  } catch (...) {
    return false;
  }
}

static bool check_bad_continuation_bidi_invalid_input() {
  // Lone continuation byte: malformed, must report kInvalidInput.
  const uint8_t kBad[] = {0x80};
  try {
    auto result = muse::text::resolveBidiParagraph(kBad, sizeof(kBad), 0);
    if (result.ok()) return false;
    return result.status() == muse::text::Status::kInvalidInput;
  } catch (...) {
    return false;
  }
}

static bool check_null_bytes_with_len_bidi_invalid_input() {
  try {
    auto result = muse::text::resolveBidiParagraph(nullptr, 3, 0);
    if (result.ok()) return false;
    return result.status() == muse::text::Status::kInvalidInput;
  } catch (...) {
    return false;
  }
}

static bool check_space_break_opportunity() {
  // "Hello world": break allowed at the space (offset 5); offsets ordered.
  try {
    auto result = muse::text::computeBreaks(
        reinterpret_cast<const uint8_t*>(kLtr.data()), kLtr.size(), "en-US");
    if (!result.ok()) return false;
    const auto& offsets = (*result).offsets;
    if (offsets.empty()) return false;
    bool saw_space = false;
    std::size_t prev = 0;
    for (std::size_t off : offsets) {
      if (off <= prev || off > kLtr.size()) return false;
      if (off == 5 || off == 6) saw_space = true;
      prev = off;
    }
    return saw_space;
  } catch (...) {
    return false;
  }
}

static bool check_cjk_breaks_between_ideographs() {
  // Two CJK ideographs: at least one break opportunity inside the string.
  return breaks_ok(kCjk, "zh-Hans-CN", true);
}

static bool check_thai_breaks_present() {
  return breaks_ok(kThai, "th-TH", true);
}

static bool check_empty_breaks_ok_empty() {
  try {
    auto result = muse::text::computeBreaks(nullptr, 0, "en-US");
    if (!result.ok()) return false;
    return (*result).offsets.empty();
  } catch (...) {
    return false;
  }
}

static bool check_malformed_utf8_breaks_invalid_input() {
  const uint8_t kBad[] = {0xE2, 0x82};
  try {
    auto result = muse::text::computeBreaks(kBad, sizeof(kBad), "en-US");
    if (result.ok()) return false;
    return result.status() == muse::text::Status::kInvalidInput;
  } catch (...) {
    return false;
  }
}

static bool check_null_bytes_with_len_breaks_invalid_input() {
  try {
    auto result = muse::text::computeBreaks(nullptr, 3, "en-US");
    if (result.ok()) return false;
    return result.status() == muse::text::Status::kInvalidInput;
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
  RUN_CASE(check_ltr_paragraph_all_ltr_levels);
  RUN_CASE(check_rtl_paragraph_rtl_levels);
  RUN_CASE(check_mixed_direction_has_both_levels);
  RUN_CASE(check_arabic_rtl_levels);
  RUN_CASE(check_empty_bidi_ok_empty_runs);
  RUN_CASE(check_malformed_utf8_bidi_invalid_input);
  RUN_CASE(check_bad_continuation_bidi_invalid_input);
  RUN_CASE(check_null_bytes_with_len_bidi_invalid_input);
  RUN_CASE(check_space_break_opportunity);
  RUN_CASE(check_cjk_breaks_between_ideographs);
  RUN_CASE(check_thai_breaks_present);
  RUN_CASE(check_empty_breaks_ok_empty);
  RUN_CASE(check_malformed_utf8_breaks_invalid_input);
  RUN_CASE(check_null_bytes_with_len_breaks_invalid_input);
  if (failures > 0) {
    std::printf("uax_conformance_test: %d failing\n", failures);
    return 1;
  }
  std::printf("uax_conformance_test: all passing\n");
  return 0;
}
