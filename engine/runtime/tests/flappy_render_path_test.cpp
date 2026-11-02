// Batch 5 / T9 contract tests — per-app content rendering (flappy game
// scene, not gallery-mapped rows).
// Binds ONLY to the T9 `Interface:` line; never to implementation.
// Hermetic: C++17 only; no device, no network, no env/config reads, no
// filesystem, no live services. All production symbols are declared here
// against the plan; definitions land with the T9 coder pass, so linking
// this file before then MUST fail (expected red, not a malformed test).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

// T6 seam setup header (FR-RUNTIME-013): scene-block types and entry points
// are owned by the content module (runtime/flappy_content.h, relocated from
// gallery_present.cpp by coder T5); explicit registration keeps the suite
// independent of static-init order. Assertions below are unchanged in
// behavior, reached via RegisterFlappyContent() + tag resolution.
#include "runtime/flappy_content.h"

// --- T9 `Interface:` line (plan verbatim) --------------------------------
// `renderTree(treeHandle: TreeHandle) -> FrameStats — maps each evaluated
// flappy node (TopAppBar/HUD/bird/pipes/score/game-over) to its own draw
// path; returns per-frame node count + unknownKind count; unknownKind > 0
// fails closed with the node kind named, never falls back to a gallery row`
//
// T5/T6 seam binding (FR-RUNTIME-013): TreeHandle/FrameStats plus
// renderTree/RegisterFlappyContentTree are owned by
// runtime/flappy_content.h (single production definition, included above);
// the cases below bind those symbols directly. No test-local mirror.

// Test-owned flappy tree fixtures (handles only; production owns content).
// Handles are registered through the production path
// (RegisterFlappyContentTree) — never magic constants: the store starts
// empty (BUG-RUNTIME-010), so every case below renders a handle its own
// setup registered.
namespace {
TreeHandle RegisteredSceneTree(const char* text) {
  const std::string bytes = text;
  return RegisterFlappyContentTree(bytes.data(), bytes.size());
}
// Valid game-scene tree: all six scene kinds.
TreeHandle ValidSceneTree() {
  static const TreeHandle handle = RegisteredSceneTree(
      "FlappyTopAppBar FlappyHud FlappyBird FlappyPipe FlappyScore "
      "FlappyGameOver");
  return handle;
}
// Second valid game-scene tree root: same six kinds, distinct bytes, so
// registration assigns a distinct handle (open valid family).
TreeHandle ValidSceneTreeAlt() {
  static const TreeHandle handle = RegisteredSceneTree(
      "FlappyGameOver FlappyScore FlappyPipe FlappyBird FlappyHud "
      "FlappyTopAppBar relaid-out");
  return handle;
}
// Unknown-kind tree: anchored scene bytes carrying one unknown scene-kind
// token, which registration keeps as an unknown-kind node.
TreeHandle UnknownSceneTree() {
  static const TreeHandle handle =
      RegisteredSceneTree("FlappyBird FlappyPipe FlappyUnknownKind:102");
  return handle;
}
constexpr TreeHandle kEmptyTree = 0;       // empty / missing tree.
constexpr TreeHandle kUnknownHandle = 777001;  // unrecognized nonzero handle.
}  // namespace

static bool check_harness_entry_reached() { return true; }

