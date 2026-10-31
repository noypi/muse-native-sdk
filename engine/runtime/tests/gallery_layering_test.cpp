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

// BUG-INPUT-001 T2: real tap-tree shape via the consume-only engine/input
// unit. Relative include on purpose (test-only): the gallery_layering_test
// target carries no ../input/include dir (touch_dispatch_test has its own),
// and the tester cannot edit build configuration — a one-line
// `target_include_directories(gallery_layering_test PRIVATE
// ../input/include)` lets this become `#include "hit_test.h"` plus
// `#include "runtime/input_chain.h"` (reconciliation, tracked in T2.md).
// Declarations below mirror runtime/input_chain.h verbatim (plan T2
// `Interface:` line); link against the static runtime, which compiles
// input_chain.cpp + MUSE_INPUT_SOURCES in.
#include "../../input/include/hit_test.h"
#include "../../input/include/gesture_arena.h"
#include "../../input/include/input/event_model.h"
void PopulateTapTreeFromLayout(HitTestTree& tree);
int32_t ResolveTapTarget(const HitTestTree& tree, float x, float y);
// BUG-INPUT-001 T3: ACTION_UP routing halves, verbatim from
// runtime/input_chain.h (test-only forward declarations, same pattern as the
// T2 pair above; the static runtime links input_chain.cpp in).
void RecordTapDown(int32_t pointerId, float x, float y);
ArenaAction ClaimTap(GestureArena& arena, int32_t pointerId, int32_t targetId);
bool FireTapHandler(int32_t nodeId);
bool RepaintWithinOneFrame(int32_t nodeId);
// BUG-INPUT-001 T3: layering-page predicate + pager owned by the present
// path (defined in gallery_present.cpp; forward-declared here exactly as in
// jni_bridge.cpp so the dispatch slice stays page-scoped in the drive).
namespace runtime {
std::vector<int> PageStarts(const std::vector<GalleryRow>& rows, int height);
bool IsLayeringPageSlice(const std::vector<GalleryRow>& rows, int first,
                         int last);
}  // namespace runtime

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

// ---- BUG-INPUT-001 T1 dispatch→dismiss regression (batch 1, test-only)
// ----
//
// Routes through the existing device-path sequence
// ResolveTapTarget → ClaimTap → FireTapHandler → onScrimTap: scrimHit is
// the already-resolved scrim hit-target outcome for the fired target,
// isDrag is the already-computed slop verdict, overlayCount is the live
// count from the paint path's store (plan A3). Page-scoped to the layering
// page only (plan Risks: unscoped routing must never fire elsewhere).
//
// Hermetic: C++17 + committed runtime headers only (gallery_present.h);
// no device, no network, no env/config reads, no filesystem, no live
// services. onScrimTap is pure except its dismiss printf (stdout, no fs).
// No input_chain.h include on purpose: the scrim hit-target node (T2) and
// the ACTION_UP routing call (T3) do not exist yet, so these cases drive
// onScrimTap directly with the resolved sequence values and pin the
// routing gap explicitly in the last case (expected RED until T2+T3 land).

// T1 case: scrim tap dismisses exactly the topmost overlay once.
bool check_scrim_tap_dismisses_topmost_once() {
  const runtime::DismissResult got = runtime::onScrimTap(true, false, 5);
  if (got.dismissed != "topmost") return false;
  if (got.remaining != 4) return false;
  if (!got.reason.empty()) return false;
  return true;
}

// T1 case: content tap is a mis-hit — no dismiss, count unchanged.
bool check_content_tap_is_miss_count_unchanged() {
  const runtime::DismissResult got = runtime::onScrimTap(false, false, 5);
  if (got.dismissed != "none") return false;
  if (got.remaining != 5) return false;
  if (got.reason != "miss") return false;
  return true;
}

// T1 case: drag (past slop) never dismisses, even over the scrim.
bool check_drag_never_dismisses_scrim_or_content() {
  const runtime::DismissResult overScrim =
      runtime::onScrimTap(true, true, 5);
  if (overScrim.dismissed != "none") return false;
  if (overScrim.remaining != 5) return false;
  if (overScrim.reason != "drag") return false;
  const runtime::DismissResult overContent =
      runtime::onScrimTap(false, true, 5);
  if (overContent.dismissed != "none") return false;
  if (overContent.remaining != 5) return false;
  if (overContent.reason != "drag") return false;
  return true;
}

