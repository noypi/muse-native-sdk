#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace runtime {

class Renderer;
class Swapchain;

// One gallery row extracted from the built tree IR (Showcase root).
// Produced by the structural TSX lowerer, so rows carry only literal text
// and literal props — never evaluated expressions (no JS engine on device).
struct GalleryRow {
  std::string label;      // visible text for the row
  std::string comp;       // component type for inventory rows ("" for text)
  std::string desc;       // full "Type k=v ..." literal-prop description
  bool dark = false;      // dark-theme section row
  bool header = false;    // section header (primary bar)
};

// Parse the MUI1 tree asset (magic + u32LE version + u32LE length + JSON IR)
// and extract the ordered gallery rows from the first `src/Showcase.tsx`
// root (first root when absent). Returns the rows; empty with *error set on
// any failure (bad envelope, bad JSON shape, no rows). Never throws.
std::vector<GalleryRow> ParseGalleryRows(const void* data, std::size_t len,
                                         std::string* error);

// Rasterize one page of rows into an RGBA_8888 buffer (stride in pixels).
// Layout runs through LayoutEngine (column flex: padding + gap), so row
// geometry honors the in-repo flexbox-subset seam. Returns rows drawn;
// *error is set when nothing could be drawn. Never throws.
int RasterGalleryPage(const std::vector<GalleryRow>& rows, int page,
                      std::uint32_t* pixels, int stridePx, int width,
                      int height, std::string* error);

// Page count for the given viewport (same row metrics as RasterGalleryPage).
int GalleryPageCount(const std::vector<GalleryRow>& rows, int height);

// Retained entry point (FR-GRAPHICS-010 / AC-GRAPHICS-030): parse the MUI1
// envelope with the existing ParseGalleryRows gate, then emit every row as
// one or more DrawCmds from the 11-kind vocabulary through renderer.draw
// into target (no direct CPU raster fallback). Returns true on success with
// *error left untouched; false + a non-blank diagnostic (nothing recorded)
// on bad envelope, zero rows, or a non-None draw result. Never throws;
// error == nullptr is tolerated on every leg.
bool PresentGallery(const void* mui1Data, std::size_t len, Renderer& renderer,
                    Swapchain& target, std::string* error);

}  // namespace runtime
