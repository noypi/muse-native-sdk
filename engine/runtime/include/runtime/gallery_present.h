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

// Real-component page entry (FR-GRAPHICS-015 / AC-GRAPHICS-042,
// RULE-GRAPHICS-003): rows from ParseGalleryRows descriptors instantiate
// real components emitting only the retained 11-kind vocabulary via
// renderer.draw into target (no CPU oracle box/caption pixels). Returns true
// with *error untouched on success; false + a non-blank diagnostic (nothing
// recorded) on empty rows, out-of-range/negative page, shaping failure, or a
// non-None draw result. Never throws; error == nullptr is tolerated.
bool PresentRealGalleryPage(const std::vector<GalleryRow>& rows, int page,
                            Renderer& renderer, Swapchain& target,
                            std::string* error);

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

// BUG-INPUT-001 T4 (AC-M3-022 scrim clause): layering-pop feed. Same
// contract as the 6-arg entry, plus a trailing overlay count forwarded into
// `LayeringScene::overlayCount` (clamped to [0, 5] by the paint path). The
// 5-arg and 6-arg overloads keep full-stack behavior (overlayCount == 5).
bool PresentDeviceGalleryPage(const std::vector<GalleryRow>& rows, int page,
                              Renderer& renderer, Swapchain& target,
                              int viewportH, std::string* error,
                              int overlayCount);

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
  int overlayCount = 5;
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

// ---- Device-path pressed engagement (FR-GRAPHICS-015 P2 / AC-GRAPHICS-043)
// ----
//
// Slop-aware DOWN/MOVE trigger for the state-layer wash on the device path.
// The native tap chain claims only on the UP slop verdict, so no pressed
// state ever engages mid-hold and the wash has nothing to render. The
// device host closes that gap through this seam: on ACTION_DOWN it resolves
// the hit target and translates it to the pressed row's 1-based id (i + 1)
// via GalleryResolvePressedRow (DOWN y against the page's row layout,
// fail-closed to clear when the target/coordinate maps to no row), then
// calls GallerySetPressedNode(row) (provisional claim); on ACTION_MOVE it
// keeps the
// node while displacement stays within slop and calls
// GalleryClearPressedNode() past slop; on ACTION_UP / cancel it calls
// GalleryClearPressedNode(). The host must request a frame after engaging
// so the mid-hold repaint carries the wash. The present path
// (EmitGalleryWindowCmds) paints exactly one pressed wash overlay for the
// engaged node via the shared PaintStateWash resolver, staged like every
// other cmd (nothing committed on a diagnostic). Fail-closed: a negative or
// zero node id clears (never engages); an engaged id outside the presented
// window paints nothing; the default state is disengaged (-1). Never throws.
void GallerySetPressedNode(int nodeId) noexcept;
void GalleryClearPressedNode() noexcept;
int GalleryPressedNode() noexcept;

// DOWN-to-row translation for the pressed seam above: maps a DOWN
// y-coordinate (device/window px) geometrically against the row layout of
// `page` under `viewportH` to the pressed row's 1-based id (i + 1, the same
// numbering EmitGalleryWindowCmds matches the wash against). The structural
// tap-tree ids (root/scrim/button) are unrelated to row numbering and must
// never be stored as the pressed node. Returns -1 (fail-closed, caller
// clears) when the coordinate maps to no row: empty rows, bad viewport,
// out-of-range page, or a y outside every row band. Never throws.
int GalleryResolvePressedRow(float yDevicePx,
                              const std::vector<GalleryRow>& rows, int page,
                              int viewportH) noexcept;

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

