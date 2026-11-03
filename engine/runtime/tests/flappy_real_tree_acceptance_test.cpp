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
#include "runtime/renderer.h"

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
  if (failures > 0) {
    std::printf("flappy_real_tree_acceptance_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_real_tree_acceptance_test: all passing\n");
  return 0;
}
