// T2 — FR-TEXT-014 / AC-TEXT-037: C++ ink stroke pipeline for stylus axes.
// Hermetic harness: plain C++17, no test framework, no device, no JVM/JNI, no
// downloads. Cases drive only the plan Interface (reproduced verbatim below)
// with synthetic inline batches: historical + predicted samples, unbuffered
// dispatch flag, hover/eraser tool types. Shape mirrors
// engine/text/tests/stylus_samples_test.cpp: static bool check_*() cases +
// main, return 0 all-green / non-zero + failing name.
//
// The sample conventions mirror engine/text/tests/stylus_samples_test.cpp
// (FR-TEXT-004): per-sample pressure/tilt/orientation axes plus tool flags,
// copied intact through the C++ batch path.
#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <vector>

#include "../ink_stroke_pipeline.h"

// Plan Interface lines (verbatim):
// `void PushSamples(const StylusSampleBatch& batch)` where
// `StylusSampleBatch{ samples: [{pressure, tilt, orientation, toolType, predicted: bool}], unbuffered: bool }`;
// `StrokeMesh Tessellate(StrokeId id) const`. No JNI/JVM type appears in the signature.
//
// ASSUMPTION (structural, for the coder — see contract T2.md): the pipeline
// lives in `muse::text::ink` as class `InkStrokePipeline` exposing the two
// member functions above; `StylusSampleBatch{ samples, unbuffered }` with
// `samples` a vector of `StylusSample{ x, y, pressure, tilt, orientation,
// toolType, predicted }` (x/y mirror FR-TEXT-004 StylusSample — the plan line
// abbreviates the element to the ink-relevant axes); `ToolType` is an enum
// with STYLUS/ERASER/HOVER; `StrokeId` brace-constructs from int with the
// first pushed stroke at id 0; `StrokeMesh{ vertices, eraser }` with
// `Vertex{ x, y, width }` (variable-width tessellation per FR-TEXT-014 Notes).

namespace ink = muse::text::ink;

static ink::StylusSample make_sample(float x, float y, float pressure,
                                     float tilt, float orientation,
                                     ink::ToolType tool, bool predicted) {
  ink::StylusSample s{};
  s.x = x;
  s.y = y;
  s.pressure = pressure;
  s.tilt = tilt;
  s.orientation = orientation;
  s.toolType = tool;
  s.predicted = predicted;
  return s;
}

static ink::StylusSampleBatch make_batch(
    std::vector<ink::StylusSample> samples, bool unbuffered) {
  ink::StylusSampleBatch b{};
  b.samples = std::move(samples);
  b.unbuffered = unbuffered;
  return b;
}

static std::vector<ink::StylusSample> line_samples(std::size_t n,
                                                   float pressure, float tilt,
                                                   float orientation,
                                                   ink::ToolType tool,
                                                   bool predicted) {
  std::vector<ink::StylusSample> out;
  for (std::size_t i = 0; i < n; ++i) {
    out.push_back(make_sample(static_cast<float>(i), 0.0f, pressure, tilt,
                              orientation, tool, predicted));
  }
  return out;
}

static float max_width(const ink::StrokeMesh& mesh) {
  float m = 0.0f;
  for (const auto& v : mesh.vertices) {
    if (v.width > m) m = v.width;
  }
  return m;
}

static bool check_push_batch_tessellates_nonempty_mesh() {
  ink::InkStrokePipeline pipe;
  pipe.PushSamples(
      make_batch(line_samples(8, 0.5f, 0.1f, 0.2f, ink::ToolType::STYLUS,
                              /*predicted=*/false),
                 /*unbuffered=*/true));
  const ink::StrokeMesh mesh = pipe.Tessellate(ink::StrokeId{0});
  return !mesh.vertices.empty();
}