// ---- T2 live probe text field (BUG-M3-FIDELITY-003 / FR-M3-006) ----
//
// Global-scope attach gate for the single instrumented probe page
// ("m3-text-probe", single-probe scope BQ1, API-29-only gate). Declared here
// (rather than in a narrower header) so the JNI bridge (FR-INPUT-011 lane),
// the JS-runtime seam (FR-RUNTIME-010, consumed not reimplemented), and the
// retained-kind override in FamilyKind all bind to one entry. Defined in
// `engine/runtime/src/js_runtime.cpp` (self-contained TU: links hermetic).
// Fail-closed: null/empty/unknown page ids and any missing collaborator
// yield false + a `muse_text` logcat reason, never a throw across JNI.
struct LiveTextFieldDeps {
  const void* editingEngine = nullptr;    // FR-TEXT-009 editing engine handle
  const void* insetFeed = nullptr;        // FR-LAYOUT-012 imeBottom feed handle
  const void* clipboard = nullptr;        // FR-TEXT-013 clipboard transport
  const void* inputConnection = nullptr;  // FR-TEXT-008 IME bridge handle
};

// Returns true iff the live field replaced the static box on the probe page
// and the retained-kind override is active. Never throws.
bool AttachLiveTextField(const char* pageId, const LiveTextFieldDeps& deps);

// Proposed observer for "retained-kind override active" (see contract T2):
// true once a probe-page attach has succeeded (idempotent, sticky until
// process exit). Never throws.
bool liveKindOverrideActive();

// ---- T3 composition device leg (BUG-M3-FIDELITY-003 / FR-M3-006 / AC-M3-011)
// ----
//
// Global-scope gate for the IME composition preview/commit/cancel leg on the
// single instrumented probe page ("m3-text-probe", API-29-only gate serial
// "127.0.0.1:6569"). Defined in `engine/runtime/src/js_runtime.cpp`
// (self-contained TU: links hermetic). Fail-fast null/empty/non-gate serial
// yields distinct nonzero codes, never throws; the gate serial drives
// open-probe -> tap-focus -> commit-text -> focus-check -> binding-check ->
// cancel-delete -> withdraw-back via tester-proved shell paths only (am
// start, input tap/text/keyevent, uiautomator focus check, dumpsys
// input_method binding fields, BACK keyevent for hide), with any failure
// echoing the full record to stdout AND appending failure.log under the
// explicit evidence dir ($MUSE_EVIDENCE_DIR when absolute, else
// /tmp/bug-m3-fidelity-003-evidence/T3). Device-log markers
// (log -t muse_text) bracket the leg start/steps/finish. Never throws.
int RunCompositionLeg(const char* serial);

// ---- T4 selection device leg (BUG-M3-FIDELITY-003 / FR-M3-006 / AC-M3-012)
// ----
//
// Global-scope gate for the caret/selection leg by touch, keyboard, and API
// on the single instrumented probe page ("m3-text-probe", API-29-only gate
// serial "127.0.0.1:6569"). Defined in `engine/runtime/src/js_runtime.cpp`
// (self-contained TU: links hermetic). Fail-fast null/empty/non-gate serial
// yields distinct nonzero codes, never throws, no adb on bad serials; the
// gate serial drives open-probe -> tap -> keyboard-type -> focus-check ->
// binding-check -> mutate -> binding-roundtrip via tester-proved shell paths
// only, with any failure echoing the full record to stdout AND appending
// failure.log under the explicit evidence dir ($MUSE_EVIDENCE_DIR when
// absolute, else /tmp/bug-m3-fidelity-003-evidence/T4). Device-log markers
// (log -t muse_text) bracket the leg start/steps/finish. Never throws.
int RunSelectionLeg(const char* serial);

