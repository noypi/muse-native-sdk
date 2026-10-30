// Batch 1 layering painter — FR-M3-010 / AC-M3-022 / RULE-M3-006 (L2).
// Hermetic: C++17 + committed runtime headers only; in-memory FakeSwapchain
// sink, no ANativeWindow, no GPU, no network, no env/config reads, no
// filesystem, no live services. Mirrors gallery_present_test.cpp conventions
// (anonymous namespace, RUN_CASE, printf main).
//
// Binds to the L2 `Interface:` lines. Parameter-passing below is the test's
// proposal and the L2 contract records the reconciliation rule (update call
// sites, not expectations) if the coder names them differently:
//
// ASSUMPTION P1 (retained-path signature): `paintLayeringPage` emits through
// the PresentGallery retained convention —
//   `void paintLayeringPage(const LayeringScene& scene, Renderer& renderer,
//                           Swapchain& target, std::string* error)` —
// and `anchorSheetToImeInset` mutates the scene in place —
//   `void anchorSheetToImeInset(LayeringScene& scene, float insetPx)`.
// ASSUMPTION P2 (scene shape): `runtime::LayeringScene` is
// default-constructible with public `bool valid` (default false), public
// `float sheetY` (default 0), public `std::uint32_t fullRelayouts` (default
// 0), and a fixed five-entry submission list in Interface order
// (Dialog, BottomSheet, Menu, SnackbarHost, Tooltip). An invalid scene
// (`valid == false`) is a no-paint with a non-blank diagnostic, never a
// crash. `fullRelayouts` is the no-full-tree-relayout marker the probe
// asserts stays zero across the stepped inset sequence.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

// In-process fake swapchain: records every submitted cmd. No GPU.
class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

runtime::LayeringScene valid_scene() {
  runtime::LayeringScene scene;
  scene.valid = true;
  return scene;
}

// L2 case: the layering emitter submits the scrim first with quarter alpha.
bool check_scrim_submitted_first_with_quarter_alpha() {
  runtime::LayeringScene scene = valid_scene();
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::paintLayeringPage(scene, renderer, target, &error);
  if (!error.empty()) return false;
  if (target.captured.empty()) return false;
  const runtime::DrawCmd& first = target.captured.front();
  if (first.kind != runtime::PrimitiveKind::OverlayScrim) return false;
  return std::fabs(first.payload.scrimAlpha - 0.25f) < 1e-6f;
}

// L2 case: exactly 1 scrim + 5 overlay contents in submission order.
bool check_five_overlay_contents_follow_in_submission_order() {
  runtime::LayeringScene scene = valid_scene();
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::paintLayeringPage(scene, renderer, target, &error);
  if (!error.empty()) return false;
  if (target.captured.size() != 6) return false;
  if (target.captured[0].kind != runtime::PrimitiveKind::OverlayScrim) {
    return false;
  }
  // Submission order is observable as strictly increasing node ids after
  // the scrim (later submission draws above).
  for (std::size_t i = 1; i < target.captured.size(); ++i) {
    if (target.captured[i].kind == runtime::PrimitiveKind::OverlayScrim) {
      return false;
    }
    if (i > 1 &&
        target.captured[i].nodeId <= target.captured[i - 1].nodeId) {
      return false;
    }
  }
  return true;
}

// L2 error shape: invalid scene is a no-paint with diagnostic, never a crash.
bool check_invalid_scene_is_no_paint_with_diagnostic() {
  runtime::LayeringScene scene;  // valid == false
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::paintLayeringPage(scene, renderer, target, &error);
  return target.captured.empty() && !error.empty();
}

// L2 error shape: a null error out-param never throws across the boundary.
bool check_null_error_ptr_never_throws() {
  runtime::LayeringScene scene = valid_scene();
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  runtime::paintLayeringPage(scene, renderer, target, nullptr);
  if (target.captured.size() != 6) return false;
  runtime::LayeringScene bad;
  FakeSwapchain rejected;
  runtime::paintLayeringPage(bad, renderer, rejected, nullptr);
  return rejected.captured.empty();
}

