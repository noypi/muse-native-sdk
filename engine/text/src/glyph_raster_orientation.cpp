// FR-TEXT-007 / AC-TEXT-013 + FR-TEXT-010 / AC-TEXT-047: CPU-raster
// glyph-upload Real adapter and orientation guard. The upload copies rows
// left-to-right (x=0 stays x=0); the guard replays the Real upload into a
// scratch buffer and verifies column order plus a strictly positive LTR
// advance. Fail-closed: invalid input returns without writing (upload) or
// false (guard); no substitution, no sign negation.

#include "muse/text/glyph_raster_orientation.h"

namespace muse::text {

void UploadGlyphBitmap(const std::uint8_t* src_rows, std::size_t width,
                       std::size_t height, std::size_t pitch,
                       std::uint8_t* dst) noexcept {
  if (src_rows == nullptr || dst == nullptr) return;
  if (width == 0 || height == 0 || pitch < width) return;
  for (std::size_t y = 0; y < height; ++y) {
    const std::uint8_t* src = src_rows + y * pitch;
    std::uint8_t* row = dst + y * width;
    for (std::size_t x = 0; x < width; ++x) row[x] = src[x];
  }
}

bool GlyphUploadPreservesAdvanceDirection(
    const std::uint8_t* src_rows, std::size_t width, std::size_t height,
    std::size_t pitch, float advance_px) noexcept {
  if (src_rows == nullptr) return false;
  if (width == 0 || height == 0 || pitch < width) return false;
  if (!(advance_px > 0.0f)) return false;  // zero/negative mirrors/collapses
  // Replay the Real upload into scratch (bounded: tests use tiny bitmaps;
  // production glyphs are small; fail-closed on absurd sizes).
  if (width > 256 || height > 256) return false;
  std::uint8_t dst[256 * 256];
  UploadGlyphBitmap(src_rows, width, height, pitch, dst);
  // Compare extreme-column means in src vs uploaded dst. Asymmetric wedges
  // (opaque x=0, clear x=w-1) must keep their ordering; a mirrored upload
  // flips it. Symmetric/uniform bitmaps carry no order signal: pass.
  std::uint64_t src_left = 0, src_right = 0, dst_left = 0, dst_right = 0;
  for (std::size_t y = 0; y < height; ++y) {
    src_left += src_rows[y * pitch + 0];
    src_right += src_rows[y * pitch + (width - 1)];
    dst_left += dst[y * width + 0];
    dst_right += dst[y * width + (width - 1)];
  }
  if (src_left == src_right) return true;
  const bool src_left_dominant = src_left > src_right;
  const bool dst_left_dominant = dst_left > dst_right;
  return src_left_dominant == dst_left_dominant;
}

}  // namespace muse::text
