// TC-TEXT-010 — AC-TEXT-010: shaping/measure stay in C++, no JNI on per-frame path.
// Hermetic harness: plain C++17, no test framework, no device, no JVM, no
// downloads. HarfBuzz/ICU boundaries are never called directly; cases drive
// only the public shaper interface with inline dummy inputs.
// Shape mirrors engine/runtime/tests/instantiate_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>

#include "muse/text/shaper.h"

// ASSUMPTION: FontHandle/TextStyle are default-constructible value types;
// StatusOr<T> exposes ok()/status()/operator*; ShapedRun exposes
// glyph_ids/advances/offsets/cluster_map; MeasuredLine exposes width_px;
// ShapedRunView is constructible from const ShapedRun&.
static bool check_ascii_shapes_with_cluster_map() {
  const std::string kText = "Hello";
  muse::text::FontHandle font{};
  muse::text::TextStyle style{};
  muse::text::StatusOr<muse::text::ShapedRun> result = muse::text::shapeRun(
      reinterpret_cast<const uint8_t*>(kText.data()), kText.size(), font, style);
  if (!result.ok()) return false;
  const muse::text::ShapedRun& run = *result;
  if (run.glyph_ids.empty()) return false;
  if (run.advances.size() != run.glyph_ids.size()) return false;
  if (run.offsets.size() != run.glyph_ids.size()) return false;
  if (run.cluster_map.size() != kText.size()) return false;
  return true;
}

// ASSUMPTION: shapeRun(nullptr, 0, ...) (empty) is valid and returns an
// empty ok run rather than an error.
static bool check_empty_input_returns_empty_run() {
  muse::text::FontHandle font{};
  muse::text::TextStyle style{};
  muse::text::StatusOr<muse::text::ShapedRun> result =
      muse::text::shapeRun(nullptr, 0, font, style);
  if (!result.ok()) return false;
  return (*result).glyph_ids.empty();
}

static bool check_bad_utf8_returns_invalid_script() {
  // Lone continuation bytes: malformed UTF-8, must fail gracefully (no
  // crash, no exception, no secret in status).
  const uint8_t kBad[] = {0x80, 0x80};
  muse::text::FontHandle font{};
  muse::text::TextStyle style{};
  muse::text::StatusOr<muse::text::ShapedRun> result =
      muse::text::shapeRun(kBad, sizeof(kBad), font, style);
  if (result.ok()) return false;
  return result.status() == muse::text::Status::kInvalidScript;
}

static bool check_truncated_sequence_returns_invalid_script() {
  // Truncated 3-byte sequence.
  const uint8_t kBad[] = {0xE2, 0x82};
  muse::text::FontHandle font{};
  muse::text::TextStyle style{};
  muse::text::StatusOr<muse::text::ShapedRun> result =
      muse::text::shapeRun(kBad, sizeof(kBad), font, style);
  if (result.ok()) return false;
  return result.status() == muse::text::Status::kInvalidScript;
}

static bool check_per_frame_path_takes_no_jvm_handle() {
  // AC-TEXT-010 unit-observable half: the per-frame shaping entry points must
  // be callable with no JNIEnv/JavaVM parameter. Verified at compile time by
  // checking the shapeRun/measureLine signatures. The profiled-frame half
  // (zero JNI calls under a profiler) is EXTERNAL.
  static_assert(std::is_same_v<decltype(&muse::text::shapeRun),
                               muse::text::StatusOr<muse::text::ShapedRun> (*)(
                                   const uint8_t*, size_t, muse::text::FontHandle,
                                   const muse::text::TextStyle&)>);
  static_assert(std::is_same_v<decltype(&muse::text::measureLine),
                               muse::text::MeasuredLine (*)(
                                   muse::text::ShapedRunView, float)>);
  return true;
}

static bool check_measure_line_respects_max_width() {
  const std::string kText = "Hello world";
  muse::text::FontHandle font{};
  muse::text::TextStyle style{};
  muse::text::StatusOr<muse::text::ShapedRun> shaped = muse::text::shapeRun(
      reinterpret_cast<const uint8_t*>(kText.data()), kText.size(), font, style);
  if (!shaped.ok()) return false;
  muse::text::MeasuredLine wide =
      muse::text::measureLine(muse::text::ShapedRunView{*shaped}, 100000.0f);
  muse::text::MeasuredLine narrow =
      muse::text::measureLine(muse::text::ShapedRunView{*shaped}, 1.0f);
  if (!(wide.width_px >= narrow.width_px)) return false;
  if (!(narrow.width_px <= 1.0f)) return false;
  return true;
}

static bool check_shape_is_deterministic_across_frames() {
  // Cacheability precondition (AC-TEXT-012 adjacent): same input twice must
  // give identical glyphs/advances so a frame cache is sound.
  const std::string kText = "Deterministic";
  muse::text::FontHandle font{};
  muse::text::TextStyle style{};
  auto a = muse::text::shapeRun(
      reinterpret_cast<const uint8_t*>(kText.data()), kText.size(), font, style);
  auto b = muse::text::shapeRun(
      reinterpret_cast<const uint8_t*>(kText.data()), kText.size(), font, style);
  if (!a.ok() || !b.ok()) return false;
  if ((*a).advances != (*b).advances) return false;
  if ((*a).glyph_ids != (*b).glyph_ids) return false;
  return true;
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
  RUN_CASE(check_ascii_shapes_with_cluster_map);
  RUN_CASE(check_empty_input_returns_empty_run);
  RUN_CASE(check_bad_utf8_returns_invalid_script);
  RUN_CASE(check_truncated_sequence_returns_invalid_script);
  RUN_CASE(check_per_frame_path_takes_no_jvm_handle);
  RUN_CASE(check_measure_line_respects_max_width);
  RUN_CASE(check_shape_is_deterministic_across_frames);
  if (failures > 0) {
    std::printf("jni_free_path_test: %d failing\n", failures);
    return 1;
  }
  std::printf("jni_free_path_test: all passing\n");
  return 0;
}