// L2 case: the sheet tracks a stepped IME inset with no jump (>1px fails)
// and stays bound to the animated value every frame.
bool check_sheet_tracks_stepped_inset_without_jump() {
  runtime::LayeringScene scene = valid_scene();
  float prev = scene.sheetY;
  for (float inset = 0.0f; inset <= 240.0f; inset += 0.5f) {
    runtime::anchorSheetToImeInset(scene, inset);
    if (std::fabs(scene.sheetY - inset) > 1e-3f) return false;
    if (std::fabs(scene.sheetY - prev) > 1.0f) return false;
    prev = scene.sheetY;
  }
  return true;
}

// L2 case: the stepped probe fires no full-tree relayout marker.
bool check_stepped_inset_fires_no_full_tree_relayout() {
  runtime::LayeringScene scene = valid_scene();
  for (float inset = 0.0f; inset <= 240.0f; inset += 5.0f) {
    runtime::anchorSheetToImeInset(scene, inset);
  }
  return scene.fullRelayouts == 0u;
}

// L2 boundary: NaN inset never crashes, never relayouts, resolves to 0.
bool check_nan_inset_never_throws_and_never_relayouts() {
  runtime::LayeringScene scene = valid_scene();
  runtime::anchorSheetToImeInset(scene, std::nanf(""));
  if (scene.sheetY != 0.0f) return false;
  return scene.fullRelayouts == 0u;
}

// ---- L8a dispatch branch (BUG-M3-FIDELITY-008 Batch 5, owned half only)
// ----
//
// The device render path is RenderLocked (jni_bridge.cpp) ->
// PresentDeviceGalleryPage 6-arg (gallery_present.cpp:2260) ->
// EmitGalleryWindowCmds (flat rows). paintLayeringPage has zero
// device-path callers: the L8a branch routes the layering-page slice to
// paintLayeringPage instead of the flat-row EmitTextRowCmds /
// EmitComponentRowCmds path, with no pager/slice change. L8b touch
// routing (filed as BUG-INPUT-001) and L9 IME plumbing (filed as
// BUG-RUNTIME-004) are explicitly out of this batch — no tests for them.
//
// ASSUMPTION D1 (trigger): the branch keys on the requested page's slice
// being the L1 layering scene rows — the "Layering page" title plus the
// dimmed-content, IME-probe, and five overlay member rows below, which
// mirror the structural-lowerer output for Showcase.tsx LayeringPage
// (ParseGalleryRows literal labels + kPropKeys descs). If the coder keys
// the predicate differently, update these rows, not the shape
// expectations (reconciliation, not dispute).
// ASSUMPTION D2 (packing): at viewportH 1600 (avail 1444 after
// kHeaderH/kFooterH/kPad) 18 text rows + 1 Button row fill page 0
// (1152 + 104 + 19 gaps = 1408) and the 9 layering rows open page 1, so
// pages == 2. If row metrics change, this guard fails loudly (no vacuous
// pass); adjust the filler count, not the dispatch shape.
// ASSUMPTION D3 (device page index): on-device the layering page displays

// L1 scene rows in Showcase order: title, dimmed light/dark content, IME
// probe editor, then the five overlay members in submission order.
std::vector<runtime::GalleryRow> LayeringSceneRows() {
  std::vector<runtime::GalleryRow> rows;
  const char* texts[] = {"Layering page", "Layering content dimmed light",
                         "Layering content dimmed dark"};
  for (const char* label : texts) {
    runtime::GalleryRow row;
    row.label = label;
    rows.push_back(row);
  }
  struct Comp {
    const char* comp;
    const char* label;
    const char* desc;
  };
  const Comp comps[] = {
      {"TextInput", "Layering IME probe",
       "TextInput placeholder=Layering IME probe"},
      {"Dialog", "Layering dialog", "Dialog title=Layering dialog"},
      {"BottomSheet", "BottomSheet", "BottomSheet"},
      {"Menu", "Menu", "Menu"},
      {"SnackbarHost", "Layering snackbar",
       "SnackbarHost message=Layering snackbar"},
      {"Tooltip", "Layering tooltip", "Tooltip text=Layering tooltip"},
  };
  for (const Comp& c : comps) {
    runtime::GalleryRow row;
    row.comp = c.comp;
    row.label = c.label;
    row.desc = c.desc;
    rows.push_back(row);
  }
  return rows;
}

