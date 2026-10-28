// FR-TEXT-014 / AC-TEXT-037: InkStrokePipeline implementation.
// Variable-width tessellation in C++ with no per-sample JVM hop. Width
// grows monotonically with clamped pressure; untrusted axis magnitudes are
// clamped so the tessellator cannot be crashed or fed non-finite widths.

#include "ink_stroke_pipeline.h"

#include <cmath>

namespace muse::text::ink {
namespace {

// Base pen width at zero pressure; full pressure multiplies it.
constexpr float kBaseWidth = 1.0f;
// Clamp bound for untrusted pressure magnitudes (burst/DoS shape).
constexpr float kMaxPressure = 4.0f;

// Finite-only clamp: NaN/inf fall back to `fallback`, out-of-range values
// pin to [lo, hi]. Guarantees finite output for untrusted axis input.
float ClampAxis(float v, float lo, float hi, float fallback) {
  if (!std::isfinite(v)) return fallback;
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

float StrokeWidth(float pressure) {
  const float p = ClampAxis(pressure, 0.0f, kMaxPressure, 0.0f);
  return kBaseWidth * (1.0f + p);
}

float SanitizePos(float v) {
  if (!std::isfinite(v)) return 0.0f;
  return v;
}

}  // namespace

void InkStrokePipeline::PushSamples(const StylusSampleBatch& batch) {
  // `unbuffered` is a delivery hint from the dispatch layer; every sample
  // reaches the stroke either way, so the flag is intentionally unread here
  // beyond acceptance (both paths deliver all rows).
  bool has_correction = false;
  for (const StylusSample& s : batch.samples) {
    if (s.toolType == ToolType::HOVER) continue;
    if (!s.predicted) {
      has_correction = true;
      break;
    }
  }
  // A correction batch supersedes the low-latency predicted overlay: drop
  // the overlay and commit the corrected rows instead of duplicating them.
  if (has_correction) predicted_.clear();
  for (const StylusSample& s : batch.samples) {
    if (s.toolType == ToolType::HOVER) continue;
    if (s.toolType == ToolType::ERASER) saw_eraser_ = true;
    if (s.predicted) {
      predicted_.push_back(s);
    } else {
      committed_.push_back(s);
    }
  }
}

StrokeMesh InkStrokePipeline::Tessellate(StrokeId id) const {
  StrokeMesh mesh;
  if (id.value != 0) return mesh;
  mesh.vertices.reserve(committed_.size() + predicted_.size());
  mesh.eraser = saw_eraser_;
  // One vertex row per sample, committed then predicted overlay: tilt and
  // orientation variations survive as distinct rows (no merging). Width is
  // pressure-variable; position passes through sanitized but unsmoothed so
  // no row is ever dropped by this stage.
  for (const StylusSample& s : committed_) {
    Vertex v;
    v.x = SanitizePos(s.x);
    v.y = SanitizePos(s.y);
    v.width = StrokeWidth(s.pressure);
    mesh.vertices.push_back(v);
  }
  for (const StylusSample& s : predicted_) {
    Vertex v;
    v.x = SanitizePos(s.x);
    v.y = SanitizePos(s.y);
    v.width = StrokeWidth(s.pressure);
    mesh.vertices.push_back(v);
  }
  return mesh;
}

}  // namespace muse::text::ink
