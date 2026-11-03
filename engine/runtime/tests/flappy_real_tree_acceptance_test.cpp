// T8R4 tester-owned acceptance regression (T8R3/F1): the REAL built flappy
// tree (`examples/flappy/dist/tree.mui.bin`, 12944 B) must present as the
// flappy game scene — never the gallery fallback.
//
// Background: `TryPresentContentScene` used to demand the synthetic
// `FlappyBird`+`FlappyPipe` byte-tags, which the real tree never carries, so
// every device frame fell through to `GalleryPage: gallery-rows=41` with taps
// firing gallery scrim node 20. Host suites stayed green because they only
// fed synthetic tagged bytes. This test presents the REAL bytes on disk, so
// a gate regression fails here first.
//
// Tester-owned integration/acceptance test (reads the filesystem by design);
// NOT a hermetic unit test — code-unit-tester owns those.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "runtime/flappy_content.h"
#include "runtime/gallery_present.h"
#include "runtime/content_registry.h"
#include "runtime/renderer.h"

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

std::string RealTreePath() {
  if (const char* env = std::getenv("FLAPPY_REAL_TREE")) {
    if (env[0] != '\0') return env;
  }
  // Repo-root-relative candidates: ctest runs from the build dir, so also
  // try the __FILE__-anchored path (tests/ -> repo root is 3 levels up).
  const char* kCandidates[] = {
      "examples/flappy/dist/tree.mui.bin",
      "../examples/flappy/dist/tree.mui.bin",
  };
  for (const char* c : kCandidates) {
    std::ifstream f(c, std::ios::binary);
    if (f.good()) return c;
  }
  std::string here = __FILE__;
  const std::string suffix = "engine/runtime/tests/flappy_real_tree_acceptance_test.cpp";
  const auto pos = here.rfind(suffix);
  if (pos != std::string::npos) {
    const std::string root = here.substr(0, pos);
    const std::string p = root + "examples/flappy/dist/tree.mui.bin";
    std::ifstream f(p, std::ios::binary);
    if (f.good()) return p;
  }
  return "examples/flappy/dist/tree.mui.bin";
}

std::vector<char> LoadRealTree() {
  const std::string path = RealTreePath();
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f.good()) {
    std::printf("  detail: cannot open real tree: %s\n", path.c_str());
    return {};
  }
  const std::streamsize n = f.tellg();
  f.seekg(0, std::ios::beg);
  std::vector<char> bytes(static_cast<std::size_t>(n));
  if (!f.read(bytes.data(), n)) {
    std::printf("  detail: short read on %s\n", path.c_str());
    return {};
  }
  std::printf("  info: real tree %s bytes=%lld\n", path.c_str(),
              static_cast<long long>(n));
  return bytes;
}

class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

static runtime::Renderer acceptance_renderer() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return runtime::Renderer(caps);
}

// Named case: the real tree registers a scene (nonzero handle) — the T8R3
// defect registered nothing recognizable and fell to gallery rows.
static bool check_real_tree_registers_scene() {
  const std::vector<char> bytes = LoadRealTree();
  if (bytes.empty()) return false;
  (void)runtime::RegisterFlappyContent();
  const TreeHandle h =
      RegisterFlappyContentTree(bytes.data(), bytes.size());
  if (h == 0) {
    std::printf("  detail: real tree registered handle 0 (gallery fallback)\n");
    return false;
  }
  return true;
}

// Named case: the real tree renders the full game scene — all six scene
// nodes, zero unknown kinds, never a gallery row.
static bool check_real_tree_renders_game_scene_no_gallery_fallback() {
  const std::vector<char> bytes = LoadRealTree();
  if (bytes.empty()) return false;
  (void)runtime::RegisterFlappyContent();
  const TreeHandle h =
      RegisterFlappyContentTree(bytes.data(), bytes.size());
  if (h == 0) return false;
  const FrameStats stats = renderTree(h);
  if (stats.unknownKindCount != 0) {
    std::printf("  detail: real tree unknownKindCount=%d kind=%s\n",
                stats.unknownKindCount, stats.unknownKindName.c_str());
    return false;
  }
  if (stats.nodeCount < 6) {
    std::printf("  detail: real tree nodeCount=%d, expected >= 6 scene nodes\n",
                stats.nodeCount);
    return false;
  }
  return true;
}

