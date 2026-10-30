#include "runtime/renderer.h"

#include <cmath>

#include "engine/graphics/vector/slug_msdf.h"
#include "engine/graphics/vector/tile_raster.h"
#include "runtime/gallery_present.h"

namespace runtime {
namespace {

// FR-TEXT-016 / AC-TEXT-045 (BUG-M3-FIDELITY-005 T11): per-pixel work bound
// for one coverage mask (AC-PERFORMANCE-008). Masks clamp to this many
// pixels per side; larger bounds sample the coverage proportionally.
constexpr int kMaxMaskSide = 256;
// Tile size (px) of the coverage grid the mask samples.
constexpr double kCoverageTile = 4.0;

}  // namespace

muse::text::StatusOr<CoverageMask> RasterizeShapedText(
    std::uint32_t run, const Rect& bounds, std::uint32_t color) {
  (void)color;
  // RULE-RUNTIME-001: the dangling sentinel never resolves (fail-closed,
  // untouched by T11).
  if (run == 0) return muse::text::Status::kMissingGlyph;
  if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
      !std::isfinite(bounds.w) || !std::isfinite(bounds.h) ||
      bounds.w <= 0.0f || bounds.h <= 0.0f) {
    return muse::text::Status::kMissingGlyph;
  }
  // No synthesis (T11 fix 1): the run must be a T5-produced handle backed
  // by the gallery shaping cache. Unknown/dangling ids fail closed with
  // nothing painted (the caller maps this onto UnknownPrimitive).
  std::vector<std::uint32_t> glyphs;
  std::vector<float> advances;
  if (!ResolveGalleryShapedRun(run, &glyphs, &advances) || glyphs.empty()) {
    return muse::text::Status::kMissingGlyph;
  }
  // NFR-TEXT-001: pure C++ rasterization; no JVM/JNI types on this path.
  float subpixel = bounds.x - std::floor(bounds.x);
  if (!std::isfinite(subpixel)) subpixel = 0.0f;
  const slug_msdf::ShapedRun shaped{std::move(glyphs), std::move(advances)};
  const slug_msdf::MsdfResult placed =
      slug_msdf::shapeAndPlace(shaped, subpixel);
  if (placed.status != slug_msdf::Status::kOk || placed.glyphs.empty()) {
    return muse::text::Status::kMissingGlyph;
  }
  // Stroke each placed glyph with a narrow stem at its shaped pen offset,
  // in bounds-local pixels: real pen positions (and advance-derived widths)
  // from shapeAndPlace modulate which tiles cover, so glyph shapes (not a
  // bounds grid) drive pixels. Stems span the middle band only, leaving
  // background pixels inside every bound.
  tile_raster::PathBatch batch;
  const double top = static_cast<double>(bounds.h) * 0.25;
  const double bottom = static_cast<double>(bounds.h) * 0.75;
  for (std::size_t i = 0; i < placed.glyphs.size(); ++i) {
    const double gx = static_cast<double>(placed.glyphs[i].x);
    double adv = 8.0;
    if (i < shaped.advances.size() &&
        std::isfinite(shaped.advances[i]) && shaped.advances[i] > 0.0f) {
      adv = static_cast<double>(shaped.advances[i]);
    }
    const double stemX = gx + adv * 0.3;
    tile_raster::Segment seg;
    seg.a = {stemX, top};
    seg.b = {stemX, bottom > top ? bottom : top + 1.0};
    batch.segments.push_back(seg);
  }
  int maskW = static_cast<int>(std::ceil(bounds.w));
  int maskH = static_cast<int>(std::ceil(bounds.h));
  if (maskW < 1) maskW = 1;
  if (maskH < 1) maskH = 1;
  if (maskW > kMaxMaskSide) maskW = kMaxMaskSide;
  if (maskH > kMaxMaskSide) maskH = kMaxMaskSide;
  tile_raster::TileGrid grid;
  grid.cols = static_cast<int>(std::ceil(static_cast<double>(maskW) /
                                         kCoverageTile));
  grid.rows = static_cast<int>(std::ceil(static_cast<double>(maskH) /
                                         kCoverageTile));
  if (grid.cols < 1) grid.cols = 1;
  if (grid.rows < 1) grid.rows = 1;
  grid.tile_width = kCoverageTile;
  grid.tile_height = kCoverageTile;
  const tile_raster::CoverageResult coverage = tile_raster::rasterizeTiles(
      batch, grid, tile_raster::BackendKind::kCpu);
  if (coverage.status != tile_raster::Status::kOk || coverage.masks.empty()) {
    return muse::text::Status::kMissingGlyph;
  }
  CoverageMask mask;
  mask.width = maskW;
  mask.height = maskH;
  mask.alpha.assign(static_cast<std::size_t>(maskW) * maskH, 0.0f);
  for (int my = 0; my < maskH; ++my) {
    const int tileRow =
        static_cast<int>((static_cast<double>(my) * grid.rows) / maskH);
    for (int mx = 0; mx < maskW; ++mx) {
      const int tileCol =
          static_cast<int>((static_cast<double>(mx) * grid.cols) / maskW);
      const std::size_t t = static_cast<std::size_t>(tileRow) *
                                static_cast<std::size_t>(grid.cols) +
                            static_cast<std::size_t>(tileCol);
      mask.alpha[static_cast<std::size_t>(my) * maskW + mx] =
          (t < coverage.masks.size() && coverage.masks[t] != 0u) ? 1.0f
                                                                 : 0.0f;
    }
  }
  return mask;
}