// Two-page gallery: 18 flat text rows + 1 flat Button row (page 0), then
// the 9 L1 layering rows (page 1 at viewportH 1600).
std::vector<runtime::GalleryRow> TwoPageRowsWithLayeringSecond() {
  std::vector<runtime::GalleryRow> rows;
  for (int i = 0; i < 18; ++i) {
    runtime::GalleryRow row;
    row.label = "Filler row " + std::to_string(i);
    rows.push_back(row);
  }
  runtime::GalleryRow button;
  button.comp = "Button";
  button.label = "Filler button";
  button.desc = "Button label=Filler button";
  rows.push_back(button);
  const std::vector<runtime::GalleryRow> scene = LayeringSceneRows();
  rows.insert(rows.end(), scene.begin(), scene.end());
  return rows;
}

constexpr int kLayeringViewportH = 1600;

// L8a: the layering-page slice dispatches to paintLayeringPage — exactly
// the composited shape (full-bleed scrim alpha 0.25 first, then 5 opaque
// contents, nodeIds 1..6 in submission order), with none of the flat-row
// markers (ElevationShadow card chrome, Text/ShapedText captions). The
// flat path can never produce this shape: it emits 2 cmds per text row
// and 4 per component row with per-row-repeated nodeIds, and its
// Dialog/Menu/Tooltip OverlayScrims are small row-local rects, never a
// full-bleed first cmd.
bool check_layering_slice_routes_to_paint_layering_page_shape() {
  const std::vector<runtime::GalleryRow> rows =
      TwoPageRowsWithLayeringSecond();
  if (runtime::GalleryPageCount(rows, kLayeringViewportH) != 2) {
    return false;  // packing guard (D2): layering slice must be page 1
  }
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  if (!runtime::PresentDeviceGalleryPage(rows, 1, renderer, target,
                                         kLayeringViewportH, &error)) {
    return false;
  }
  if (!error.empty()) return false;
  if (target.captured.size() != 6) return false;
  const runtime::DrawCmd& scrim = target.captured.front();
  if (scrim.kind != runtime::PrimitiveKind::OverlayScrim) return false;
  if (std::fabs(scrim.payload.scrimAlpha - 0.25f) > 1e-6f) return false;
  if (scrim.bounds.w != 720.0f || scrim.bounds.h != 1600.0f) return false;
  for (std::size_t i = 0; i < target.captured.size(); ++i) {
    if (target.captured[i].nodeId != static_cast<std::uint32_t>(i + 1)) {
      return false;
    }
    const runtime::PrimitiveKind kind = target.captured[i].kind;
    if (kind == runtime::PrimitiveKind::Text ||
        kind == runtime::PrimitiveKind::ShapedText ||
        kind == runtime::PrimitiveKind::ElevationShadow) {
      return false;  // flat-row marker leaked into the composited present
    }
  }
  for (std::size_t i = 1; i < target.captured.size(); ++i) {
    if (target.captured[i].kind ==
        runtime::PrimitiveKind::OverlayScrim) {
      return false;
    }
    if (target.captured[i].payload.stateOpacity != 1.0f) return false;
  }
  return true;
}