// ---- T5 inset device leg (BUG-M3-FIDELITY-003 / FR-M3-006 / AC-M3-013)
// ----
//
// Global-scope gate for the animated inset lockstep leg on the single
// instrumented probe page ("m3-text-probe", API-29-only gate serial
// "127.0.0.1:6569"). Defined in `engine/runtime/src/js_runtime.cpp`
// (self-contained TU: links hermetic). Fail-fast null/empty/non-gate serial
// yields distinct nonzero codes, never throws, no adb on bad serials; the
// gate serial drives the per-frame evidence leg (tap-focus, dumpsys
// window/input_method sampling, screencap frames, BACK keyevent withdraw)
// via tester-proved shell paths only on the `m3-text-probe` gate, with any
// failure echoing the full record to stdout AND appending failure.log under
// the explicit evidence dir ($MUSE_EVIDENCE_DIR when absolute, else
// /tmp/bug-m3-fidelity-003-evidence/T5). Device-log markers
// (log -t muse_text) bracket the leg start/steps/finish. Never throws.
int RunInsetLeg(const char* serial);

// ---- T6 clipboard device leg (BUG-M3-FIDELITY-003 / FR-M3-006 / AC-M3-014)
// ----
//
// Global-scope gate for the platform clipboard cut/copy/paste/select-all leg
// (plain + rich) on the single instrumented probe page ("m3-text-probe",
// API-29-only gate serial "127.0.0.1:6569"). Defined in
// `engine/runtime/src/js_runtime.cpp` (self-contained TU: links hermetic).
// Fail-fast null/empty/non-gate serial yields distinct nonzero codes, never
// throws, no adb on bad serials; the gate serial drives open-probe -> tap ->
// type-plain -> focus-check -> binding-check -> copy/paste/cut keyevents via
// tester-proved shell paths only (the gate has no shell selection/clipboard
// transport), with any failure echoing the full record to stdout AND
// appending failure.log under the explicit evidence dir ($MUSE_EVIDENCE_DIR
// when absolute, else /tmp/bug-m3-fidelity-003-evidence/T6). Device-log
// markers (log -t muse_text) bracket the leg start/steps/finish. Never
// throws.
int RunClipboardLeg(const char* serial);

// ---- T8 IME/text/clipboard observation surface (BUG-M3-FIDELITY-003 /
// FR-M3-006 / AC-M3-011/012/014) ----
//
// JNI registration point for the in-app observation surface. The Java probe
// field (`IMEInterceptingEditText`, an observability-only EditText
// subclass) forwards every IME/text/clipboard event through
// `nativeOnIMEEvent` / `nativeOnTextUpdated`; the definitions in
// `engine/runtime/src/jni_bridge.cpp` form the logcat-relay sink: no heavy
// work in the callback (copy strings + return), each event emitting exactly
// one `muse_text` JSON logcat line (`__android_log_print(ANDROID_LOG_DEBUG,
// "muse_text", ...)` with a canonical monotonically increasing eventId,
// queryable via `adb logcat -s muse_text`) plus an atomic receive-counter.
// Fail-closed/never-throw across JNI: exception-check + early return on
// every entry; null/empty/rapid inputs never crash the process.
//
// `text` may carry a trailing metadata envelope (`\x01key=value;...`,
// produced by the Java envelope helpers for span-carrying and clipboard
// events); the sink strips it and re-emits `spanStart`/`spanEnd`/`mime`
// top-level, so the fixed 4-arg JNI signatures are never reshaped.
// Malformed envelopes fail closed (emitted as plain text). Never throws.

// Relays one IME/clipboard/caret event to the `muse_text` logcat stream.
// `eventType` is one of the closed 9-member set (composing, commit_text,
// finish_composing, delete, set_selection, key_event, editor_action,
// text_updated, clip_changed); `eventId` is the Java-side monotonic hint,
// canonicalized gap-free by the sink. Null pointers fail closed to empty.
void EmitMuseTextEvent(const char* eventType, const char* text,
                       int cursorPosition, long long eventId);

// Relays one TextWatcher content snapshot to the `muse_text` logcat stream
// (emitted as `type=text_updated` with the sink-owned canonical event id).
// Null fails closed to empty.
void EmitMuseTextUpdated(const char* currentText);

// Number of observation lines relayed since process start (counts both
// entries above). Backed by a relaxed atomic; safe to call from any thread.
long long MuseTextReceiveCount() noexcept;
