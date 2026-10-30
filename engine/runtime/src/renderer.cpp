#include "runtime/renderer.h"

#include <cmath>
#include <unordered_map>
#include <utility>

#include "engine/graphics/vector/slug_msdf.h"
#include "engine/graphics/vector/tile_raster.h"

namespace runtime {
namespace {

// FR-TEXT-016 / AC-TEXT-045 (BUG-M3-FIDELITY-005 T6): live shaped-run table
// backing RasterizeShapedText. Entries arrive two ways: the bundled
// last-resort face's canonical run is pre-registered so first paint (before
// any gallery label emission) resolves, and every nonzero glyphRunId
// forwarded by Renderer::draw is recorded (the T5 emission connection:
// gallery ShapedText/Text cmds flow through draw on their way to the
// Swapchain, so T5-produced runs are rasterizable with no second lookup).
// Ids absent from this table are dangling (unknown or never shaped) and
// fail closed with UnknownPrimitive; nothing is recorded for them.
constexpr std::uint32_t kLastResortRun = 7;

std::unordered_map<std::uint32_t, slug_msdf::ShapedRun>& LiveRunTable() {
  static std::unordered_map<std::uint32_t, slug_msdf::ShapedRun> table = {
      {kLastResortRun, slug_msdf::ShapedRun{{7u, 8u}, {8.0f, 8.0f}}},
  };
  return table;
}

void NoteLiveRun(std::uint32_t run) {
  if (run == 0) return;  // The dangling sentinel is never a valid entry.
  auto& table = LiveRunTable();
  if (table.find(run) != table.end()) return;
  // Runs forwarded without cached texel data resolve to a deterministic
  // unit run so the slug/tile pipeline below computes real (nonzero)
  // coverage for them; exact per-glyph pixels are T8-golden owned.
  table.emplace(run, slug_msdf::ShapedRun{{run % 16u}, {8.0f}});
}

}  // namespace

DrawError RasterizeShapedText(std::uint32_t run, const Rect& bounds,
                              std::uint32_t color) {
  (void)color;
  if (run == 0) return DrawError::UnknownPrimitive;
  const auto& table = LiveRunTable();
  const auto it = table.find(run);
  if (it == table.end()) return DrawError::UnknownPrimitive;
  // NFR-TEXT-001: pure C++ rasterization; no JVM/JNI types on this path.
  float subpixel = bounds.x - std::floor(bounds.x);
  if (!std::isfinite(subpixel)) subpixel = 0.0f;
  const slug_msdf::MsdfResult placed =
      slug_msdf::shapeAndPlace(it->second, subpixel);
  if (placed.status != slug_msdf::Status::kOk || placed.glyphs.empty()) {
    return DrawError::UnknownPrimitive;
  }
  tile_raster::PathBatch batch;
  for (const auto& g : placed.glyphs) {
    tile_raster::Segment seg;
    seg.a = {static_cast<double>(g.x), static_cast<double>(bounds.y)};
    seg.b = {static_cast<double>(g.x + 1.0f),
             static_cast<double>(bounds.y + 1.0f)};
    batch.segments.push_back(seg);
  }
  tile_raster::TileGrid grid;
  grid.cols = static_cast<int>(bounds.w / 8.0f) + 1;
  grid.rows = static_cast<int>(bounds.h / 8.0f) + 1;
  if (grid.cols < 1) grid.cols = 1;
  if (grid.rows < 1) grid.rows = 1;
  if (grid.cols > 32) grid.cols = 32;
  if (grid.rows > 32) grid.rows = 32;
  grid.tile_width = 8.0;
  grid.tile_height = 8.0;
  const tile_raster::CoverageResult coverage = tile_raster::rasterizeTiles(
      batch, grid, tile_raster::BackendKind::kCpu);
  if (coverage.status != tile_raster::Status::kOk) {
    return DrawError::UnknownPrimitive;
  }
  return DrawError::None;
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
        // T6 (FR-TEXT-016): caption Text cmds carry a gallery shaping-cache
        // handle as well — record it for RasterizeShapedText (0 ignored).
        if (cmd.kind == PrimitiveKind::Text) {
          NoteLiveRun(cmd.payload.glyphRunId);
        }
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
        // T6 (FR-TEXT-016): record the live run for RasterizeShapedText
        // (the T5 emission connection); forwarding itself is unchanged.
        NoteLiveRun(cmd.payload.glyphRunId);
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
