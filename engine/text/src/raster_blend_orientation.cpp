// FR-TEXT-007 / AC-TEXT-013 + FR-TEXT-010 / AC-TEXT-047: Rasterize->Blend
// seam Real adapter (BUG-TEXT-008 t5r5). Compares extreme-column means:
// asymmetric wedges (opaque x=0, clear x=w-1) must keep left-dominance from
// the coverage mask to the posted pixels; a mirrored stage flips it and
// fails. Fail-closed: invalid input returns false; no synthesis, no fill.

#include "muse/text/raster_blend_orientation.h"

namespace muse::text {

bool RasterPreservesGlyphColumnOrder(const float* alpha, std::size_t width,
                                     std::size_t height) noexcept {
  if (alpha == nullptr) return false;
  if (width == 0 || height == 0) return false;
  if (width > 1024 || height > 1024) return false;
  double left = 0.0, right = 0.0;
  for (std::size_t y = 0; y < height; ++y) {
    left += static_cast<double>(alpha[y * width + 0]);
    right += static_cast<double>(alpha[y * width + (width - 1)]);
  }
  if (left == right) return true;
  return left > right;
}

bool BlendPreservesMaskColumnOrder(const float* mask_alpha,
                                   std::size_t mask_w, std::size_t mask_h,
                                   const std::uint32_t* posted, int posted_w,
                                   int stride) noexcept {
  if (mask_alpha == nullptr || posted == nullptr) return false;
  if (mask_w == 0 || mask_h == 0) return false;
  if (posted_w <= 0 || stride <= 0) return false;
  if (mask_w > 1024 || mask_h > 1024) return false;
  if (static_cast<std::size_t>(posted_w) > 4096 ||
      static_cast<std::size_t>(stride) > 4096) {
    return false;
  }
  double mask_left = 0.0, mask_right = 0.0;
  for (std::size_t y = 0; y < mask_h; ++y) {
    mask_left += static_cast<double>(mask_alpha[y * mask_w + 0]);
    mask_right += static_cast<double>(mask_alpha[y * mask_w + (mask_w - 1)]);
  }
  if (mask_left == mask_right) return true;
  // Luminance of the posted extreme columns over the mask rows.
  auto luminance = [](std::uint32_t px) -> double {
    const double r = static_cast<double>(px & 0xFFu);
    const double g = static_cast<double>((px >> 8) & 0xFFu);
    const double b = static_cast<double>((px >> 16) & 0xFFu);
    return 0.299 * r + 0.587 * g + 0.114 * b;
  };
  double post_left = 0.0, post_right = 0.0;
  for (std::size_t y = 0; y < mask_h; ++y) {
    const std::size_t row = y * static_cast<std::size_t>(stride);
    post_left += luminance(posted[row + 0]);
    post_right += luminance(
        posted[row + static_cast<std::size_t>(posted_w) - 1]);
  }
  if (post_left == post_right) return false;
  const bool mask_left_dominant = mask_left > mask_right;
  const bool post_left_dominant = post_left > post_right;
  return mask_left_dominant == post_left_dominant;
}

}  // namespace muse::text