// T1 boundary: empty stack — scrim tap resolves miss, never negative.
bool check_empty_stack_scrim_tap_is_miss() {
  const runtime::DismissResult got = runtime::onScrimTap(true, false, 0);
  if (got.dismissed != "none") return false;
  if (got.remaining != 0) return false;
  if (got.reason != "miss") return false;
  return true;
}

// T1 boundary: negative count clamps to 0 with a miss, never throws.
bool check_negative_count_clamps_to_miss() {
  const runtime::DismissResult got = runtime::onScrimTap(true, false, -3);
  if (got.dismissed != "none") return false;
  if (got.remaining != 0) return false;
  if (got.reason != "miss") return false;
  return true;
}

// T1 case: overlay-count transition — one scrim tap pops exactly one;
// a following content tap leaves the decremented count unchanged.
bool check_overlay_count_decrements_exactly_once() {
  const runtime::DismissResult first =
      runtime::onScrimTap(true, false, 3);
  if (first.dismissed != "topmost" || first.remaining != 2) return false;
  const runtime::DismissResult second =
      runtime::onScrimTap(false, false, first.remaining);
  if (second.dismissed != "none") return false;
  if (second.remaining != 2) return false;
  if (second.reason != "miss") return false;
  return true;
}

// T3 routing case (tester-owned drive, wired post-T2/T3): a dispatched
// scrim tap on the layering page reaches onScrimTap through the device
// dispatch sequence — ResolveTapTarget → RecordTapDown/ClaimTap slop
// verdict → FireTapHandler → layering-page scope (PageStarts +
// IsLayeringPageSlice) → live count (GalleryHandle.layeringOverlayCount
// mirror, opens at 5) → single pop + repaint on dismissed=topmost — and a
// non-layering-page tap never routes there. Mirrors the ACTION_UP branch in
// jni_bridge.cpp (fired target 20 == scrim node; fired taps carry
// isDrag=false by the ClaimTap slop verdict; drags fail the claim).
// Reconciliation, not weakening: the drive follows the routing, never the
// DismissResult expectations above.
bool check_dispatched_scrim_tap_routes_to_dismiss_layering_only() {
  // 1. Fired target: the scrim probe resolves to the T2 scrim node 20
  // (scrimHit = fired == 20), distinct from content 21 and root 10.
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  const int32_t firedTarget = ResolveTapTarget(tree, 5.0f, 5.0f);
  if (firedTarget != 20) return false;
  const bool scrimHit = (firedTarget == 20);

  // 2. Slop verdict: a tap gesture (down+up at the probe) claims, so the
  // fired tap carries isDrag=false by construction.
  constexpr int32_t kTapPointer = 7001;
  RecordTapDown(kTapPointer, 5.0f, 5.0f);
  GestureArena tapArena;
  input::AxisState tapAxes;
  tapAxes.x = 5.0f;
  tapAxes.y = 5.0f;
  tapAxes.pressure = 1.0f;
  tapArena.AddPointer(input::NormalizeMotion(kTapPointer,
                                             input::ToolType::Touch, 0u,
                                             tapAxes, 0u,
                                             input::SourceClass::Touch),
                      tree);
  const ArenaAction tapClaim = ClaimTap(tapArena, kTapPointer, firedTarget);
  if (!tapClaim.claimed) return false;
  const bool isDrag = !tapClaim.claimed;
  if (isDrag) return false;
  if (!FireTapHandler(firedTarget)) return false;

  // 3. Layering-page scope: the page-1 slice routes, the page-0 (flat-row)
  // slice never routes (device cur-page windowing via PageStarts).
  const std::vector<runtime::GalleryRow> rows =
      TwoPageRowsWithLayeringSecond();
  if (runtime::GalleryPageCount(rows, kLayeringViewportH) != 2) return false;
  const std::vector<int> starts =
      runtime::PageStarts(rows, kLayeringViewportH);
  if (starts.size() != 3) return false;
  if (!runtime::IsLayeringPageSlice(rows, starts[1], starts[2] - 1)) {
    return false;
  }
  if (runtime::IsLayeringPageSlice(rows, starts[0], starts[1] - 1)) {
    return false;  // non-layering page must never route
  }

  // 4. Live count: mirrors GalleryHandle.layeringOverlayCount (opens at 5);
  // the dispatched scrim tap pops exactly the topmost once.
  int liveOverlayCount = 5;
  const runtime::DismissResult dismiss =
      runtime::onScrimTap(scrimHit, isDrag, liveOverlayCount);
  if (dismiss.dismissed != "topmost") return false;
  if (dismiss.remaining != liveOverlayCount - 1) return false;
  if (!dismiss.reason.empty()) return false;
  liveOverlayCount = dismiss.remaining;  // single pop applied under mu

  // Single pop + repaint on dismissed=topmost.
  if (!RepaintWithinOneFrame(firedTarget)) return false;

  // 5. Content tap is a miss: button probe still resolves to content 21
  // (never the scrim node), count unchanged.
  const int32_t contentTarget = ResolveTapTarget(tree, 150.0f, 140.0f);
  if (contentTarget == 20) return false;
  const runtime::DismissResult contentMiss =
      runtime::onScrimTap(false, false, liveOverlayCount);
  if (contentMiss.dismissed != "none") return false;
  if (contentMiss.remaining != liveOverlayCount) return false;
  if (contentMiss.reason != "miss") return false;

  // 6. Drag never dismisses: down at the probe but up far past slop fails
  // the ClaimTap verdict (unclaimed), so the device takes the isDrag path
  // even over the scrim.
  constexpr int32_t kDragPointer = 7002;
  RecordTapDown(kDragPointer, 5.0f, 5.0f);
  GestureArena dragArena;
  input::AxisState dragAxes;
  dragAxes.x = 400.0f;
  dragAxes.y = 700.0f;
  dragAxes.pressure = 1.0f;
  dragArena.AddPointer(input::NormalizeMotion(kDragPointer,
                                              input::ToolType::Touch, 0u,
                                              dragAxes, 0u,
                                              input::SourceClass::Touch),
                       tree);
  if (ClaimTap(dragArena, kDragPointer, firedTarget).claimed) return false;
  const runtime::DismissResult dragOverScrim =
      runtime::onScrimTap(true, true, liveOverlayCount);
  if (dragOverScrim.dismissed != "none") return false;
  if (dragOverScrim.remaining != liveOverlayCount) return false;
  if (dragOverScrim.reason != "drag") return false;
  return true;
}

