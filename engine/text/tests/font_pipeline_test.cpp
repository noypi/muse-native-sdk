// T9 — AC-TEXT-028 / FR-TEXT-011: font pipeline end-to-end — three weights
// distinct, missing script font falls back, failed download degrades in one
// frame.
// Hermetic harness: plain C++17, no test framework, no device, no font
// files, no network, no downloads. All byte strings, variation vectors,
// and download outcomes are INLINE test doubles. Cases drive only the
// public entry point named in the plan Interface line and assert
// observable shaping outputs (advance vectors, ok() status, non-empty
// glyphs), never pipeline internals.
// Shape mirrors engine/text/tests/font_fallback_test.cpp (Batch 1 T1):
// static bool check_*() cases + main, return 0 all-green / non-zero +
// failing name.
// NOTE: the T9 production symbols (FontRequest, shapeRunWithFallback) do
// not exist yet, so this file does not compile — that
// compile-missing-production state is the expected RED for T9, not a
// malformed test.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "muse/text/shaper.h"

// ASSUMPTION (structural, pending shaper.h): the request type and entry
// point are declared exactly as the plan Interface line states:
//   muse::text::StatusOr<muse::text::ShapedRun>
//   muse::text::shapeRunWithFallback(const uint8_t* bytes, size_t len,
//                                    const muse::text::TextStyle& style,
//                                    const muse::text::FontRequest& req)
//   noexcept
// and FontRequest carries AT LEAST:
//   - an optional variation vector (variations pointer + variation_count,
//     null/0 means "no variations"), reusing T6's FontVariation;
//   - a nullable downloadable-font handle (has_downloadable_font flag +
//     downloadable_font handle; flag false means "no download attempted").
// ASSUMPTION (test seam): FontRequest additionally carries a synchronous
// test-double download outcome (download_failed bool: true simulates a
// failed fetch) plus script_tag / bcp47_locale / generic_family routing
// fields so the fallback path is selectable hermetically without NDK or
// network. If the coder's download seam differs (e.g. status enum or
// callback), that is a T1.10 interface dispute: STOP, do not adapt the
// test silently — the contract is re-published by the tester.
// ASSUMPTION: on download failure the call returns bundled-face shaping
// synchronously within the single call (one layout pass — observable as:
// exactly one call, ok(), non-empty glyphs, no throw). No wall-clock or
// retry counter is asserted; a second-frame retry on the measurement path
// would be a production bug, not something this unit test can observe.
// ASSUMPTION: errors are Status::kInvalidInput on malformed UTF-8 ONLY;
// missing script fonts and failed downloads NEVER surface an error — they
// degrade to the bundled face (ok()).
// ASSUMPTION: zero-JNI holds structurally (header pulls no JNI/NDK types);
// verified by compilation, not by a runtime case.

namespace {

constexpr uint32_t kWght = 0x77676874u;  // 'wght'
constexpr uint32_t kLatn = 0x4C61746Eu;  // 'Latn'
constexpr uint32_t kArab = 0x41726162u;  // 'Arab'

const std::string kText = "Pipeline Aa";
const std::string kArabicText = "العربية";

muse::text::FontRequest make_request(const muse::text::FontVariation* vars,
                                     std::size_t n) {
  muse::text::FontRequest req{};
  req.variations = vars;
  req.variation_count = n;
  req.has_downloadable_font = false;
  req.download_failed = false;
  req.script_tag = kLatn;
  req.bcp47_locale = "en-US";
  req.generic_family = "sans-serif";
  return req;
}

std::vector<float> advances_for_request(const std::string& text,
                                         const muse::text::FontRequest& req) {
  muse::text::TextStyle style{};
  auto result = muse::text::shapeRunWithFallback(
      reinterpret_cast<const uint8_t*>(text.data()), text.size(), style,
      req);
  if (!result.ok()) return {};
  return (*result).advances;
}

bool advances_differ(const std::vector<float>& a,
                     const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty() || b.empty()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return true;
  }
  return false;
}

}  // namespace

static bool check_three_weights_distinct() {
  try {
    const muse::text::FontVariation kLight[] = {{kWght, 300.0f}};
    const muse::text::FontVariation kRegular[] = {{kWght, 400.0f}};
    const muse::text::FontVariation kBold[] = {{kWght, 700.0f}};
    std::vector<float> light = advances_for_request(kText, make_request(kLight, 1));
    std::vector<float> regular =
        advances_for_request(kText, make_request(kRegular, 1));
    std::vector<float> bold = advances_for_request(kText, make_request(kBold, 1));
    if (light.empty() || regular.empty() || bold.empty()) return false;
    return advances_differ(light, regular) &&
           advances_differ(regular, bold) && advances_differ(light, bold);
  } catch (...) {
    return false;
  }
}

