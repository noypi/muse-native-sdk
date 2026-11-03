#pragma once

// FR-TEXT-007 / AC-TEXT-013 + FR-TEXT-010 / AC-TEXT-047: Rasterize->Blend
// seam column-order guards (BUG-TEXT-008 t5r5 TRUE mirror site).
// Device fact (device-trace-mirror.md ROOT_CAUSE_FOUND): the executed lane is
// PresentDeviceGalleryPage -> ShapedText -> RasterizeShapedText -> BlendMask,
// and per-glyph texels are X-mirrored on device while advances stay +x LTR.
// The atlas-upload guard (glyph_raster_orientation.h) is pixel-dead on this
// lane — do NOT touch it. These pins bind the executed seam:
// RasterPreservesGlyphColumnOrder covers BuildGlyphOutline (:111) /
// RasterizeOutlineBlock (renderer.cpp:408-498); BlendPreservesMaskColumnOrder
// covers WindowSwapchain::BlendMask (jni_bridge.cpp:646-690).

#include <cstddef>
#include <cstdint>

namespace muse::text {

// Real-adapter guards: true iff the leftmost coverage column stays leftmost
// through the stage. Asymmetric wedges (ink x=0, clear x=w-1, the "3" vs "E"
// signature) must keep their ordering; a per-glyph X-flip returns false
// (fail closed). Symmetric/uniform inputs carry no order signal: pass.
// RasterPreservesGlyphColumnOrder is pinned by its unit tests and the
// Blend-half guard; the RasterizeOutlineBlock raster-half guard compares
// segment-mean against the coverage centroid instead (bbox edge columns
// are vacuous for inset stems like '6'/'2').
// Never throws; false on invalid input. No substitution, no fallback change.
bool RasterPreservesGlyphColumnOrder(const float* alpha, std::size_t width,
                                     std::size_t height) noexcept;
bool BlendPreservesMaskColumnOrder(const float* mask_alpha,
                                   std::size_t mask_w, std::size_t mask_h,
                                   const std::uint32_t* posted, int posted_w,
                                   int stride) noexcept;

}  // namespace muse::text
