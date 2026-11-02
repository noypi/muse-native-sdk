#pragma once

// FR-TEXT-007 / AC-TEXT-013: CPU-raster glyph-upload orientation guard.
// Device fact (BUG-TEXT-008 t5r3): shaping resolves (Roboto-Regular.ttf,
// drawn>0) but labels render X-mirrored ("m3-gallery" reads mirrored).
// Shaping exonerated; defect is a CPU-raster glyph-upload column-order flip.
// Text owns this orientation guard; graphics owns atlas allocation per the
// SPEC-TYPOGRAPHY-TEXT Interfaces table.

#include <cstddef>
#include <cstdint>

namespace muse::text {

// Real adapter: CPU-raster glyph upload. Copies src_rows to dst
// left-to-right, row by row, never mirrored. dst must hold w*h bytes.
// Never throws; no-ops on invalid input.
void UploadGlyphBitmap(const std::uint8_t* src_rows, std::size_t width,
                       std::size_t height, std::size_t pitch,
                       std::uint8_t* dst) noexcept;

// Guard pin: true iff the Real upload preserves advance direction —
// strictly positive (+x) LTR advance and the leftmost source column lands
// at the leftmost uploaded texel (an X-mirrored upload returns false).
// Never throws.
bool GlyphUploadPreservesAdvanceDirection(const std::uint8_t* src_rows,
                                          std::size_t width,
                                          std::size_t height,
                                          std::size_t pitch,
                                          float advance_px) noexcept;

}  // namespace muse::text