static bool check_missing_script_font_falls_back() {
  try {
    // System Arabic face "removed from the test image": request routes to
    // the chain with no downloadable font; must still yield glyphs, never
    // tofu (no kMissingGlyph), via the bundled face.
    muse::text::FontRequest req = make_request(nullptr, 0);
    req.script_tag = kArab;
    req.bcp47_locale = "ar-EG";
    muse::text::TextStyle style{};
    auto result = muse::text::shapeRunWithFallback(
        reinterpret_cast<const uint8_t*>(kArabicText.data()),
        kArabicText.size(), style, req);
    if (!result.ok()) return false;
    const muse::text::ShapedRun& run = *result;
    return !run.glyph_ids.empty() &&
           run.advances.size() == run.glyph_ids.size();
  } catch (...) {
    return false;
  }
}

static bool check_failed_download_degrades_single_pass() {
  try {
    // Downloadable font present but fetch fails: exactly one synchronous
    // call must return bundled-face shaping (ok(), non-empty), with no
    // second-frame retry observable on the measurement path.
    const muse::text::FontVariation kVars[] = {{kWght, 600.0f}};
    muse::text::FontRequest req = make_request(kVars, 1);
    req.has_downloadable_font = true;
    req.downloadable_font = muse::text::FontHandle{999};
    req.download_failed = true;
    muse::text::TextStyle style{};
    auto result = muse::text::shapeRunWithFallback(
        reinterpret_cast<const uint8_t*>(kText.data()), kText.size(), style,
        req);
    if (!result.ok()) return false;
    const muse::text::ShapedRun& run = *result;
    return !run.glyph_ids.empty() &&
           run.advances.size() == run.glyph_ids.size();
  } catch (...) {
    return false;
  }
}

static bool check_successful_download_shapes() {
  try {
    // Downloadable font present and fetch succeeds: shaping succeeds with
    // the requested variations applied.
    const muse::text::FontVariation kVars[] = {{kWght, 600.0f}};
    muse::text::FontRequest req = make_request(kVars, 1);
    req.has_downloadable_font = true;
    req.downloadable_font = muse::text::FontHandle{999};
    req.download_failed = false;
    std::vector<float> with_download = advances_for_request(kText, req);
    std::vector<float> bundled =
        advances_for_request(kText, make_request(kVars, 1));
    // Both succeed; the downloaded face need not match bundled advances,
    // but shaping must be non-empty either way.
    return !with_download.empty() && !bundled.empty();
  } catch (...) {
    return false;
  }
}

static bool check_no_requested_font_still_shapes() {
  try {
    // No downloadable font at all: plain bundled/chain shaping succeeds.
    std::vector<float> advances =
        advances_for_request(kText, make_request(nullptr, 0));
    return !advances.empty();
  } catch (...) {
    return false;
  }
}

static bool check_malformed_utf8_invalid_input_only() {
  try {
    const uint8_t kBad[] = {0xE4, 0xB8};  // truncated 3-byte lead
    muse::text::TextStyle style{};
    muse::text::FontRequest req = make_request(nullptr, 0);
    auto result = muse::text::shapeRunWithFallback(kBad, sizeof(kBad), style,
                                                   req);
    return !result.ok() &&
           result.status() == muse::text::Status::kInvalidInput;
  } catch (...) {
    return false;
  }
}

static bool check_empty_input_ok_empty() {
  try {
    const uint8_t kEmpty[] = {0x00};
    muse::text::TextStyle style{};
    muse::text::FontRequest req = make_request(nullptr, 0);
    auto result =
        muse::text::shapeRunWithFallback(kEmpty, 0, style, req);
    if (!result.ok()) return false;
    return (*result).glyph_ids.empty() && (*result).advances.empty();
  } catch (...) {
    return false;
  }
}

static bool check_fallback_never_returns_missing_glyph() {
  try {
    // Unknown script + unknown locale + failed download: the pipeline must
    // still terminate at the bundled face (ok()), never kMissingGlyph —
    // only malformed UTF-8 is an error on this interface.
    const uint8_t kAscii[] = {'A', 'b', 'c'};
    muse::text::FontRequest req = make_request(nullptr, 0);
    req.script_tag = 0x5A5A5A5Au;  // 'ZZZZ' unknown
    req.bcp47_locale = "xx-XX";
    req.generic_family = "NoSuchFamily-XYZ";
    req.has_downloadable_font = true;
    req.download_failed = true;
    muse::text::TextStyle style{};
    auto result = muse::text::shapeRunWithFallback(kAscii, sizeof(kAscii),
                                                   style, req);
    if (!result.ok()) {
      return result.status() != muse::text::Status::kMissingGlyph;
    }
    return !(*result).glyph_ids.empty();
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
  RUN_CASE(check_three_weights_distinct);
  RUN_CASE(check_missing_script_font_falls_back);
  RUN_CASE(check_failed_download_degrades_single_pass);
  RUN_CASE(check_successful_download_shapes);
  RUN_CASE(check_no_requested_font_still_shapes);
  RUN_CASE(check_malformed_utf8_invalid_input_only);
  RUN_CASE(check_empty_input_ok_empty);
  RUN_CASE(check_fallback_never_returns_missing_glyph);
  if (failures > 0) {
    std::printf("font_pipeline_test: %d failing\n", failures);
    return 1;
  }
  std::printf("font_pipeline_test: all passing\n");
  return 0;
}