static bool check_pressure_modulates_stroke_width() {
  ink::InkStrokePipeline light, heavy;
  light.PushSamples(
      make_batch(line_samples(8, 0.1f, 0.0f, 0.0f, ink::ToolType::STYLUS,
                              /*predicted=*/false),
                 /*unbuffered=*/true));
  heavy.PushSamples(
      make_batch(line_samples(8, 1.0f, 0.0f, 0.0f, ink::ToolType::STYLUS,
                              /*predicted=*/false),
                 /*unbuffered=*/true));
  const float w_light = max_width(light.Tessellate(ink::StrokeId{0}));
  const float w_heavy = max_width(heavy.Tessellate(ink::StrokeId{0}));
  return w_heavy > w_light && w_light >= 0.0f;
}

static bool check_tilt_orientation_carried_per_sample() {
  // Every pushed sample (with distinct tilt/orientation axes) reaches
  // tessellation: one vertex row per sample, none dropped or merged.
  ink::InkStrokePipeline pipe;
  std::vector<ink::StylusSample> samples;
  for (int i = 0; i < 6; ++i) {
    samples.push_back(make_sample(
        static_cast<float>(i), static_cast<float>(i), 0.5f,
        0.1f * static_cast<float>(i), 0.2f * static_cast<float>(i),
        ink::ToolType::STYLUS, /*predicted=*/false));
  }
  pipe.PushSamples(make_batch(std::move(samples), /*unbuffered=*/true));
  return pipe.Tessellate(ink::StrokeId{0}).vertices.size() == 6;
}

static bool check_predicted_samples_corrected_not_duplicated() {
  // Predicted samples tessellate (low-latency path), and the later
  // non-predicted correction replaces them instead of doubling the mesh.
  ink::InkStrokePipeline pipe;
  pipe.PushSamples(
      make_batch(line_samples(4, 0.5f, 0.1f, 0.2f, ink::ToolType::STYLUS,
                              /*predicted=*/true),
                 /*unbuffered=*/true));
  const std::size_t predicted_count =
      pipe.Tessellate(ink::StrokeId{0}).vertices.size();
  if (predicted_count == 0) return false;
  pipe.PushSamples(
      make_batch(line_samples(4, 0.5f, 0.1f, 0.2f, ink::ToolType::STYLUS,
                              /*predicted=*/false),
                 /*unbuffered=*/true));
  return pipe.Tessellate(ink::StrokeId{0}).vertices.size() == predicted_count;
}

static bool check_hover_emits_no_stroke() {
  ink::InkStrokePipeline hover_only, mixed;
  hover_only.PushSamples(
      make_batch(line_samples(5, 0.0f, 0.1f, 0.0f, ink::ToolType::HOVER,
                              /*predicted=*/false),
                 /*unbuffered=*/true));
  if (!hover_only.Tessellate(ink::StrokeId{0}).vertices.empty()) return false;
  // Hover samples interleaved with pen samples contribute no vertices.
  std::vector<ink::StylusSample> samples =
      line_samples(4, 0.5f, 0.0f, 0.0f, ink::ToolType::STYLUS, false);
  const std::vector<ink::StylusSample> hover =
      line_samples(3, 0.0f, 0.1f, 0.0f, ink::ToolType::HOVER, false);
  samples.insert(samples.end(), hover.begin(), hover.end());
  mixed.PushSamples(make_batch(std::move(samples), /*unbuffered=*/true));
  return mixed.Tessellate(ink::StrokeId{0}).vertices.size() == 4;
}

static bool check_eraser_toggles_erase_semantics() {
  ink::InkStrokePipeline pen, eraser;
  pen.PushSamples(make_batch(line_samples(4, 0.5f, 0.0f, 0.0f,
                                          ink::ToolType::STYLUS, false),
                             /*unbuffered=*/true));
  eraser.PushSamples(make_batch(line_samples(4, 0.5f, 0.0f, 0.0f,
                                             ink::ToolType::ERASER, false),
                                /*unbuffered=*/true));
  const ink::StrokeMesh pen_mesh = pen.Tessellate(ink::StrokeId{0});
  const ink::StrokeMesh eraser_mesh = eraser.Tessellate(ink::StrokeId{0});
  if (pen_mesh.vertices.empty()) return false;
  if (eraser_mesh.vertices.empty()) return false;
  return !pen_mesh.eraser && eraser_mesh.eraser;
}

