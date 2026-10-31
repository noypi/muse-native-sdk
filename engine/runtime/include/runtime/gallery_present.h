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

// ---- Layering page (L2: BUG-M3-FIDELITY-008 / FR-M3-010 / AC-M3-022) ----
//
// Hermetic layering scene for RULE-M3-006: dimmed content plus the five
// overlay contents stacked on a single OverlayHost in submission order.
// The CPU-mock path (paintLayeringPage) mirrors the retained geometry so
// hermetic checks observe identical ordering and sheet placement.
struct LayeringScene {
  bool valid = false;
  float sheetY = 0.0f;
  std::uint32_t fullRelayouts = 0;
};

// Emit the layering scene through the retained path: one OverlayScrim
// (alpha 0.25) then the five overlay contents in submission order
// (Dialog, BottomSheet, Menu, SnackbarHost, Tooltip). Later submissions
// draw above earlier ones. Invalid scene (valid == false) is a no-paint
// with a non-blank diagnostic, never a crash; error == nullptr tolerated.
void paintLayeringPage(const LayeringScene& scene, Renderer& renderer,
                       Swapchain& target, std::string* error);

// Bind the sheet Y to the animated layout inset (FR-LAYOUT-012) per frame.
// Consumes the inset value in place (no one-off copy, no full-tree
// relayout: fullRelayouts untouched). Non-finite inset resolves to 0.
// Never throws.
void anchorSheetToImeInset(LayeringScene& scene, float insetPx);

// T3 feed entry (BUG-RUNTIME-004 / FR-LAYOUT-012): record one animated IME
// frame height (px) into the live `deps.insets.imeBottom` field the layering
// present path reads. Non-finite and negative heights resolve to zero
// (hidden keyboard). Never throws.
void SetPresentImeBottom(float imeBottomPx) noexcept;

// ---- Scrim-tap-dismiss (L3: BUG-M3-FIDELITY-008 / FR-M3-010 / AC-M3-022,
// AC-INPUT-020) ----
//
// Dismiss routing for the layering page only. Rides the real post-007 tap
// chain (arena/slop governs hit-testing); this entry consumes the already
// resolved scrim hit-target outcome, so drag-no-fire holds by construction.
struct DismissResult {
  std::string dismissed;  // "topmost" on dismiss, "none" otherwise
  int remaining = 0;      // overlay count after routing (decrements by one)
  std::string reason;     // "" on dismiss, "miss" | "drag" otherwise
};

// Route one resolved tap on the layering-page scrim hit-target. A tap
// inside the scrim dismisses exactly the topmost overlay once; a content
// tap is a mis-hit (no dismiss); a drag never fires dismiss (AC-INPUT-020).
// Non-dismissals resolve { dismissed: 'none', reason: 'miss' | 'drag' }.
// Never throws.
DismissResult onScrimTap(bool scrimHit, bool isDrag,
                         int overlayCount) noexcept;
// Plan T62 `Interface:` line (verbatim): maps a state token to its numeric
// M3 state-layer opacity (0.08 hover / 0.12 focus / 0.16 pressed / 0.38
// disabled baseline per D1). Unknown/empty input returns the 0.0f no-wash
// sentinel; nothing throws.
float StateLayerOpacity(const std::string& state);

// Raster/retained wash (T62 detail 2): src-over state-layer fill driven by
// `StateLayerOpacity`, primary-role wash, ripple-bounded for Ripple kind,
// theme-matched via token resolution (D4). Fail-closed input yields the base
// command untouched, so the row is kept as-is. Never throws.
DrawCmd PaintStateWash(const DrawCmd& base, const std::string& kind,
                       const std::string& state);

// CPU-oracle wash (T62 detail 3): mirrors the raster wash 1:1 so raster and
// retained commands agree. Same resolver, same src-over application, same
// fail-closed shape. Never throws.
DrawCmd CpuOraclePaintStateWash(const DrawCmd& base, const std::string& kind,
                                const std::string& state);

// Whitelist gate (T62 detail 1): true only for descriptor prop keys that
// survive into `desc`. `"state"` is whitelisted so the T61 literal reaches
// the paint path. Never throws.
bool IsPropKeyWhitelisted(const std::string& key);

// Retained row path (renderer lane): paints the wash for one parsed gallery
// state row behind the `IsPropKeyWhitelisted("state")` gate. Fail-closed:
// unwhitelisted or unknown state keeps the base command, never blank, never
// throws across JNI.
DrawCmd BuildRetainedGalleryRowCmd(const DrawCmd& base,
                                   const std::string& kind,
                                   const std::string& state);

// Oracle row path (presenter lane): paints the 1:1 oracle expectation for
// one parsed gallery state row behind the `IsPropKeyWhitelisted("state")`
// gate. Same fail-closed shape as the retained lane. Never throws.
DrawCmd BuildOracleGalleryRowCmd(const DrawCmd& base,
                                 const std::string& kind,
                                 const std::string& state);

}  // namespace runtime
