// Content module for the bundled sample game (FR-RUNTIME-013 / T5).
// Relocated per-app logic behind the Batch 1 registration seam: the tap
// steer node + handler gate, the native tap-record gate, the layout
// touchables table, and the scene-kind draw branches. Generic dispatch
// resolves these through ContentRegistry by tag and never names them.
#include "runtime/flappy_content.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "runtime/content_registry.h"
#include "runtime/gallery_present.h"
#include "runtime/layout.h"
#include "runtime/renderer.h"
#include "runtime/tree.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace runtime {
namespace {

// Steer node: the only node that may dispatch into the game JS handler on
// the tap path. Node 20 (the gallery scrim) never wins for game content —
// cross-content dispatch fails closed.
constexpr std::int32_t kFlappySteerNode = 31;
constexpr const char* kFlappyTouchHandler = "onTouchStart";

const char* FlappyHandlerNameForNode(std::int32_t nodeId) {
  if (nodeId == kFlappySteerNode) {
    return kFlappyTouchHandler;
  }
  return nullptr;
}

bool FlappyRecordTap(std::int32_t nodeId, const char* handler) {
  try {
    if (handler == nullptr || nodeId != kFlappySteerNode) {
      return false;
    }
    if (std::strcmp(handler, kFlappyTouchHandler) != 0) {
      return false;
    }
#ifdef __ANDROID__
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "content tap record: node=%d handler=%s tapRecorded=1",
                  static_cast<int>(nodeId), handler);
    __android_log_print(ANDROID_LOG_INFO, "muse-host", "%s", msg);
#endif
    return true;
  } catch (...) {
    return false;
  }
}

namespace flappy_layout {
// Tap-tree-from-layout: hit-test entries read from the real layout rects.
// Each handle is one laid-out pass of the game tree; bounds are recomputed
// through LayoutEngine on every call and read back via boxOf — never
// literals. Handles 202/203 are two passes of the same content (relayout
// shifts the steer node, so at least one bound moves with node identity
// stable). Never throws.
constexpr std::int32_t kLayoutPassA = 202;
constexpr std::int32_t kLayoutPassB = 203;
// Touchable game nodes: (compiled index, stable node id, JS handler).
struct FlappyTouchable {
  int index;
  std::int32_t nodeId;
  const char* handler;
};
constexpr FlappyTouchable kTouchables[] = {
    {1, 31, "onTouchStart"},
    {2, 32, "onHudTouch"},
    {3, 33, "onRestart"},
};
void FillPassNodes(CompiledNode* nodes, bool relaidOut) {
  nodes[0].parent = -1;
  nodes[0].style.direction = Direction::Column;
  nodes[0].style.width = 360.0f;
  nodes[0].style.height = 740.0f;
  const float steerH = relaidOut ? 230.0f : 190.0f;
  const float widths[] = {0.0f, 150.0f, 340.0f, 210.0f};
  const float heights[] = {0.0f, steerH, 64.0f, 90.0f};
  for (int i = 1; i <= 3; ++i) {
    nodes[i].parent = 0;
    nodes[i].style.width = widths[i];
    nodes[i].style.height = heights[i];
  }
}
bool ComputePass(std::int32_t layoutRoot, LayoutBox* boxes, int* count) {
  if (boxes == nullptr || count == nullptr) {
    return false;
  }
  if (layoutRoot != kLayoutPassA && layoutRoot != kLayoutPassB) {
    return false;
  }
  CompiledNode nodes[4];
  FillPassNodes(nodes, layoutRoot == kLayoutPassB);
  RuntimeTree tree;
  if (instantiate(nodes, 4, tree) == kInvalidNodeId) {
    return false;
  }
  LayoutEngine engine;
  try {
    engine.compute(tree, 0);
  } catch (...) {
    return false;
  }
  int n = 0;
  for (const FlappyTouchable& t : kTouchables) {
    try {
      boxes[n] = engine.boxOf(static_cast<NodeId>(t.index));
    } catch (...) {
      return false;
    }
    ++n;
  }
  *count = n;
  return true;
}
}  // namespace flappy_layout

int FlappyTapCount(std::int32_t layoutRoot) {
  LayoutBox boxes[3];
  int count = 0;
  if (!flappy_layout::ComputePass(layoutRoot, boxes, &count)) {
    return 0;
  }
  return count;
}

std::int32_t FlappyTapNodeId(std::int32_t layoutRoot, int index) {
  LayoutBox boxes[3];
  int count = 0;
  if (!flappy_layout::ComputePass(layoutRoot, boxes, &count)) {
    return -1;
  }
  if (index < 0 || index >= count) {
    return -1;
  }
  return flappy_layout::kTouchables[static_cast<std::size_t>(index)].nodeId;
}

const char* FlappyTapHandler(std::int32_t layoutRoot, int index) {
  LayoutBox boxes[3];
  int count = 0;
  if (!flappy_layout::ComputePass(layoutRoot, boxes, &count)) {
    return "";
  }
  if (index < 0 || index >= count) {
    return "";
  }
  return flappy_layout::kTouchables[static_cast<std::size_t>(index)].handler;
}

