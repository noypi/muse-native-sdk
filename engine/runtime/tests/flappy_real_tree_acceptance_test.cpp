// BUG-RUNTIME-012 removal-contract pins — the former acceptance regression
// (the REAL built tree presenting as the game scene through the removed
// content module) is deleted with the module — see the run contract for
// its lines: register/render/present/gate/broken-scene/log cases. What
// remains pins the gallery fallback through the surviving generic seam:
// every byte sequence declines the presented-scene refresh with the
// fallback tap tree intact, and no content provider is registered.
// Tester-owned (was integration/acceptance; the real-tree file it read is
// gone with the module, so the surviving contract is hermetic).
// Binds ONLY to production Real headers; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no device, no network,
// no env/config reads, no filesystem, no live services.
#include <cstdio>
#include <string>

#include "runtime/content_registry.h"

// Real device-lane hit-test type (consume-only, relative include per
// flappy_tap_tree_test.cpp convention — runtime/input_chain.h is not
// directly includable from tests: it pulls gesture_arena.h/hit_test.h
// which are PRIVATE to the runtime lib).
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

// Representative ex-content scene bytes: with the module removed, no byte
// sequence may rebuild the tap tree.
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

// Security pin F3a (AC-INPUT-020 / AC-RUNTIME-038/039): gallery bytes keep
// the gallery fallback tap tree — refresh declines and node 20 still fires.
static bool check_gallery_bytes_keep_fallback_tree() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  if (ResolveTapTarget(tree, 540.0f, 930.0f) != 20) {
    std::printf("  detail: fallback tree missing gallery scrim node 20\n");
    return false;
  }
  const char gallery[] = "GalleryPage gallery-rows=41 TopAppBar-less";
  if (RefreshTapTreeFromPresentedScene(tree, gallery, sizeof(gallery) - 1,
                                       0.0f, 0.0f)) {
    std::printf("  detail: gallery bytes rebuilt the tap tree\n");
    return false;
  }
  if (ResolveTapTarget(tree, 540.0f, 930.0f) != 20) {
    std::printf("  detail: declined refresh lost the fallback tree\n");
    return false;
  }
  return true;
}

// Security pin F3b inverted for the removal (AC-INPUT-020 /
// AC-RUNTIME-038/039): ex-content bytes keep the fallback tap tree —
// refresh declines and node 20 still fires, never a phantom scene node.
static bool check_ex_content_bytes_keep_fallback_tree() {
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
  RUN_CASE(check_gallery_bytes_keep_fallback_tree);
  RUN_CASE(check_ex_content_bytes_keep_fallback_tree);
  if (failures > 0) {
    std::printf("flappy_real_tree_acceptance_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_real_tree_acceptance_test: all passing\n");
  return 0;
}