// Named case: the real tree presents through the content-scene path and
// drains DrawCmds — the device frame must not fall through to gallery.
static bool check_real_tree_presents_content_scene_and_drains() {
  const std::vector<char> bytes = LoadRealTree();
  if (bytes.empty()) return false;
  (void)runtime::RegisterFlappyContent();
  runtime::Renderer renderer = acceptance_renderer();
  FakeSwapchain target;
  std::string error;
  const bool ok = runtime::TryPresentContentScene(
      bytes.data(), bytes.size(), renderer, target, &error);
  if (!ok) {
    std::printf("  detail: TryPresentContentScene false: %s\n",
                error.c_str());
    return false;
  }
  if (!error.empty()) {
    std::printf("  detail: success left diagnostic: %s\n", error.c_str());
    return false;
  }
  if (target.captured.empty()) {
    std::printf("  detail: presented scene drained zero cmds\n");
    return false;
  }
  return true;
}

}  // namespace

// Security pin F1 (AC-RUNTIME-032): paired-tag gate — the scene presents
// ONLY when the full anchor combination holds. Positive: synthetic pair and
// real pair both present with the diagnostic untouched. Negative: any single
// anchor, gallery bytes, empty, or null never reroute and leave the error
// untouched so the caller keeps the gallery fallback.
static bool check_paired_tag_gate_single_anchor_never_reroutes() {
  (void)runtime::RegisterFlappyContent();
  {
    const char both[] = "FlappyBird FlappyPipe";
    std::string error;
    if (!runtime::TryPresentContentScene(both, sizeof(both) - 1, &error)) {
      std::printf("  detail: synthetic pair did not present\n");
      return false;
    }
    if (!error.empty()) {
      std::printf("  detail: synthetic-pair success left diagnostic: %s\n",
                  error.c_str());
      return false;
    }
  }
  {
    const char real[] = "TopAppBar flappy-hero HUD";
    std::string error;
    if (!runtime::TryPresentContentScene(real, sizeof(real) - 1, &error)) {
      std::printf("  detail: real pair did not present\n");
      return false;
    }
    if (!error.empty()) {
      std::printf("  detail: real-pair success left diagnostic: %s\n",
                  error.c_str());
      return false;
    }
  }
  const char* singles[] = {
      "FlappyBird",
      "FlappyPipe",
      "TopAppBar",
      "flappy-hero",
      "GalleryPage gallery-rows=41",
      "",
  };
  for (const char* s : singles) {
    std::string error = "SENTINEL";
    std::size_t len = 0;
    while (s[len] != '\0') ++len;
    if (runtime::TryPresentContentScene(s, len, &error)) {
      std::printf("  detail: single anchor presented: %.40s\n", s);
      return false;
    }
    if (error != "SENTINEL") {
      std::printf("  detail: non-content touched error: %.40s -> %s\n", s,
                  error.c_str());
      return false;
    }
  }
  {
    std::string error = "SENTINEL";
    if (runtime::TryPresentContentScene(nullptr, 0, &error)) {
      std::printf("  detail: null presented\n");
      return false;
    }
    if (error != "SENTINEL") {
      std::printf("  detail: null touched error: %s\n", error.c_str());
      return false;
    }
  }
  return true;
}

