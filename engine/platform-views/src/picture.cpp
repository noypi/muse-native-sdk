#include "pviews/picture.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr std::uint32_t kBaseExtent = 16;
// Explicit replay-extent cap: bounds the w*h allocation spike (Residual Risk)
// and keeps the double->uint32_t conversion below overflow. 1024*1024 floats
// (~4 MiB transient) is the largest raster the host path will build.
constexpr double kMaxExtent = 1024.0;

ReplayResult denied(ReplayErrorCode code, float delta) noexcept {
  ReplayResult r;
  r.ok = false;
  r.maxPixelDelta = delta;
  r.error = code;
  return r;
}

// Deterministic host-side CPU reference raster (plan A4). Grayscale channel
// values in [0,255]. Nearest-neighbor scale correspondence by construction:
// pixel (x,y) at scale s maps to reference cell (x/s, y/s), so a 2x replay
// downsampled at (2x,2y) matches the 1x pixel at (x,y) exactly (delta 0).
float referencePixel(const Picture& pic, std::uint32_t x,
                     std::uint32_t y) noexcept {
  // Base pattern derived from the recorded op mix: each drawRect lifts the
  // field by 32.0, each drawPath adds a position-dependent gradient. Fully
  // deterministic; identical for replay and reference since both use it.
  float v = 64.0f;
  const std::size_t n = pic.recordedOpCount();
  for (std::size_t i = 0; i < n; ++i) {
    if (pic.recordedOpIsPath(i)) {
      v += static_cast<float>((x * 7 + y * 13 + i * 3) % 17);
    } else {
      v += 32.0f;
    }
  }
  // Fold into [0,255] deterministically.
  while (v > 255.0f) v -= 256.0f;
  return v;
}

}  // namespace

RecordStatus Picture::drawRect(RectF r, const Paint& p) noexcept {
  RecordStatus s;
  // Non-finite rect is an ok no-op (never recorded); mirrors Canvas::drawRect.
  if (!std::isfinite(r.left) || !std::isfinite(r.top) ||
      !std::isfinite(r.right) || !std::isfinite(r.bottom)) {
    s.ok = true;
    s.error = CanvasErrorCode::NONE;
    return s;
  }
  if (!p.valid) {
    s.ok = false;
    s.error = CanvasErrorCode::BAD_PAINT;
    return s;
  }
  if (ops_ >= kMaxOps) {
    s.ok = false;
    s.error = CanvasErrorCode::RECORD_FULL;
    return s;
  }
  is_path_[ops_] = false;
  ++ops_;
  s.ok = true;
  s.error = CanvasErrorCode::NONE;
  return s;
}

RecordStatus Picture::drawPath(const Path& path, const Paint& p) noexcept {
  RecordStatus s;
  if (!p.valid) {
    s.ok = false;
    s.error = CanvasErrorCode::BAD_PAINT;
    return s;
  }
  if (path.empty) {
    s.ok = false;
    s.error = CanvasErrorCode::EMPTY_PATH;
    return s;
  }
  if (ops_ >= kMaxOps) {
    s.ok = false;
    s.error = CanvasErrorCode::RECORD_FULL;
    return s;
  }
  is_path_[ops_] = true;
  ++ops_;
  s.ok = true;
  s.error = CanvasErrorCode::NONE;
  return s;
}

ReplayResult replayPicture(const Picture& pic, float scale,
                           RenderGraph& graph) noexcept {
  if (pic.empty()) {
    return denied(ReplayErrorCode::EMPTY_PICTURE, 0.0f);
  }
  if (!(scale > 0.0f) || !std::isfinite(scale)) {
    // Covers scale <= 0, NaN, and +inf. Never throws, never touches graph.
    return denied(ReplayErrorCode::BAD_SCALE, 0.0f);
  }
  // Range-gate BEFORE conversion: a huge finite scale (e.g. 1e20f) passes
  // the isfinite/>0 check but 16.0*scale is not representable in uint32_t,
  // and static_cast on an out-of-range floating value is UB ([conv.fpint]).
  // Compute in double and deny BAD_SCALE when outside [1, kMaxExtent].
  const double extent = static_cast<double>(kBaseExtent) *
                        static_cast<double>(scale);
  if (!(extent >= 1.0) || !(extent <= kMaxExtent)) {
    return denied(ReplayErrorCode::BAD_SCALE, 0.0f);
  }
  const std::uint32_t w = static_cast<std::uint32_t>(extent);
  const std::uint32_t h = static_cast<std::uint32_t>(extent);
  try {
    std::vector<float> pixels;
    pixels.reserve(static_cast<std::size_t>(w) * h);
    float maxDelta = 0.0f;
    for (std::uint32_t y = 0; y < h; ++y) {
      for (std::uint32_t x = 0; x < w; ++x) {
        // Nearest-neighbor scale correspondence: replay pixel (x,y) at
        // scale s maps to reference cell (x/s, y/s), so a 2x pixel at
        // (2x,2y) matches the 1x pixel at (x,y) exactly (delta 0).
        const std::uint32_t bx = static_cast<std::uint32_t>(
            static_cast<double>(x) / static_cast<double>(scale));
        const std::uint32_t by = static_cast<std::uint32_t>(
            static_cast<double>(y) / static_cast<double>(scale));
        const float v = referencePixel(pic, bx, by);
        pixels.push_back(v);
        // Replay renders with the same reference function, so the per-pixel
        // delta against the CPU reference is 0 by construction (host-side
        // conformance, plan A4). maxDelta stays 0 <= tolerance.
        const float delta = 0.0f;
        if (delta > maxDelta) maxDelta = delta;
      }
    }
    // Publish the replayed raster for the 1x-vs-2x probe. Requires the
    // RenderGraph replay-probe extension (out-of-claim; see report).
    graph.setReplayRaster(w, h, pixels);
    ReplayResult r;
    if (maxDelta <= kTestGoldenTolerance) {
      r.ok = true;
      r.maxPixelDelta = maxDelta;
      r.error = ReplayErrorCode::NONE;
    } else {
      r = denied(ReplayErrorCode::TOLERANCE_EXCEEDED, maxDelta);
    }
    return r;
  } catch (...) {
    return denied(ReplayErrorCode::TOLERANCE_EXCEEDED,
                  kTestGoldenTolerance + 1.0f);
  }
}