// Named case: flappy tree renders its own draw path — node count covers the
// evaluated flappy nodes with zero unknown kinds (never gallery rows).
static bool check_flappy_tree_renders_game_scene_no_unknown_kinds() {
  const FrameStats stats = renderTree(ValidSceneTree());
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
  const FrameStats stats = renderTree(UnknownSceneTree());
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
  const FrameStats stats = renderTree(ValidSceneTreeAlt());
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
  (void)renderTree(UnknownSceneTree());
  const FrameStats stats = renderTree(ValidSceneTree());
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

// Named case (security — null-preamble registration, Batch 5 review):
// null/empty input registers nothing (handle 0, no dereference, never
// throws). Pins the function-boundary contract the coder's preamble
// follow-up will enforce.
static bool check_register_null_empty_preamble_returns_zero() {
  static const char kBytes[] = "FlappyBird FlappyPipe FlappyTopAppBar";
  try {
    if (RegisterFlappyContentTree(nullptr, 0) != 0) {
      std::printf("  detail: (nullptr, 0) registered a tree\n");
      return false;
    }
    if (RegisterFlappyContentTree(kBytes, 0) != 0) {
      std::printf("  detail: (bytes, 0) registered a tree\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: RegisterFlappyContentTree must never throw\n");
    return false;
  }
  return true;
}

// ---- T1 (bug-runtime-009-010 Option A) RED cases ----
// Plan `Interface:` line (verbatim): `renderTree(treeHandle: int32_t) ->
// FrameStats` — post-condition `stats.nodeCount == <emitted DrawCmd count>`
// per kind; `DrawFlappySceneKind` returning true requires the command
// appended to the sink/swapchain feed; unknown kind fails closed with kind
// named.
// Production sink surface (T3 coder implements in
// engine/runtime/src/flappy_content.cpp, feeding the command list/swapchain
// via engine/runtime/src/renderer.cpp): declared here against the plan; the
// definition lands with T3, so linking the new cases below before then MUST
// fail (expected red, not a malformed test). `g++ -c` on this file passes;
// the full link fails on the undefined sink symbols below.
struct DrawCmdSink;  // production-owned emission feed; T3 defines it.
bool DrawFlappySceneKindToSink(const std::string& kind, std::int32_t nodeId,
                               DrawCmdSink& sink);
std::size_t EmittedDrawCmdCount(const DrawCmdSink& sink);
DrawCmdSink& TestDrawCmdFeed();  // T3-provided hermetic feed accessor.

// Named case (emission): every scene kind appends exactly one DrawCmd —
// true requires the append, so nodeCount equals the emitted count per kind.
static bool check_each_kind_appends_exactly_one_draw_cmd() {
  static const char* kKinds[] = {
      "FlappyTopAppBar", "FlappyHud",     "FlappyBird",
      "FlappyPipe",      "FlappyScore",   "FlappyGameOver",
  };
  DrawCmdSink& sink = TestDrawCmdFeed();
  for (const char* kind : kKinds) {
    const std::size_t before = EmittedDrawCmdCount(sink);
    if (!DrawFlappySceneKindToSink(kind, 3, sink)) {
      std::printf("  detail: kind %s did not emit\n", kind);
      return false;
    }
    if (EmittedDrawCmdCount(sink) != before + 1) {
      std::printf("  detail: kind %s true without exactly one append\n", kind);
      return false;
    }
  }
  return true;
}

// Named case (discard fails): true-without-append is a defect —
// BUG-RUNTIME-009 is `(void)cmd; return true` counting a discarded command
// as drawn. The emitted depth must move on every true.
static bool check_true_without_append_fails() {
  DrawCmdSink& sink = TestDrawCmdFeed();
  const std::size_t before = EmittedDrawCmdCount(sink);
  const bool ok = DrawFlappySceneKindToSink("FlappyBird", 3, sink);
  if (ok && EmittedDrawCmdCount(sink) == before) {
    std::printf("  detail: true-without-append counted as drawn\n");
    return false;
  }
  return ok;
}

// Named case (fail-closed): unknown kind appends nothing, returns false,
// and names the kind — never a gallery row.
static bool check_unknown_kind_appends_nothing_names_kind() {
  DrawCmdSink& sink = TestDrawCmdFeed();
  const std::size_t before = EmittedDrawCmdCount(sink);
  if (DrawFlappySceneKindToSink("FlappyUnknownKind:102", 7, sink)) {
    std::printf("  detail: unknown kind emitted\n");
    return false;
  }
  if (EmittedDrawCmdCount(sink) != before) {
    std::printf("  detail: unknown kind appended without true\n");
    return false;
  }
  const FrameStats stats = renderTree(UnknownSceneTree());
  if (stats.unknownKindCount <= 0 || stats.unknownKindName.empty()) {
    std::printf("  detail: unknown kind not named fail-closed\n");
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
  // T6 seam setup (FR-RUNTIME-013): register content tags before asserting
  // render outcomes. Idempotent.
  (void)runtime::RegisterFlappyContent();
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_flappy_tree_renders_game_scene_no_unknown_kinds);
  RUN_CASE(check_unknown_kind_fails_closed_with_kind_named);
  RUN_CASE(check_empty_tree_renders_nothing_no_gallery_fallback);
  RUN_CASE(check_unrecognized_nonzero_handle_renders_nothing);
  RUN_CASE(check_second_flappy_tree_resolves_via_tree_not_magic_handle);
  RUN_CASE(check_scene_render_isolated_from_unknown_renders);
  RUN_CASE(check_register_null_empty_preamble_returns_zero);
  RUN_CASE(check_each_kind_appends_exactly_one_draw_cmd);
  RUN_CASE(check_true_without_append_fails);
  RUN_CASE(check_unknown_kind_appends_nothing_names_kind);
  if (failures > 0) {
    std::printf("flappy_render_path_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_render_path_test: all passing\n");
  return 0;
}