// L8a guard: non-layering pages keep the flat-row emission — caption
// vector text plus card chrome are present, and the composited 6-shape
// never appears. Pins the "all other pages unchanged / no pager change"
// half of the Interface.
bool check_non_layering_page_keeps_flat_row_emission() {
  const std::vector<runtime::GalleryRow> rows =
      TwoPageRowsWithLayeringSecond();
  if (runtime::GalleryPageCount(rows, kLayeringViewportH) != 2) {
    return false;
  }
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  if (!runtime::PresentDeviceGalleryPage(rows, 0, renderer, target,
                                         kLayeringViewportH, &error)) {
    return false;
  }
  if (!error.empty() || target.captured.empty()) return false;
  bool shapedCaption = false;
  bool cardChrome = false;
  for (const runtime::DrawCmd& cmd : target.captured) {
    if (cmd.kind == runtime::PrimitiveKind::ShapedText &&
        cmd.payload.glyphRunId != 0) {
      shapedCaption = true;
    }
    if (cmd.kind == runtime::PrimitiveKind::ElevationShadow) {
      cardChrome = true;
    }
  }
  if (!shapedCaption || !cardChrome) return false;
  if (target.captured.size() == 6 &&
      target.captured.front().kind ==
          runtime::PrimitiveKind::OverlayScrim) {
    return false;  // the branch swallowed a non-layering page
  }
  return true;
}

// L8a boundary: a null error out-param never throws across the dispatch
// (both legs tolerate error == nullptr, mirroring the L2 painter case).
bool check_layering_dispatch_null_error_ptr_never_throws() {
  const std::vector<runtime::GalleryRow> rows =
      TwoPageRowsWithLayeringSecond();
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  if (!runtime::PresentDeviceGalleryPage(rows, 1, renderer, target,
                                         kLayeringViewportH, nullptr)) {
    return false;
  }
  return target.captured.size() == 6;
}

// L8a predicate-fix (round-3, tester-owned RED): the on-device 18/19 viewport
// shows ~6 rows with no title, but IsLayeringPageSlice requires the windowed
// slice to be exactly 9 rows (title + 2 dimmed + 6 comps), so the branch
// never fires on-device. This case feeds the REAL PageStarts-windowed subset
// the device path supplies: 7 filler text rows + 1 flat Button row, then the
// 9 L1 layering rows, at viewportH 1070. PageStarts packs page 0 as
// [0,11) (fillers + button + title + 2 dimmed) and page 1 as [11,17) —
// exactly the 6 comp rows (IME probe, Dialog, BottomSheet, Menu,
// SnackbarHost, Tooltip) with NO title. That windowed subset must still
// route to paintLayeringPage (composited 6-shape). Pre-fix RED: the
// predicate's `last - first + 1 != 9` rejects the 6-row window and the flat
// emitter runs instead.
std::vector<runtime::GalleryRow> WindowedSubsetRows() {
  std::vector<runtime::GalleryRow> rows;
  for (int i = 0; i < 7; ++i) {
    runtime::GalleryRow row;
    row.label = "Subset filler " + std::to_string(i);
    rows.push_back(row);
  }
  runtime::GalleryRow button;
  button.comp = "Button";
  button.label = "Subset button";
  button.desc = "Button label=Subset button";
  rows.push_back(button);
  const std::vector<runtime::GalleryRow> scene = LayeringSceneRows();
  rows.insert(rows.end(), scene.begin(), scene.end());
  return rows;
}

constexpr int kSubsetViewportH = 1070;

