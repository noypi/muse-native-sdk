#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "muse/text/text_types.h"
#include "runtime/vulkan_baseline.h"

namespace runtime {

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

enum class PrimitiveKind {
  Rect,
  RoundedRect,
  Image,
  Text,
  StateLayer,
  Ripple,
  ElevationShadow,
  CircleArc,
  VectorIcon,
  ShapedText,
  OverlayScrim,
};

struct PrimitivePayload {
  std::uint32_t stateColor = 0;
  float stateOpacity = 1.0f;
  Rect rippleBounds;
  float rippleProgress = 0.0f;
  std::uint32_t rippleColor = 0;
  float elevationDp = 0.0f;
  float shadowBlur = 0.0f;
  std::uint32_t shadowColor = 0;
  float arcStartDeg = 0.0f;
  float arcSweepDeg = 0.0f;
  float strokeWidth = 0.0f;
  std::uint32_t arcColor = 0;
  std::uint32_t assetId = 0;
  std::uint32_t tintColor = 0;
  std::uint32_t glyphRunId = 0;
  std::uint32_t textColor = 0;
  std::uint32_t scrimColor = 0;
  float scrimAlpha = 0.0f;
};

struct DrawCmd {
  PrimitiveKind kind = PrimitiveKind::Rect;
  Rect bounds;
  float radius = 0.0f;
  float opacity = 1.0f;
  std::uint32_t nodeId = 0;
  PrimitivePayload payload;
};

enum class DrawError {
  None,
  UnknownPrimitive,
  UnsupportedBaseline,
};

class Swapchain {
 public:
  virtual ~Swapchain() = default;
  virtual void submit(const DrawCmd& cmd) = 0;
};

class Renderer {
 public:
  explicit Renderer(DeviceCaps caps);

  DrawError draw(const std::vector<DrawCmd>& cmds, Swapchain& target);
  std::string lastError() const;

 private:
  DeviceCaps caps_;
  std::string lastError_;
};

// FR-TEXT-016 / AC-TEXT-045 (BUG-M3-FIDELITY-005 T11): per-pixel glyph
// coverage for one rasterized run, in bounds-local pixels. Row-major
// `alpha` (`width * height` floats, 0 = background, 1 = full ink) threads
// true per-pixel stem coverage (T12: analytic at each pixel center, with a
// soft edge falloff) out of `RasterizeShapedText` so window painters
// src-over blend per pixel instead of filling text bounds solid. Never
// throws.
struct CoverageMask {
  int width = 0;
  int height = 0;
  std::vector<float> alpha;
};

// FR-TEXT-016 / AC-TEXT-045 (BUG-M3-FIDELITY-005 T11, T12): rasterize a
// cached ShapedText run into a CoverageMask via slug_msdf pen offsets +
// per-pixel analytic stem coverage (CPU mirror; pure C++, zero JNI on the
// frame path per NFR-TEXT-001). The run resolves through the T5 gallery shaping cache
// (no synthesis: unknown/dangling ids fail closed with kMissingGlyph and
// paint nothing, extending the glyphRunId == 0 sentinel rule). The caller
// maps a failed Status onto DrawError::UnknownPrimitive. Never throws.
muse::text::StatusOr<CoverageMask> RasterizeShapedText(
    std::uint32_t run, const Rect& bounds, std::uint32_t color);

}  // namespace runtime