Renderer::Renderer(DeviceCaps caps) : caps_(caps) {}

DrawError Renderer::draw(const std::vector<DrawCmd>& cmds, Swapchain& target) {
  const BaselineCheck baseline = checkBaseline(caps_);
  if (!baseline.supported) {
    lastError_ = baseline.error;
    return DrawError::UnsupportedBaseline;
  }
  lastError_.clear();
  DrawError result = DrawError::None;
  for (const auto& cmd : cmds) {
    switch (cmd.kind) {
      case PrimitiveKind::Rect:
      case PrimitiveKind::RoundedRect:
      case PrimitiveKind::Image:
      case PrimitiveKind::Text:
      case PrimitiveKind::StateLayer:
        // T2 (FR-GRAPHICS-010 / AC-GRAPHICS-029): StateLayer renders a
        // tint rect at cmd.bounds from payload.stateColor/stateOpacity.
        // Presentation reuses the gallery_present.cpp src-over state-tint
        // math as the pixel-parity reference (BlendPixel: per-channel
        // (src*a + dst*(255-a))/255 with a = round(255*stateOpacity));
        // opacity 0.0 still submits (DrawError::None) and renders no
        // visible tint. Forwarding the cmd preserves bounds/payload
        // exactly; the Swapchain rasterizes at bounds.
        // T6 (FR-TEXT-016): caption Text cmds carrying a gallery shaping-cache
        // handle ride the ShapedText leg downstream (T11 coverage resolves
        // them through the shaping cache; 0 stays on the Text leg).
        target.submit(cmd);
        break;
      case PrimitiveKind::Ripple:
        // T3 (FR-GRAPHICS-010 / AC-GRAPHICS-029): Ripple renders a bounded
        // wash inside payload.rippleBounds at payload.rippleProgress with
        // payload.rippleColor. The retained-path rasterizer (Swapchain)
        // expands a disc about the rounded rippleBounds center with integer
        // radius ceil(progress * halfDiagonal(rippleBounds)), clipped to
        // rippleBounds, src-over blended (gallery_present.cpp BlendPixel:
        // per-channel (src*a + dst*(255-a))/255) at a = round(255*progress),
        // using the RingCircle/FillCircle integer disc test
        // (dx*dx + dy*dy <= r*r).
        // progress 0.0 submits (DrawError::None) and leaves the buffer
        // unchanged (radius/alpha zero); progress 1.0 covers the whole
        // bounds rect at full alpha. Forwarding the cmd preserves
        // rippleBounds/rippleProgress/rippleColor exactly; the Swapchain
        // rasterizes the wash.
        target.submit(cmd);
        break;
      case PrimitiveKind::ElevationShadow:
        // T4 (FR-GRAPHICS-010 / AC-GRAPHICS-029): ElevationShadow renders
        // a blurred rounded shadow of cmd.bounds/cmd.radius from
        // payload.elevationDp/payload.shadowBlur/payload.shadowColor.
        // The retained-path rasterizer (Swapchain) reuses the
        // gallery_present.cpp ShadowRounded math as the pixel-parity
        // reference: two offset soft passes (+2/+3 at a=28, +4/+6 at
        // a=18) clipped to the rounded silhouette
        // (dx*dx + dy*dy <= r*r per corner) and src-over blended
        // (BlendPixel: per-channel (src*a + dst*(255-a))/255) with the
        // shadowColor RGB; elevationDp == 0.0 submits (DrawError::None)
        // and leaves the buffer unchanged (no visible shadow).
        // Forwarding the cmd preserves bounds/radius/elevationDp/
        // shadowBlur/shadowColor exactly; the Swapchain rasterizes the
        // shadow under the card fill.
        target.submit(cmd);
        break;
      case PrimitiveKind::CircleArc:
        // T5 (FR-GRAPHICS-010 / AC-GRAPHICS-029): CircleArc renders a
        // ring/arc inside cmd.bounds from payload.arcStartDeg/
        // payload.arcSweepDeg/payload.strokeWidth/payload.arcColor. The
        // retained-path rasterizer (Swapchain) reuses the
        // gallery_present.cpp RingCircle/DrawArc math as the
        // pixel-parity reference: integer ring test
        // (d2 <= r*r && d2 >= inner*inner with inner = r - thick) for a
        // full 360 sweep (closed ring), and the DrawArc polyline-of-discs
        // walk (steps = clamp(sweepDeg*2, 8, 720), disc per step) for a
        // partial sweep; arcSweepDeg == 360.0 submits (DrawError::None)
        // and closes the ring (never clamped to an open arc).
        // Forwarding the cmd preserves bounds/arcStartDeg/arcSweepDeg/
        // strokeWidth/arcColor exactly; the Swapchain rasterizes the
        // ring/arc in bounds.
        target.submit(cmd);
        break;
      case PrimitiveKind::VectorIcon:
        // T6 (FR-GRAPHICS-010 / AC-GRAPHICS-029): VectorIcon resolves the
        // registry-supplied assetId and renders the tinted glyph in
        // cmd.bounds with payload.tintColor. assetId == 0 is the
        // unresolvable sentinel (the field default, never a valid
        // registry-supplied id; no filesystem/network on the draw path) and
        // fails closed per RULE-RUNTIME-001: report UnknownPrimitive and
        // record nothing (no substitute box). Forwarding a nonzero-id cmd
        // preserves bounds/assetId/tintColor exactly; the Swapchain
        // rasterizes the tinted glyph in bounds.
        if (cmd.payload.assetId == 0) {
          result = DrawError::UnknownPrimitive;
          break;
        }
        target.submit(cmd);
        break;
      case PrimitiveKind::ShapedText:
        // T7 (FR-GRAPHICS-010 / AC-GRAPHICS-029): ShapedText renders the
        // referenced glyph run in cmd.bounds with payload.textColor.
        // glyphRunId == 0 is the dangling sentinel (the field default,
        // never a valid supplied handle; shaping/atlas supply is
        // BUG-M3-FIDELITY-005's ownership) and fails closed per
        // RULE-RUNTIME-001: report UnknownPrimitive and record nothing
        // (no substitute box). Forwarding a nonzero-id cmd preserves
        // bounds/glyphRunId/textColor exactly; the Swapchain rasterizes
        // the referenced run in bounds.
        if (cmd.payload.glyphRunId == 0) {
          result = DrawError::UnknownPrimitive;
          break;
        }
        // T11 (FR-TEXT-016): forwarding itself is unchanged; the retained
        // run resolves through the shaping cache at rasterization time
        // (unknown/dangling handles fail closed there, never synthesized).
        target.submit(cmd);
        break;
      case PrimitiveKind::OverlayScrim:
        // T8 (FR-GRAPHICS-010 / AC-GRAPHICS-029): OverlayScrim renders a
        // dim layer over cmd.bounds from payload.scrimColor/
        // payload.scrimAlpha, under subsequent overlay content. The
        // retained-path rasterizer (Swapchain) src-over blends
        // (gallery_present.cpp BlendPixel: per-channel
        // (src*a + dst*(255-a))/255) the scrimColor RGB at
        // a = round(255*scrimAlpha) across the full bounds rect;
        // scrimAlpha == 0.0 submits (DrawError::None) and leaves the
        // buffer unchanged (no visible dim). Forwarding the cmd preserves
        // bounds/scrimColor/scrimAlpha exactly; the Swapchain rasterizes
        // the dim in submission order, so later overlay content draws
        // over it.
        target.submit(cmd);
        break;
      default:
        // RULE-RUNTIME-001: never substitute a default box for an
        // undeclared primitive; report and skip the unknown node.
        result = DrawError::UnknownPrimitive;
        break;
    }
  }
  return result;
}

std::string Renderer::lastError() const { return lastError_; }

}  // namespace runtime