// ---- BUG-INPUT-001 T2 scrim hit-target (batch 1, test-only) ----
//
// ASSUMPTION S1 (scrim probe): the laid-out layering scrim geometry covers
// the page background outside content/button bounds; probe (5,5) is inside
// the root viewport (400x800) but outside button node 21
// ((50,100)-(250,180)), so post-T2 it must resolve to the scrim hit-target
// node — distinct from content node 21 and root 10, never -1. If the
// coder's layering-derived scrim bounds exclude (5,5), update this probe
// (reconciliation, not weakening) and re-publish the contract.
//
// Hermetic: real PopulateTapTreeFromLayout + ResolveTapTarget over an
// in-process HitTestTree; no device, no env/config, no live services.

// T2 case (AC-INPUT-020 preserved): button bounds still resolve to content
// node 21 after the scrim node lands.
bool check_tap_tree_button_still_resolves_to_content_node() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  return ResolveTapTarget(tree, 150.0f, 140.0f) == 21;
}

// T2 case (AC-INPUT-020 preserved): far-outside taps still fail closed.
bool check_tap_tree_outside_still_fails_closed() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  return ResolveTapTarget(tree, 10000.0f, 10000.0f) == -1;
}

// T2 case (AC-M3-022 scrim clause; expected RED pre-T2): scrim geometry
// resolves to the scrim hit-target node, distinct from content node 21
// and root 10. Pre-T2 the tree holds only root 10 + button 21, so the
// scrim probe resolves to root 10 and this case FAILS by design.
bool check_scrim_geometry_resolves_to_scrim_node() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  const int32_t got = ResolveTapTarget(tree, 5.0f, 5.0f);
  if (got == -1 || got == 10 || got == 21) return false;
  return true;
}

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
  RUN_CASE(check_scrim_tap_dismisses_topmost_once);
  RUN_CASE(check_content_tap_is_miss_count_unchanged);
  RUN_CASE(check_drag_never_dismisses_scrim_or_content);
  RUN_CASE(check_empty_stack_scrim_tap_is_miss);
  RUN_CASE(check_negative_count_clamps_to_miss);
  RUN_CASE(check_overlay_count_decrements_exactly_once);
  RUN_CASE(check_dispatched_scrim_tap_routes_to_dismiss_layering_only);
  RUN_CASE(check_tap_tree_button_still_resolves_to_content_node);
  RUN_CASE(check_tap_tree_outside_still_fails_closed);
  RUN_CASE(check_scrim_geometry_resolves_to_scrim_node);
  if (failures > 0) {
    std::printf("gallery_layering_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_layering_test: all passing\n");
  return 0;
}
