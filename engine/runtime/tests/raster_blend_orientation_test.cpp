// T6c/T6d — Rasterize->Blend seam column-order pins (BUG-TEXT-008 t5r5).
//
// Device fact (device-trace-mirror.md ROOT_CAUSE_FOUND): the executed lane is
// PresentDeviceGalleryPage -> ShapedText -> RasterizeShapedText -> BlendMask,
// and per-glyph texels are X-mirrored on device (engine-sha=69b49c9) while
// advances stay +x LTR ("m3-gallery" reads "mE-gallery"). T6a pins a dead
// upload path and T6b pins advance sign, so neither covers this seam. The
// flip localizes inside per-glyph coverage generation or the final blit:
// BuildGlyphOutline / RasterizeOutlineBlock (renderer.cpp:111-498) or
// BlendMask (jni_bridge.cpp:646-690).
//
// These pins are hermetic and Real-only (P12.6): C++17, no framework, no
// device, no font files, no downloads, no config. They bind to the planned
// coder-owned seam guards:
//
//   bool muse::text::RasterPreservesGlyphColumnOrder(
//       const float* alpha, size_t width, size_t height) noexcept;
//   bool muse::text::BlendPreservesMaskColumnOrder(
//       const float* mask_alpha, size_t mask_w, size_t mask_h,
//       const uint32_t* posted, int posted_w, int stride) noexcept;
//
// The header does not exist yet — the RED below is a missing-behavior
// compile failure, not a malformed test. The stub-backed wedge model IS the
// contract suite on host (no Fake, Real-only).
//
// T6c (this file, check_raster_preserves_column_order): an asymmetric wedge
//   CoverageMask (ink at column x=0, clear at x=w-1 — the shape that
//   distinguishes "3" from "E") must come out of the raster left-to-right;
//   a per-glyph X-flip in BuildGlyphOutline/RasterizeOutlineBlock FAILs.
// T6d (this file, check_blend_preserves_column_order): the same wedge,
//   posted through the BlendMask proportional texel mapping, must keep its
//   left column inked on screen; a per-glyph X-flip in BlendMask FAILs.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "muse/text/raster_blend_orientation.h"

namespace {

// Asymmetric wedge: column x=0 fully inked, column x=w-1 fully clear.
// An X-flip anywhere on the seam turns this wedge into its mirror (the
// "3" -> "E" defect signature on t5r5).
static bool check_raster_preserves_column_order() {
  constexpr size_t kW = 8, kH = 8;
  std::vector<float> alpha(kW * kH, 0.0f);
  for (size_t y = 0; y < kH; ++y) alpha[y * kW + 0] = 1.0f;
  return muse::text::RasterPreservesGlyphColumnOrder(alpha.data(), kW, kH);
}

// The posted row keeps the wedge's left column inked through the BlendMask
// proportional mapping (mask.width onto the scaled destination rect).
// A blend-side X-flip moves the ink to the right column and FAILs.
static bool check_blend_preserves_column_order() {
  constexpr size_t kW = 8, kH = 8;
  std::vector<float> mask(kW * kH, 0.0f);
  for (size_t y = 0; y < kH; ++y) mask[y * kW + 0] = 1.0f;
  std::vector<std::uint32_t> posted(kW * kH, 0xFF000000u);
  for (size_t y = 0; y < kH; ++y)
    posted[y * kW + 0] = 0xFFFFFFFFu;  // ink lands left, mirroring BlendMask
  return muse::text::BlendPreservesMaskColumnOrder(
      mask.data(), kW, kH, posted.data(), static_cast<int>(kW),
      static_cast<int>(kW));
}

}  // namespace

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
  RUN_CASE(check_raster_preserves_column_order);
  RUN_CASE(check_blend_preserves_column_order);
  if (failures > 0) {
    std::printf("raster_blend_orientation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("raster_blend_orientation_test: all passing\n");
  return 0;
}
