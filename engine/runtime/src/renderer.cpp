#include "runtime/renderer.h"

namespace runtime {

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