bool check_viewport_windowed_layering_subset_routes_to_paint_shape() {
  const std::vector<runtime::GalleryRow> rows = WindowedSubsetRows();
  if (runtime::GalleryPageCount(rows, kSubsetViewportH) != 2) {
    return false;  // packing guard: the 6-comp subset must be page 1
  }
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  if (!runtime::PresentDeviceGalleryPage(rows, 1, renderer, target,
                                         kSubsetViewportH, &error)) {
    return false;
  }
  if (!error.empty()) return false;
  if (target.captured.size() != 6) return false;
  const runtime::DrawCmd& scrim = target.captured.front();
  if (scrim.kind != runtime::PrimitiveKind::OverlayScrim) return false;
  if (std::fabs(scrim.payload.scrimAlpha - 0.25f) > 1e-6f) return false;
  if (scrim.bounds.w != 720.0f || scrim.bounds.h != 1600.0f) return false;
  for (std::size_t i = 0; i < target.captured.size(); ++i) {
    if (target.captured[i].nodeId != static_cast<std::uint32_t>(i + 1)) {
      return false;
    }
    const runtime::PrimitiveKind kind = target.captured[i].kind;
    if (kind == runtime::PrimitiveKind::Text ||
        kind == runtime::PrimitiveKind::ShapedText ||
        kind == runtime::PrimitiveKind::ElevationShadow) {
      return false;  // flat-row marker leaked into the composited present
    }
  }
  for (std::size_t i = 1; i < target.captured.size(); ++i) {
    if (target.captured[i].kind ==
        runtime::PrimitiveKind::OverlayScrim) {
      return false;
    }
    if (target.captured[i].payload.stateOpacity != 1.0f) return false;
  }
  return true;
}

// L8a predicate-fix (round-6 device-shape, tester-owned RED): verbatim
// on-device slice p17 [107,112] (tester round-6, 127.0.0.1:6569):
//   comps=`{OverlayHost|TextInput|OverlayHost|Surface|Dialog|BottomSheet}`
//   labels=`{OverlayHost|Layering IME probe|OverlayHost|Surface tone=scrim|Layering dialog|BottomSheet}`
// The on-device layering slice carries WRAPPER rows (OverlayHost x2, Surface
// scrim, TextInput IME probe) the 9-row L1 scene contract never named — that
// is why no offset ever aligned. This case feeds EXACTLY that 6-row slice as
// page 1 (9 fillers + 1 Button on page 0, 6 verbatim rows on page 1 at
// viewportH 1000: avail 844; page 0 used 752, +112 > 844 breaks at 10;
// page 1 used 826 <= 844) and requires routing to paintLayeringPage
// (composited 6-shape). Pre-fix RED: the predicate knows only the
// wrapper-less 9-row scene, so the flat emitter runs instead.
std::vector<runtime::GalleryRow> DeviceShapeSliceRows() {
  std::vector<runtime::GalleryRow> rows;
  for (int i = 0; i < 9; ++i) {
    runtime::GalleryRow row;
    row.label = "Device filler " + std::to_string(i);
    rows.push_back(row);
  }
  runtime::GalleryRow button;
  button.comp = "Button";
  button.label = "Device button";
  button.desc = "Button label=Device button";
  rows.push_back(button);
  struct Comp {
    const char* comp;
    const char* label;
    const char* desc;
  };
  const Comp dev[] = {
      {"OverlayHost", "OverlayHost", "OverlayHost"},
      {"TextInput", "Layering IME probe",
       "TextInput placeholder=Layering IME probe"},
      {"OverlayHost", "OverlayHost", "OverlayHost"},
      {"Surface", "Surface tone=scrim", "Surface tone=scrim"},
      {"Dialog", "Layering dialog", "Dialog title=Layering dialog"},
      {"BottomSheet", "BottomSheet", "BottomSheet"},
  };
  for (const Comp& c : dev) {
    runtime::GalleryRow row;
    row.comp = c.comp;
    row.label = c.label;
    row.desc = c.desc;
    rows.push_back(row);
  }
  return rows;
}

constexpr int kDeviceSliceViewportH = 1000;

