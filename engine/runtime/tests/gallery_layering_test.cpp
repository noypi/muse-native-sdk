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
  if (failures > 0) {
    std::printf("gallery_layering_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_layering_test: all passing\n");
  return 0;
}
