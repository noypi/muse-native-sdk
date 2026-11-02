// Batch 5 / T9 contract tests — per-app content rendering (flappy game
// scene, not gallery-mapped rows).
// Binds ONLY to the T9 `Interface:` line; never to implementation.
// Hermetic: C++17 only; no device, no network, no env/config reads, no
// filesystem, no live services. All production symbols are declared here
// against the plan; definitions land with the T9 coder pass, so linking
// this file before then MUST fail (expected red, not a malformed test).
#include <cstdint>
#include <cstdio>
#include <string>

// --- T9 `Interface:` line (plan verbatim) --------------------------------
// `renderTree(treeHandle: TreeHandle) -> FrameStats — maps each evaluated
// flappy node (TopAppBar/HUD/bird/pipes/score/game-over) to its own draw
// path; returns per-frame node count + unknownKind count; unknownKind > 0
// fails closed with the node kind named, never falls back to a gallery row`
using TreeHandle = int32_t;  // opaque handle; production owns the real type.
struct FrameStats {
  int32_t nodeCount = 0;
  int32_t unknownKindCount = 0;
  std::string unknownKindName;  // named kind when unknownKindCount > 0.
};
FrameStats renderTree(TreeHandle treeHandle);

// Test-owned flappy tree fixtures (handles only; production owns content).
namespace {
constexpr TreeHandle kFlappyTree = 101;    // flappy game-scene tree.
constexpr TreeHandle kEmptyTree = 0;       // empty / missing tree.
constexpr TreeHandle kUnknownKindTree = 102;  // tree with an unknown kind.
// Tightened-plan handles: the valid family is OPEN (resolved via the real
// evaluated tree, never a closed magic-constant list), so the test pins a
// second valid member plus an unrecognized handle the stub cannot know.
constexpr TreeHandle kFlappyTreeAlt = 103;  // second valid flappy tree root.
constexpr TreeHandle kUnknownHandle = 777001;  // unrecognized nonzero handle.
}  // namespace

static bool check_harness_entry_reached() { return true; }

// Named case: flappy tree renders its own draw path — node count covers the
// evaluated flappy nodes with zero unknown kinds (never gallery rows).
static bool check_flappy_tree_renders_game_scene_no_unknown_kinds() {
  const FrameStats stats = renderTree(kFlappyTree);
  if (stats.nodeCount <= 0) {
    std::printf("  detail: expected nodeCount > 0 for flappy tree\n");
    return false;
  }
  if (stats.unknownKindCount != 0) {
    std::printf("  detail: expected unknownKindCount 0, kind=%s\n",
                stats.unknownKindName.c_str());
    return false;
  }
  return true;
}

// Named case: unknown component kind fails closed — unknownKindCount > 0
// with the kind named, never silently mapped to a gallery row.
static bool check_unknown_kind_fails_closed_with_kind_named() {
  const FrameStats stats = renderTree(kUnknownKindTree);
  if (stats.unknownKindCount <= 0) {
    std::printf("  detail: expected unknownKindCount > 0\n");
    return false;
  }
  if (stats.unknownKindName.empty()) {
    std::printf("  detail: expected unknown kind named, got empty\n");
    return false;
  }
  return true;
}

// Named case (boundary): empty tree renders nothing and never claims a
// gallery fallback — nodeCount 0, no crash, no throw.
static bool check_empty_tree_renders_nothing_no_gallery_fallback() {
  FrameStats stats;
  try {
    stats = renderTree(kEmptyTree);
  } catch (...) {
    std::printf("  detail: renderTree must never throw\n");
    return false;
  }
  if (stats.nodeCount != 0) {
    std::printf("  detail: expected nodeCount 0 for empty tree\n");
    return false;
  }
  return true;
}

// Named case (stub-bar): an unrecognized nonzero handle renders nothing —
// nodeCount 0, no gallery fallback, never throws. A magic-handle stub can
// only canned-answer the handles it was written against; the open family of
// unrecognized handles must stay empty.
static bool check_unrecognized_nonzero_handle_renders_nothing() {
  FrameStats stats;
  try {
    stats = renderTree(kUnknownHandle);
  } catch (...) {
    std::printf("  detail: renderTree must never throw\n");
    return false;
  }
  if (stats.nodeCount != 0) {
    std::printf("  detail: expected nodeCount 0 for unrecognized handle\n");
    return false;
  }
  if (stats.unknownKindName.find("Gallery") != std::string::npos ||
      stats.unknownKindName.find("gallery") != std::string::npos ||
      stats.unknownKindName.find("row") != std::string::npos) {
    std::printf("  detail: unrecognized handle fell back to gallery row: %s\n",
                stats.unknownKindName.c_str());
    return false;
  }
  return true;
}

// Named case (draw-path routing): a second valid flappy tree handle resolves
// through the real evaluated tree to the game scene — nodeCount > 0 with zero
// unknown kinds. A magic-handle stub (canned stats for 101/102, no call site
// in the present path) answers 0 here and FAILS; only resolution via the
// evaluated tree can satisfy an open family of valid handles.
static bool check_second_flappy_tree_resolves_via_tree_not_magic_handle() {
  const FrameStats stats = renderTree(kFlappyTreeAlt);
  if (stats.nodeCount <= 0) {
    std::printf("  detail: second flappy tree rendered nothing (stub answers 0)\n");
    return false;
  }
  if (stats.unknownKindCount != 0) {
    std::printf("  detail: expected unknownKindCount 0, kind=%s\n",
                stats.unknownKindName.c_str());
    return false;
  }
  return true;
}

// Named case (routing isolation): interleaved unknown-kind renders never
// pollute the scene path — the flappy scene still renders clean with an
// empty unknown-kind name (no gallery-fallback residue) after an unknown
// render.
static bool check_scene_render_isolated_from_unknown_renders() {
  (void)renderTree(kUnknownKindTree);
  const FrameStats stats = renderTree(kFlappyTree);
  if (stats.nodeCount <= 0 || stats.unknownKindCount != 0) {
    std::printf("  detail: scene polluted by interleaved unknown render\n");
    return false;
  }
  if (!stats.unknownKindName.empty()) {
    std::printf("  detail: scene carries fallback residue: %s\n",
                stats.unknownKindName.c_str());
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
  RUN_CASE(check_flappy_tree_renders_game_scene_no_unknown_kinds);
  RUN_CASE(check_unknown_kind_fails_closed_with_kind_named);
  RUN_CASE(check_empty_tree_renders_nothing_no_gallery_fallback);
  RUN_CASE(check_unrecognized_nonzero_handle_renders_nothing);
  RUN_CASE(check_second_flappy_tree_resolves_via_tree_not_magic_handle);
  RUN_CASE(check_scene_render_isolated_from_unknown_renders);
  if (failures > 0) {
    std::printf("flappy_render_path_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_render_path_test: all passing\n");
  return 0;
}