bool check_device_shape_slice_routes_to_paint_shape() {
  const std::vector<runtime::GalleryRow> rows = DeviceShapeSliceRows();
  if (runtime::GalleryPageCount(rows, kDeviceSliceViewportH) != 2) {
    return false;  // packing guard: the 6-row verbatim slice must be page 1
  }
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  if (!runtime::PresentDeviceGalleryPage(rows, 1, renderer, target,
                                         kDeviceSliceViewportH, &error)) {
    return false;
  }
  if (!error.empty()) return false;
  if (target.captured.size() != 6) return false;
  const runtime::DrawCmd& scrim = target.captured.front();
  if (scrim.kind != runtime::PrimitiveKind::OverlayScrim) return false;
  if (std::fabs(scrim.payload.scrimAlpha - 0.25f) > 1e-6f) return false;
  if (scrim.bounds.w != 720.0f || scrim.bounds.h != 1600.0f) return false;
  for (std::size_t i = 0; i < target.captured.size(); ++i) {
    if (target.captured[i].nodeId != static_cast<std::uint32_t>(i + 1)) {
      return false;
    }
    const runtime::PrimitiveKind kind = target.captured[i].kind;
    if (kind == runtime::PrimitiveKind::Text ||
        kind == runtime::PrimitiveKind::ShapedText ||
        kind == runtime::PrimitiveKind::ElevationShadow) {
      return false;  // flat-row marker leaked into the composited present
    }
  }
  for (std::size_t i = 1; i < target.captured.size(); ++i) {
    if (target.captured[i].kind ==
        runtime::PrimitiveKind::OverlayScrim) {
      return false;
    }
    if (target.captured[i].payload.stateOpacity != 1.0f) return false;
  }
  return true;
}

// L8b texture half (tester round-7, tester-owned RED): the composited
// 6-shape from paintLayeringPage carries per-overlay identity for all 5
// contents (Dialog/BottomSheet/Menu/SnackbarHost/Tooltip).
//
// TEXT-CHANNEL ABSENCE (read from the painter's current content emission
// in gallery_present.cpp paintLayeringPage): the five contents are emitted
// as Rect/RoundedRect cmds only — no Text/ShapedText cmd, glyphRunId == 0
// and textColor == 0 on every content — so there is no text channel in
// which labels could ride. Identity is therefore asserted via distinct
// (stateColor, elevationDp) per overlay content: all five pairs must be
// pairwise distinct. Current painter state RED: every content carries the
// same fill (stateColor == kWhite, elevationDp == 0.0f), so the distinct
// check below fails on the first comparison.
bool check_layering_paint_emits_per_overlay_identity() {
  runtime::LayeringScene scene = valid_scene();
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  runtime::paintLayeringPage(scene, renderer, target, &error);
  if (!error.empty()) return false;
  if (target.captured.size() != 6) return false;
  // Contents are indices 1..5 (index 0 is the scrim).
  for (std::size_t i = 1; i < target.captured.size(); ++i) {
    const runtime::DrawCmd& cmd = target.captured[i];
    if (cmd.kind == runtime::PrimitiveKind::Text ||
        cmd.kind == runtime::PrimitiveKind::ShapedText) {
      return false;  // identity must not ride a text channel that the
                     // composited shape does not emit
    }
    if (cmd.payload.glyphRunId != 0 || cmd.payload.textColor != 0) {
      return false;  // no text channel on overlay contents
    }
  }
  for (std::size_t i = 1; i < target.captured.size(); ++i) {
    for (std::size_t j = i + 1; j < target.captured.size(); ++j) {
      const bool sameColor = target.captured[i].payload.stateColor ==
                             target.captured[j].payload.stateColor;
      const bool sameElev =
          std::fabs(target.captured[i].payload.elevationDp -
                    target.captured[j].payload.elevationDp) < 1e-6f;
      if (sameColor && sameElev) return false;  // overlays i,j indistinguishable
    }
  }
  return true;
}

#if !defined(_WIN32)
// Capture stdout emitted during fn() into out (in-process pipe + dup2;
// hermetic: no filesystem, no env, no network).
void capture_stdout_during(void (*fn)(), std::string* out) {
  std::fflush(stdout);
  const int saved = dup(STDOUT_FILENO);
  int fds[2];
  if (pipe(fds) != 0) {
    if (saved >= 0) close(saved);
    return;
  }
  dup2(fds[1], STDOUT_FILENO);
  fn();
  std::fflush(stdout);
  dup2(saved, STDOUT_FILENO);
  close(saved);
  close(fds[1]);
  char buf[4096];
  ssize_t n = 0;
  while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
    out->append(buf, static_cast<std::size_t>(n));
  }
  close(fds[0]);
}