// Security pin F2 (FR-RUNTIME-013): broken-scene fail-closed — recognized
// but undrawable bytes fail with the named error, so the frame path skips
// the gallery fallback (non-empty error) instead of silently gallery-mapping.
static bool check_broken_scene_fail_closed_named_error_no_fallback() {
  (void)runtime::RegisterFlappyContent();
  const char broken[] = "FlappyBird FlappyPipe FlappyQuantumBlorb";
  std::string error;
  if (runtime::TryPresentContentScene(broken, sizeof(broken) - 1, &error)) {
    std::printf("  detail: recognized-but-broken scene presented\n");
    return false;
  }
  if (error.empty()) {
    std::printf("  detail: broken scene left no error (fallback taken)\n");
    return false;
  }
  if (error.find("FlappyQuantumBlorb") == std::string::npos) {
    std::printf("  detail: broken-scene error names no kind: %s\n",
                error.c_str());
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

// Security pin F3b (AC-INPUT-020 / AC-RUNTIME-038/039): real bytes rebuild
// the tap tree to scene nodes — every laid-out touchable resolves to its
// own scene id and gallery node 20 is gone.
static bool check_real_bytes_rebuild_scene_taps_no_gallery_node() {
  const std::vector<char> bytes = LoadRealTree();
  if (bytes.empty()) return false;
  (void)runtime::RegisterFlappyContent();
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  if (!RefreshTapTreeFromPresentedScene(tree, bytes.data(), bytes.size(),
                                        0.0f, 0.0f)) {
    std::printf("  detail: real bytes did not rebuild the tap tree\n");
    return false;
  }
  PresentedSceneTap taps[8];
  const int count = runtime::QueryPresentedSceneTaps(
      bytes.data(), bytes.size(), taps, 8);
  if (count <= 0) {
    std::printf("  detail: real bytes query 0 taps\n");
    return false;
  }
  for (int i = 0; i < count; ++i) {
    if (taps[i].nodeId == 20) {
      std::printf("  detail: gallery node 20 leaked into scene taps\n");
      return false;
    }
    const float cx = taps[i].x + taps[i].w * 0.5f;
    const float cy = taps[i].y + taps[i].h * 0.5f;
    const std::int32_t hit = ResolveTapTarget(tree, cx, cy);
    if (hit == 20 || hit == -1) {
      std::printf("  detail: scene tap %d center resolves to %d\n",
                  taps[i].nodeId, hit);
      return false;
    }
    bool known = false;
    for (int j = 0; j < count; ++j) {
      if (hit == taps[j].nodeId) {
        known = true;
        break;
      }
    }
    if (!known) {
      std::printf("  detail: scene tap %d center hit phantom %d\n",
                  taps[i].nodeId, hit);
      return false;
    }
  }
  return true;
}

// Security pin F4 (failure-only logging convention): the gate log half of
// the contract — success stays silent (error untouched), failure emits a
// bounded named diagnostic with lengths/presence/error literal only.
// ASSUMPTION: the __ANDROID__ F3DIAG logcat emission itself is device proof;
// this pins the host-observable gate-error contract behind it. Inert payload
// bytes must never appear in the diagnostic (no tree-content leak).
static bool check_gate_log_failure_only_bounded_no_leak() {
  (void)runtime::RegisterFlappyContent();
  {
    const std::vector<char> bytes = LoadRealTree();
    if (bytes.empty()) return false;
    std::string error = "SENTINEL";
    if (!runtime::TryPresentContentScene(bytes.data(), bytes.size(),
                                         &error)) {
      std::printf("  detail: real tree failed: %s\n", error.c_str());
      return false;
    }
    if (error != "SENTINEL") {
      std::printf("  detail: healthy frame not silent: %s\n", error.c_str());
      return false;
    }
  }
  {
    const char broken[] =
        "FlappyBird FlappyPipe FlappyQuantumBlorb SECRET_MARKER_XYZ_123";
    std::string error;
    if (runtime::TryPresentContentScene(broken, sizeof(broken) - 1, &error)) {
      std::printf("  detail: broken scene presented\n");
      return false;
    }
    if (error.empty()) {
      std::printf("  detail: failure emitted nothing\n");
      return false;
    }
    if (error.size() >= 320) {
      std::printf("  detail: diagnostic unbounded: %zu bytes\n", error.size());
      return false;
    }
    if (error.find("flappy scene failed closed") == std::string::npos) {
      std::printf("  detail: diagnostic missing error literal: %s\n",
                  error.c_str());
      return false;
    }
    if (error.find("SECRET_MARKER_XYZ_123") != std::string::npos) {
      std::printf("  detail: diagnostic leaks tree content: %s\n",
                  error.c_str());
      return false;
    }
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
  RUN_CASE(check_real_tree_registers_scene);
  RUN_CASE(check_real_tree_renders_game_scene_no_gallery_fallback);
  RUN_CASE(check_real_tree_presents_content_scene_and_drains);
  RUN_CASE(check_paired_tag_gate_single_anchor_never_reroutes);
  RUN_CASE(check_broken_scene_fail_closed_named_error_no_fallback);
  RUN_CASE(check_gallery_bytes_keep_fallback_tree);
  RUN_CASE(check_real_bytes_rebuild_scene_taps_no_gallery_node);
  RUN_CASE(check_gate_log_failure_only_bounded_no_leak);
  if (failures > 0) {
    std::printf("flappy_real_tree_acceptance_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_real_tree_acceptance_test: all passing\n");
  return 0;
}