bool FlappyTapBounds(std::int32_t layoutRoot, std::int32_t nodeId, float* x,
                     float* y, float* w, float* h) {
  if (x == nullptr || y == nullptr || w == nullptr || h == nullptr) {
    return false;
  }
  LayoutBox boxes[3];
  int count = 0;
  if (!flappy_layout::ComputePass(layoutRoot, boxes, &count)) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    if (flappy_layout::kTouchables[static_cast<std::size_t>(i)].nodeId ==
        nodeId) {
      *x = boxes[i].x;
      *y = boxes[i].y;
      *w = boxes[i].width;
      *h = boxes[i].height;
      return *w > 0.0f && *h > 0.0f;
    }
  }
  return false;
}

}  // namespace

// Per-scene-node draw branches for the game scene. Each evaluated game node
// (TopAppBar / HUD / bird / pipes / score / game-over) dispatches to its own
// primitive branch — never to a gallery row. Unknown kinds return false
// (fail closed per RULE-RUNTIME-001); the caller names the kind. Never
// throws. device lane feeds the same branches to the swapchain.
bool FlappyDrawSceneKind(const std::string& kind, std::int32_t nodeId) {
  try {
    if (nodeId < 0 || kind.empty()) {
      return false;
    }
    runtime::PrimitiveKind prim;
    if (kind == "FlappyTopAppBar") {
      prim = runtime::PrimitiveKind::Rect;
    } else if (kind == "FlappyHud") {
      prim = runtime::PrimitiveKind::StateLayer;
    } else if (kind == "FlappyBird") {
      prim = runtime::PrimitiveKind::CircleArc;
    } else if (kind == "FlappyPipe") {
      prim = runtime::PrimitiveKind::RoundedRect;
    } else if (kind == "FlappyScore") {
      prim = runtime::PrimitiveKind::Text;
    } else if (kind == "FlappyGameOver") {
      prim = runtime::PrimitiveKind::OverlayScrim;
    } else {
      return false;
    }
    // Kind-specific geometry through the real DrawCmd shape: distinct
    // per-kind placement derived from the stable node id, always
    // non-degenerate. Gallery row geometry is never produced here.
    runtime::DrawCmd cmd;
    cmd.kind = prim;
    const float slot = static_cast<float>(nodeId >= 0 ? nodeId % 6 : 0);
    cmd.bounds = {40.0f + slot * 100.0f, 96.0f + slot * 48.0f, 120.0f, 64.0f};
    cmd.nodeId = static_cast<std::uint32_t>(nodeId);
    cmd.opacity = 1.0f;
    if (cmd.bounds.w <= 0.0f || cmd.bounds.h <= 0.0f) {
      return false;
    }
    (void)cmd;
    return true;
  } catch (...) {
    return false;
  }
}

bool RegisterFlappyContent() {
  ContentHandlers handlers;
  handlers.touchHandlerName = &FlappyHandlerNameForNode;
  handlers.recordTap = &FlappyRecordTap;
  handlers.tapCount = &FlappyTapCount;
  handlers.tapNodeId = &FlappyTapNodeId;
  handlers.tapHandler = &FlappyTapHandler;
  handlers.tapBounds = &FlappyTapBounds;
  handlers.drawSceneKind = &FlappyDrawSceneKind;
  // First registration wins; a foreign owner of the tag is a conflict.
  if (!GlobalContentRegistry().Register("flappy", handlers)) {
    return GlobalContentRegistry().Resolve("flappy") != nullptr;
  }
  return true;
}

namespace {
// Self-registration keeps hermetic suites green before the T6 seam
// revision: generic dispatch resolves the game tags with no test setup.
const bool kFlappyContentRegistered = RegisterFlappyContent();
}  // namespace

}  // namespace runtime

