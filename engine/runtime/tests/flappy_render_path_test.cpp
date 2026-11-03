// BUG-RUNTIME-012 removal-contract pins — per-app content rendering is
// deleted, so the former render-path suite (renderTree/TreeHandle scene
// rendering through the removed content module) is deleted — see the run
// contract for its lines. What remains pins the removal through the
// surviving generic seam: no content provider is registered, the presented
// scene tap refresh declines every byte sequence with the gallery fallback
// intact, and the gallery present path fails closed with a gallery
// diagnostic on non-envelope bytes (never a content scene).
// Binds ONLY to production Real headers; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no device, no network,
// no env/config reads, no filesystem, no live services. The swapchain is
// an in-memory fake. No GPU.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/content_registry.h"
#include "runtime/gallery_present.h"
#include "runtime/renderer.h"

// Real device-lane hit-test type (consume-only, relative include per
// gallery_layering_test.cpp — runtime/input_chain.h is not directly
// includable from tests).
#include "../../input/include/hit_test.h"

// Generic tap-tree seams (defined in src/input_chain.cpp, linked via the
// runtime lib); forward-declared here per the sibling-test convention.
void PopulateTapTreeFromLayout(HitTestTree& tree, float viewportW,
                               float viewportH);
void PopulateTapTreeFromLayout(HitTestTree& tree);
bool RefreshTapTreeFromPresentedScene(HitTestTree& tree, const void* data,
                                      std::size_t len, float viewportW,
                                      float viewportH);
std::int32_t ResolveTapTarget(const HitTestTree& tree, float x, float y);

namespace {

// In-process fake swapchain: records every submitted cmd. No GPU.
class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

runtime::Renderer test_renderer() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return runtime::Renderer(caps);
}

// Representative ex-content scene bytes: the legacy per-app scene format
// carries no MUI1 envelope, so the gallery path must fail closed on them.
const char* kExContentSceneBytes = "scene panel-a panel-b overlay hud";

}  // namespace

static bool check_harness_entry_reached() { return true; }

// Removal pin: no content provider answers the registry in a fresh
// process — the deleted module registers nothing.
static bool check_removed_module_registers_nothing() {
  if (GlobalContentRegistry().Resolve("flappy") != nullptr) {
    std::printf("  detail: removed content tag still registered\n");
    return false;
  }
  return true;
}

// Removal pin: the presented-scene refresh declines ex-content bytes and
// leaves the gallery fallback tree intact (fail-closed, never a phantom
// node). The stub ignores payload shape by design — no scene bytes can be
// recognized without the content module.
static bool check_refresh_declines_ex_content_bytes_fallback_intact() {
  try {
    HitTestTree tree;
    PopulateTapTreeFromLayout(tree);
    if (ResolveTapTarget(tree, 540.0f, 930.0f) != 20) {
      std::printf("  detail: fallback tree missing gallery scrim node 20\n");
      return false;
    }
    const std::string scene = kExContentSceneBytes;
    if (RefreshTapTreeFromPresentedScene(tree, scene.data(), scene.size(),
                                         0.0f, 0.0f)) {
      std::printf("  detail: ex-content bytes rebuilt the tap tree\n");
      return false;
    }
    if (ResolveTapTarget(tree, 540.0f, 930.0f) != 20) {
      std::printf("  detail: declined refresh lost the fallback tree\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: refresh seam must never throw\n");
    return false;
  }
  return true;
}

// Removal pin: the gallery present path fails closed with a gallery
// envelope diagnostic on ex-content bytes — no content-scene routing
// remains, never a silent success, never a throw.
static bool check_present_gallery_fail_closed_on_ex_content_bytes() {
  try {
    runtime::Renderer renderer = test_renderer();
    FakeSwapchain target;
    const std::string scene = kExContentSceneBytes;
    std::string error;
    const bool ok = runtime::PresentGallery(scene.data(), scene.size(),
                                            renderer, target, &error);
    if (ok) {
      std::printf("  detail: ex-content bytes presented as a scene\n");
      return false;
    }
    if (error.empty()) {
      std::printf("  detail: ex-content bytes left no diagnostic\n");
      return false;
    }
    if (!target.captured.empty()) {
      std::printf("  detail: failed present drained cmds\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: present seam must never throw\n");
    return false;
  }
  return true;
}

#define RUN_CASE(name)                                  \
  do {                                                  \
    if (!(name)()) {                                    \
      std::printf("FAIL %s\n", #name);                  \
      ++failures;                                       \
    } else {                                            \
      std::printf("PASS %s\n", #name);                  \
    }                                                   \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_removed_module_registers_nothing);
  RUN_CASE(check_refresh_declines_ex_content_bytes_fallback_intact);
  RUN_CASE(check_present_gallery_fail_closed_on_ex_content_bytes);
  if (failures > 0) {
    std::printf("flappy_render_path_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_render_path_test: all passing\n");
  return 0;
}
