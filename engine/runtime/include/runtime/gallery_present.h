#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "runtime/presenter.h"
#include "runtime/renderer.h"

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

// Realized window for the virtualized gallery scroll path (FR-LISTS-007 /
// AC-LISTS-033): the visible row range plus velocity-derived prefetch headroom.
// `firstVisible`/`lastVisible` are inclusive row indices clamped to
// `[0, rowCount)` while rows exist (RULE-RUNTIME-001: never blank); an empty
// list is encoded as `firstVisible == 0` with `lastVisible < 0` (no rows
// claimed). `prefetchAhead` is a non-negative row count added ahead of the
// scroll direction; the velocity-to-rows curve is monotonic (|v| / 500 px/s
// per row, capped at 32) and never moves the visible anchor.
struct RealizedWindow {
  int firstVisible = 0;
  int lastVisible = -1;
  int prefetchAhead = 0;
};

// Pure, never throws: map a scroll offset + viewport to the realized row
// window. Out-of-range offsets/viewports clamp into `[0, rowCount)`; fling
// velocity extends `prefetchAhead` without moving `firstVisible`.
RealizedWindow ComputeRealizedWindow(int scrollOffsetPx, int viewportPx,
                                     int rowCount,
                                     float velocityPxS) noexcept;

// Emit retained DrawCmds for exactly the realized window [firstVisible,
// lastVisible] through the 006 vocabulary (same row geometry as
// PresentGallery). The scrolled-row emission path binds windowed rows only,
// never the full list; CPU PaintComponent helpers stay as the pixel-parity
// reference. Never throws; an empty window emits nothing.
void EmitGalleryWindowCmds(const std::vector<GalleryRow>& rows,
                           const RealizedWindow& window,
                           std::vector<DrawCmd>& out);

// Windowed emission entry (FR-LISTS-007 / AC-LISTS-033): consumes 006's
// retained redirect for the scrolled-row path. The caller passes only the
// realized-window DrawCmds plus the visible range they cover; this entry
// carries no surface/swapchain, so with no surface it fails closed with
// kPacingError (+ diagnostic) and never presents. Never throws.
PresenterError PresentGalleryWindow(const std::vector<DrawCmd>& windowCmds,
                                    int firstVisible, int lastVisible);

// Presented-window sink for vector text (T10 F1 fix): emit the
// realized-window DrawCmds through Renderer draw into the window-attached
// Swapchain, with T6 RasterizeShapedText run per run in bounds. Caption
// Text runs carrying a real run id present as ShapedText so the retained
// Swapchain text output is the presented-window sink, not the CPU oracle.
// Returns true with *error untouched on success; false + a non-blank
// *error diagnostic with nothing presented on bad input, zero rows, or a
// non-None draw result (fail-closed, same error shape as PresentGallery).
// Keeps the glyphRunId == 0 fail-closed untouched. C++ only, zero JNI on
// the per-frame path; error == nullptr is tolerated on every leg.
//
// T11 keeps this 5-arg entry as the reference-viewport (kPresentH) lane so
// the committed T10 hermetic suite stays green unmodified; new callers pass
// the real viewport height through the 6-arg overload below.
bool PresentDeviceGalleryPage(const std::vector<GalleryRow>& rows, int page,
                              Renderer& renderer, Swapchain& target,
                              std::string* error);

// T11 page-slicing lane (FR-TEXT-016 / AC-TEXT-045, F1 fix): same contract
// as the 5-arg entry, but page range and emission slice use `viewportH`
// (the real window height, shared with the caller's pager) instead of the
// fixed reference viewport, and the slice paints at the viewport origin.
// `viewportH <= 0` fails closed like bad input. RULE-RUNTIME-001
// fail-closed, same error shape as PresentGallery.
bool PresentDeviceGalleryPage(const std::vector<GalleryRow>& rows, int page,
                              Renderer& renderer, Swapchain& target,
                              int viewportH, std::string* error);

// T11 coverage seam (FR-TEXT-016 / AC-TEXT-045): resolve a ShapeGalleryLabel
// handle to its shaped glyph data (glyph ids + advances) from the T5
// gallery shaping cache. Returns true with both outputs populated on a
// known handle; false on 0/unknown handles or null outputs (fail-closed,
// never synthesized). Never throws.
bool ResolveGalleryShapedRun(std::uint32_t handle,
                             std::vector<std::uint32_t>* glyphs,
                             std::vector<float>* advances);

}  // namespace runtime