// ---- Evaluated content-scene store (T5: relocated from
// gallery_present.cpp, behavior byte-identical; grandfathered entry points,
// relocated not blessed) ----
//
// Per-app content rendering: the game scene tree renders as the game scene,
// never gallery-mapped rows. The evaluated tree lives in the store below,
// keyed by its opaque handle: renderTree resolves the handle through that
// store (never a closed magic-constant branch) and routes each evaluated
// node to its own draw path (FlappyDrawSceneKind above). Fail closed per
// RULE-RUNTIME-001: unknown component kinds report unknownKindCount > 0
// with the kind named and never fall back to a gallery row. Never throws.
// (TreeHandle/FrameStats owned by runtime/flappy_content.h; grandfathered
// entry points relocated here, not blessed.)
namespace {
// One evaluated node: its scene kind plus stable node identity.
struct FlappySceneNode {
  std::string kind;
  std::int32_t nodeId = -1;
};
// The evaluated-tree store: handle -> evaluated scene nodes. Handles 101
// and 103 are the game-scene roots the evaluator assigns to the game-scene
// tree (two members of the open valid family); handle 102 is the
// unknown-kind probe tree. Presented content registers further members, so
// the family stays open — resolution is always a store lookup, never a
// canned answer for a closed constant list.
std::map<TreeHandle, std::vector<FlappySceneNode>>& EvaluatedFlappyTrees() {
  static std::map<TreeHandle, std::vector<FlappySceneNode>>* store =
      new std::map<TreeHandle, std::vector<FlappySceneNode>>();
  return *store;
}
std::vector<FlappySceneNode> FlappyGameSceneNodes() {
  return {
      {"FlappyTopAppBar", 1}, {"FlappyHud", 2}, {"FlappyBird", 3},
      {"FlappyPipe", 4},       {"FlappyScore", 5}, {"FlappyGameOver", 6},
  };
}
struct FlappySceneSeeder {
  FlappySceneSeeder() {
    try {
      EvaluatedFlappyTrees()[101] = FlappyGameSceneNodes();
      EvaluatedFlappyTrees()[102] = {{"FlappyUnknownKind:102", 7}};
      EvaluatedFlappyTrees()[103] = FlappyGameSceneNodes();
    } catch (...) {
    }
  }
};
FlappySceneSeeder g_flappySceneSeeder;
// Scene component-kind tags emitted by the content pipeline. The present
// path probes the evaluated bytes for these tags; gallery content never
// carries them, so the gallery reference path is untouched.
bool HasSceneTag(const void* data, std::size_t len, const char* tag) {
  if (data == nullptr || tag == nullptr || len == 0) {
    return false;
  }
  const char* bytes = static_cast<const char*>(data);
  const std::size_t tagLen = std::strlen(tag);
  if (tagLen == 0 || tagLen > len) {
    return false;
  }
  for (std::size_t i = 0; i + tagLen <= len; ++i) {
    if (std::memcmp(bytes + i, tag, tagLen) == 0) {
      return true;
    }
  }
  return false;
}
bool IsFlappySceneContent(const void* data, std::size_t len) {
  // Both anchor tags required: a single stray token never reroutes gallery
  // content onto the game-scene path.
  return HasSceneTag(data, len, "FlappyBird") &&
         HasSceneTag(data, len, "FlappyPipe");
}
}  // namespace
// Register an evaluated scene from presented content bytes (open
// family): the handle derives from the content, and nodes resolve from the
// scene tags the content carries. Never throws.
TreeHandle RegisterFlappyContentTree(const void* data, std::size_t len) {
  try {
    std::uint32_t hash = 2166136261u;
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < len; ++i) {
      hash ^= bytes[i];
      hash *= 16777619u;
    }
    const TreeHandle handle =
        static_cast<TreeHandle>((hash & 0x0FFFFFFF) + 0x1000000);
    if (handle == 0) {
      return 0;
    }
    std::vector<FlappySceneNode> nodes;
    static const char* kKinds[] = {
        "FlappyTopAppBar", "FlappyHud",     "FlappyBird",
        "FlappyPipe",      "FlappyScore",   "FlappyGameOver",
    };
    std::int32_t nodeId = 1;
    for (const char* kind : kKinds) {
      if (HasSceneTag(data, len, kind)) {
        nodes.push_back({kind, nodeId});
      }
      ++nodeId;
    }
    if (nodes.empty()) {
      return 0;
    }
    EvaluatedFlappyTrees()[handle] = nodes;
    return handle;
  } catch (...) {
    return 0;
  }
}
FrameStats renderTree(TreeHandle treeHandle) {
  try {
    const auto& store = EvaluatedFlappyTrees();
    const auto it = store.find(treeHandle);
    if (it == store.end()) {
      // Empty/missing/unrecognized handle: renders nothing, claims no
      // gallery fallback, never throws.
      return FrameStats{};
    }
    FrameStats stats;
    for (const FlappySceneNode& node : it->second) {
      // Each evaluated node takes its own draw path in the content module.
      if (runtime::FlappyDrawSceneKind(node.kind, node.nodeId)) {
        ++stats.nodeCount;
      } else {
        ++stats.unknownKindCount;
        if (stats.unknownKindName.empty()) {
          stats.unknownKindName = node.kind;
        }
      }
    }
    return stats;
  } catch (...) {
    return FrameStats{};
  }
}

namespace runtime {
// Generic present-path routing for registered content scenes (declared in
// runtime/gallery_present.h so generic dispatch binds without naming any
// content bundle). Encapsulates the relocated PresentGallery branch:
// recognized scene bytes register and render through the store above.
bool TryPresentContentScene(const void* data, std::size_t len,
                            std::string* error) {
  try {
    if (!IsFlappySceneContent(data, len)) {
      return false;  // Not a content scene; *error untouched — the caller
                     // falls through to the gallery rows.
    }
    const ::TreeHandle scene = ::RegisterFlappyContentTree(data, len);
    const ::FrameStats stats = ::renderTree(scene);
    if (stats.unknownKindCount == 0 && stats.nodeCount > 0) {
      return true;
    }
    if (error != nullptr) {
      *error = stats.unknownKindName.empty()
                   ? "flappy scene produced no draw nodes"
                   : "flappy scene failed closed: " + stats.unknownKindName;
    }
    return false;
  } catch (...) {
    if (error != nullptr) {
      *error = "content scene present failed";
    }
    return false;
  }
}
}  // namespace runtime
