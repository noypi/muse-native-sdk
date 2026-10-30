// T1 attach-surface tests — bug-m3-fidelity-009 (verify-only).
// Verifies the 006 repaint target this run sequences onto, and fails unless
// a windowed emission entry with the plan shape exists:
//   PresentGalleryWindow(const std::vector<runtime::DrawCmd>& windowCmds,
//                        int firstVisible, int lastVisible) -> PresenterError
// Hermetic: C++17 + committed runtime headers only; in-memory fakes, no GPU,
// no window, no network, no filesystem. Mirrors gallery_present_test.cpp
// conventions (anonymous namespace, RUN_CASE, main with printf).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/presenter.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

namespace {

runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

// 006 surface: exactly the 11 retained PrimitiveKind values exist.
static bool check_eleven_primitive_kinds() {
  const runtime::PrimitiveKind kinds[] = {
      runtime::PrimitiveKind::Rect,
      runtime::PrimitiveKind::RoundedRect,
      runtime::PrimitiveKind::Image,
      runtime::PrimitiveKind::Text,
      runtime::PrimitiveKind::StateLayer,
      runtime::PrimitiveKind::Ripple,
      runtime::PrimitiveKind::ElevationShadow,
      runtime::PrimitiveKind::CircleArc,
      runtime::PrimitiveKind::VectorIcon,
      runtime::PrimitiveKind::ShapedText,
      runtime::PrimitiveKind::OverlayScrim,
  };
  if (sizeof(kinds) / sizeof(kinds[0]) != 11) return false;
  for (const auto kind : kinds) {
    switch (kind) {
      case runtime::PrimitiveKind::Rect:
      case runtime::PrimitiveKind::RoundedRect:
      case runtime::PrimitiveKind::Image:
      case runtime::PrimitiveKind::Text:
      case runtime::PrimitiveKind::StateLayer:
      case runtime::PrimitiveKind::Ripple:
      case runtime::PrimitiveKind::ElevationShadow:
      case runtime::PrimitiveKind::CircleArc:
      case runtime::PrimitiveKind::VectorIcon:
      case runtime::PrimitiveKind::ShapedText:
      case runtime::PrimitiveKind::OverlayScrim:
        break;
      default:
        return false;
    }
  }
  return true;
}

// 006 surface: DrawCmd carries the retained payload (state/ripple/shadow/
// arc/asset/glyph/scrim fields) a windowed range needs.
static bool check_draw_cmd_carries_payload() {
  runtime::DrawCmd cmd;
  cmd.kind = runtime::PrimitiveKind::StateLayer;
  cmd.payload.stateColor = 0xFFA45067u;
  cmd.payload.stateOpacity = 0.9f;
  cmd.payload.rippleProgress = 0.5f;
  cmd.payload.elevationDp = 2.0f;
  cmd.payload.arcSweepDeg = 270.0f;
  cmd.payload.assetId = 7;
  cmd.payload.glyphRunId = 9;
  cmd.payload.scrimAlpha = 0.25f;
  return cmd.payload.stateColor == 0xFFA45067u &&
         cmd.payload.glyphRunId == 9;
}

// 006 surface: PresentGallery entry is resolvable from the gallery path
// (bad envelope fails closed: false + diagnostic, never blank).
static bool check_present_gallery_entry_resolvable() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  const char bad[12] = {'X', 'X', 'X', 'X', 1, 0, 0, 0, 0, 0, 0, 0};
  std::string error;
  if (runtime::PresentGallery(bad, sizeof(bad), renderer, target, &error)) {
    return false;
  }
  return !error.empty() && target.captured.empty();
}

// Windowed attach shape (plan T1 Interface: line): the scrolled-row emission
// path must resolve to a range-carrying entry. Fails (compile error = red)
// while 006's entry cannot carry [firstVisible, lastVisible].
static bool check_windowed_entry_carries_visible_range() {
  std::vector<runtime::DrawCmd> windowCmds;
  runtime::DrawCmd cmd;
  cmd.kind = runtime::PrimitiveKind::Rect;
  cmd.nodeId = 1;
  windowCmds.push_back(cmd);
  runtime::PresenterError err =
      runtime::PresentGalleryWindow(windowCmds, 0, 0);
  return err.code == runtime::PresenterCode::kOk ||
         err.code == runtime::PresenterCode::kPacingError;
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
  RUN_CASE(check_eleven_primitive_kinds);
  RUN_CASE(check_draw_cmd_carries_payload);
  RUN_CASE(check_present_gallery_entry_resolvable);
  RUN_CASE(check_windowed_entry_carries_visible_range);
  if (failures > 0) {
    std::printf("gallery_window_attach_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_window_attach_test: all passing\n");
  return 0;
}
