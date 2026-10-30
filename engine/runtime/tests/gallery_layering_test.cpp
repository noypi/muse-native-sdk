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
  if (failures > 0) {
    std::printf("gallery_layering_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_layering_test: all passing\n");
  return 0;
}