namespace {
struct ConfirmProbe {
  static const std::vector<runtime::GalleryRow>* rows;
  static runtime::Renderer* renderer;
  static FakeSwapchain* target;
  static int viewportH;
  static void run() {
    std::string error;
    runtime::PresentDeviceGalleryPage(*rows, 1, *renderer, *target,
                                      viewportH, &error);
  }
};
const std::vector<runtime::GalleryRow>* ConfirmProbe::rows = nullptr;
runtime::Renderer* ConfirmProbe::renderer = nullptr;
FakeSwapchain* ConfirmProbe::target = nullptr;
int ConfirmProbe::viewportH = 0;
}  // namespace
#endif

// L8b confirm-log half (tester round-7, tester-owned RED): the dispatch
// path emits a confirm line naming the routed slice —
// `[layering] paint layering page=<p> shapes=6` — distinguishable from the
// pre-existing slice diagnostic (`[layering] slice page=... comps={...}
// labels={...}`). Current dispatch state RED: PresentDeviceGalleryPage
// emits only the slice diagnostic and paintLayeringPage prints nothing, so
// no `paint layering page` confirm line exists on stdout.
bool check_layering_dispatch_emits_confirm_log() {
#if defined(_WIN32)
  return false;  // stdout-capture probe is POSIX-only; coder adds the log
                 // and this case is verified on the POSIX host
#else
  const std::vector<runtime::GalleryRow> rows = DeviceShapeSliceRows();
  if (runtime::GalleryPageCount(rows, kDeviceSliceViewportH) != 2) {
    return false;  // packing guard: verbatim device slice must be page 1
  }
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  ConfirmProbe::rows = &rows;
  ConfirmProbe::renderer = &renderer;
  ConfirmProbe::target = &target;
  ConfirmProbe::viewportH = kDeviceSliceViewportH;
  std::string captured;
  capture_stdout_during(&ConfirmProbe::run, &captured);
  ConfirmProbe::rows = nullptr;
  if (target.captured.size() != 6) return false;  // must route to paint shape
  // The confirm names the routed page and shape count and is not the slice
  // diagnostic (which says "slice", never "paint layering page").
  if (captured.find("paint layering page=1") == std::string::npos) {
    return false;
  }
  if (captured.find("shapes=6") == std::string::npos) return false;
  return true;
#endif
}

}  // namespace

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_scrim_submitted_first_with_quarter_alpha);
  RUN_CASE(check_five_overlay_contents_follow_in_submission_order);
  RUN_CASE(check_invalid_scene_is_no_paint_with_diagnostic);
  RUN_CASE(check_null_error_ptr_never_throws);
  RUN_CASE(check_sheet_tracks_stepped_inset_without_jump);
  RUN_CASE(check_stepped_inset_fires_no_full_tree_relayout);
  RUN_CASE(check_nan_inset_never_throws_and_never_relayouts);
  RUN_CASE(check_layering_slice_routes_to_paint_layering_page_shape);
  RUN_CASE(check_non_layering_page_keeps_flat_row_emission);
  RUN_CASE(check_layering_dispatch_null_error_ptr_never_throws);
  RUN_CASE(check_viewport_windowed_layering_subset_routes_to_paint_shape);
  RUN_CASE(check_device_shape_slice_routes_to_paint_shape);
  RUN_CASE(check_layering_paint_emits_per_overlay_identity);
  RUN_CASE(check_layering_dispatch_emits_confirm_log);
  if (failures > 0) {
    std::printf("gallery_layering_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_layering_test: all passing\n");
  return 0;
}