static bool check_unbuffered_flag_does_not_drop_samples() {
  ink::InkStrokePipeline buffered, unbuffered;
  buffered.PushSamples(
      make_batch(line_samples(6, 0.5f, 0.1f, 0.2f, ink::ToolType::STYLUS,
                              /*predicted=*/false),
                 /*unbuffered=*/false));
  unbuffered.PushSamples(
      make_batch(line_samples(6, 0.5f, 0.1f, 0.2f, ink::ToolType::STYLUS,
                              /*predicted=*/false),
                 /*unbuffered=*/true));
  return buffered.Tessellate(ink::StrokeId{0}).vertices.size() ==
         unbuffered.Tessellate(ink::StrokeId{0}).vertices.size();
}

static bool check_empty_batch_tessellates_empty() {
  ink::InkStrokePipeline pipe;
  if (!pipe.Tessellate(ink::StrokeId{0}).vertices.empty()) return false;
  pipe.PushSamples(make_batch({}, /*unbuffered=*/true));
  return pipe.Tessellate(ink::StrokeId{0}).vertices.empty();
}

static bool check_extreme_axes_do_not_throw() {
  // Untrusted axis magnitudes must not crash the tessellator.
  ink::InkStrokePipeline pipe;
  pipe.PushSamples(make_batch({make_sample(1.0f, 1.0f, 1e6f, -1e6f, 1e6f,
                                           ink::ToolType::STYLUS, false)},
                              /*unbuffered=*/true));
  const ink::StrokeMesh mesh = pipe.Tessellate(ink::StrokeId{0});
  (void)max_width(mesh);
  return true;
}

static bool check_large_batch_is_bounded() {
  // A hostile/oversized batch (unbuffered burst) still returns.
  ink::InkStrokePipeline pipe;
  pipe.PushSamples(
      make_batch(line_samples(50000, 0.5f, 0.0f, 0.0f, ink::ToolType::STYLUS,
                              /*predicted=*/false),
                 /*unbuffered=*/true));
  return !pipe.Tessellate(ink::StrokeId{0}).vertices.empty();
}

static bool check_signature_takes_no_jvm_handle() {
  // AC-TEXT-037 unit-observable half: batch entry and tessellation are pure
  // C++ member functions with no JNIEnv/JavaVM parameter. Verified at compile
  // time. The "zero JNI calls under a profiler" half is EXTERNAL.
  static_assert(std::is_same_v<
                decltype(&ink::InkStrokePipeline::PushSamples),
                void (ink::InkStrokePipeline::*)(const ink::StylusSampleBatch&)>);
  static_assert(std::is_same_v<
                decltype(&ink::InkStrokePipeline::Tessellate),
                ink::StrokeMesh (ink::InkStrokePipeline::*)(ink::StrokeId) const>);
  // Batch shape carries exactly the plan's fields with the plan's types.
  static_assert(std::is_same_v<decltype(ink::StylusSampleBatch::unbuffered),
                               bool>);
  static_assert(
      std::is_same_v<decltype(ink::StylusSample::pressure), float>);
  static_assert(std::is_same_v<decltype(ink::StylusSample::tilt), float>);
  static_assert(
      std::is_same_v<decltype(ink::StylusSample::orientation), float>);
  static_assert(
      std::is_same_v<decltype(ink::StylusSample::predicted), bool>);
  return true;
}

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_push_batch_tessellates_nonempty_mesh);
  RUN_CASE(check_pressure_modulates_stroke_width);
  RUN_CASE(check_tilt_orientation_carried_per_sample);
  RUN_CASE(check_predicted_samples_corrected_not_duplicated);
  RUN_CASE(check_hover_emits_no_stroke);
  RUN_CASE(check_eraser_toggles_erase_semantics);
  RUN_CASE(check_unbuffered_flag_does_not_drop_samples);
  RUN_CASE(check_empty_batch_tessellates_empty);
  RUN_CASE(check_extreme_axes_do_not_throw);
  RUN_CASE(check_large_batch_is_bounded);
  RUN_CASE(check_signature_takes_no_jvm_handle);
  if (failures > 0) {
    std::printf("stylus_axes_test: %d failing\n", failures);
    return 1;
  }
  std::printf("stylus_axes_test: all passing\n");
  return 0;
}
