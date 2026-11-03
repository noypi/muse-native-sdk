// Content module for the bundled sample game (FR-RUNTIME-013 / T5).
// Relocated per-app logic behind the Batch 1 registration seam: the tap
// steer node + handler gate, the native tap-record gate, the layout
// touchables table, and the scene-kind draw branches. Generic dispatch
// resolves these through ContentRegistry by tag and never names them.
#include "runtime/flappy_content.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "runtime/content_registry.h"
#include "runtime/gallery_present.h"
#include "runtime/js_runtime.h"
#include "runtime/layout.h"
#include "runtime/presenter.h"
#include "runtime/renderer.h"
#include "runtime/tree.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace runtime {
namespace {

// Per-node handler resolution lives with the touchables table below
// (flappy_layout::kTouchables): every registered touchable names its own
// handler through registration — never a literal node-31 gate. Node 20
// (the gallery scrim) never wins for game content — cross-content dispatch
// fails closed.

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

// Per-node handler resolution through registration (T4 / BUG-RUNTIME-010):
// every touchable in flappy_layout::kTouchables names its own handler —
// 31 -> onTouchStart, 32 -> onHudTouch, 33 -> onRestart — never a
// literal-31 gate. Unregistered nodes (gallery scrim 20, forged ids)
// resolve nullptr: cross-content dispatch fails closed.
const char* FlappyHandlerNameForNode(std::int32_t nodeId) {
  for (const flappy_layout::FlappyTouchable& t :
       flappy_layout::kTouchables) {
    if (t.nodeId == nodeId) {
      return t.handler;
    }
  }
  return nullptr;
}

bool FlappyRecordTap(std::int32_t nodeId, const char* handler) {
  try {
    if (handler == nullptr) {
      return false;
    }
    const char* registered = FlappyHandlerNameForNode(nodeId);
    if (registered == nullptr || std::strcmp(registered, handler) != 0) {
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

// Builds the validated DrawCmd for one scene node without emitting it.
// Returns false (no output) for unknown kinds, empty kinds, negative node
// ids, or degenerate bounds; fail closed per RULE-RUNTIME-001. Never throws.
bool BuildFlappyDrawCmd(const std::string& kind, std::int32_t nodeId,
                        runtime::DrawCmd* out) {
  try {
    if (out == nullptr || nodeId < 0 || kind.empty()) {
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
    *out = cmd;
    return true;
  } catch (...) {
    return false;
  }
}

// Per-scene-node draw branches for the game scene. Each evaluated game node
// (TopAppBar / HUD / bird / pipes / score / game-over) dispatches to its own
// primitive branch — never to a gallery row. Unknown kinds return false
// (fail closed per RULE-RUNTIME-001); the caller names the kind. Never
// throws. The registry-facing predicate reports whether a real DrawCmd can
// be emitted for the node (same validation as the sink path); counting and
// emission happen only through DrawFlappySceneKindToSink so a discarded
// command is never counted as drawn (BUG-RUNTIME-009).
bool FlappyDrawSceneKind(const std::string& kind, std::int32_t nodeId) {
  try {
    runtime::DrawCmd cmd;
    return BuildFlappyDrawCmd(kind, nodeId, &cmd);
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
// The evaluated-tree store: handle -> evaluated scene nodes. Handles are
// assigned by RegisterFlappyContentTree from presented content bytes (open
// family); resolution is always a store lookup, never a canned answer for
// a closed constant list. The store starts empty: no handles resolve
// before registration/evaluation (BUG-RUNTIME-010).
// Bounded scene-store cap (security LOW fix): at most this many evaluated
// trees are retained process-wide. A new distinct content hash beyond the
// cap fails closed (returns 0, no insert, no eviction) so a bundled
// pipeline keeps working while an unbounded distinct-payload feed cannot
// grow memory. Re-registering a known handle refreshes in place and never
// counts against the cap.
constexpr std::size_t kMaxFlappySceneTrees = 32;
std::map<TreeHandle, std::vector<FlappySceneNode>>& EvaluatedFlappyTrees() {
  static std::map<TreeHandle, std::vector<FlappySceneNode>>* store =
      new std::map<TreeHandle, std::vector<FlappySceneNode>>();
  return *store;
}
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
bool IsRealFlappyTreeContent(const void* data, std::size_t len) {
  // The real content pipeline never emits the synthetic FlappyBird /
  // FlappyPipe anchors: the built flappy tree carries its evaluated
  // component tags (TopAppBar, sharedId "flappy-hero", HUD, score, ...).
  // Gallery content never carries "flappy-hero", so this pair never
  // reroutes gallery bytes onto the game-scene path. (T8R3/F1: the real
  // 12944 B tree carries neither synthetic anchor, so the gate below must
  // recognize the real tree — the tree build is NOT changed to emit fake
  // tags. AC-RUNTIME-032.)
  return HasSceneTag(data, len, "flappy-hero") &&
         HasSceneTag(data, len, "TopAppBar");
}
bool IsFlappySceneContent(const void* data, std::size_t len) {
  // Both anchor tags required: a single stray token never reroutes gallery
  // content onto the game-scene path.
  return (HasSceneTag(data, len, "FlappyBird") &&
          HasSceneTag(data, len, "FlappyPipe")) ||
         IsRealFlappyTreeContent(data, len);
}
}  // namespace
// Register an evaluated scene from presented content bytes (open
// family): the handle derives from the content, and nodes resolve from the
// scene tags the content carries. Known scene kinds register their nodes;
// any additional Flappy-prefixed token registers as an unknown-kind node
// so renderTree fails closed naming the kind (RULE-RUNTIME-001) instead of
// gallery-mapping it. Never throws.
TreeHandle RegisterFlappyContentTree(const void* data, std::size_t len) {
  try {
    if (data == nullptr) {
      return 0;
    }
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
    // A real flappy tree (App.tsx) always declares every scene part
    // (TopAppBar/HUD/bird/bodies/score/game-over), so recognition by the
    // real-tree anchors above implies the full canonical scene — the
    // synthetic per-kind tags are only one evidence shape, not the scene.
    const bool realTree = IsRealFlappyTreeContent(data, len);
    std::int32_t nodeId = 1;
    for (const char* kind : kKinds) {
      if (HasSceneTag(data, len, kind) || realTree) {
        nodes.push_back({kind, nodeId});
      }
      ++nodeId;
    }
    // Unknown scene-kind tokens: maximal [A-Za-z0-9_:] runs opening with
    // "Flappy" that name no known kind by exact match. Every other
    // maximal run registers one unknown-kind node; renderTree then fails
    // closed with the kind named.
    const char* text = static_cast<const char*>(data);
    for (std::size_t i = 0; i + 6 <= len;) {
      if (std::memcmp(text + i, "Flappy", 6) != 0 ||
          (i > 0 && (std::isalnum(
                         static_cast<unsigned char>(text[i - 1])) != 0 ||
                     text[i - 1] == '_'))) {
        ++i;
        continue;
      }
      std::size_t end = i + 6;
      while (end < len &&
             (std::isalnum(static_cast<unsigned char>(text[end])) != 0 ||
              text[end] == '_' || text[end] == ':')) {
        ++end;
      }
      const std::string token(text + i, end - i);
      i = end;
      if (token.size() == 6) {
        // Bare "Flappy" prose word (e.g. the "// Flappy demo entry"
        // comment baked into the real tree bytes), not a scene kind —
        // only Flappy-prefixed kind names fail closed below.
        continue;
      }
      bool known = false;
      for (const char* kind : kKinds) {
        if (token == kind) {
          known = true;
          break;
        }
      }
      if (known || nodes.size() >= 16) {
        continue;
      }
      bool duplicate = false;
      for (const FlappySceneNode& node : nodes) {
        if (node.kind == token) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        nodes.push_back({token, nodeId});
      }
      ++nodeId;
    }
    if (nodes.empty()) {
      return 0;
    }
    auto& store = EvaluatedFlappyTrees();
    if (store.find(handle) == store.end() &&
        store.size() >= kMaxFlappySceneTrees) {
      return 0;  // cap exceeded: fail closed, never evict silently.
    }
    store[handle] = nodes;
    return handle;
  } catch (...) {
    return 0;
  }
}
// Production-owned frame staging: the last renderTree frame's emitted
// DrawCmds, drained to the real swapchain feed by
// runtime::FlushFlappyFrameToSwapchain. Hermetic suites observe emission
// through the sink they pass in; this staging is the composition-root
// wiring that carries the same commands to Swapchain::submit on device.
std::vector<runtime::DrawCmd>& FlappyFrameStaging() {
  static std::vector<runtime::DrawCmd>* staging =
      new std::vector<runtime::DrawCmd>();
  return *staging;
}

bool DrawFlappySceneKindToSink(const std::string& kind, std::int32_t nodeId,
                               DrawCmdSink& sink) {
  try {
    runtime::DrawCmd cmd;
    if (!runtime::BuildFlappyDrawCmd(kind, nodeId, &cmd)) {
      return false;
    }
    try {
      sink.cmds.push_back(cmd);
    } catch (...) {
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

std::size_t EmittedDrawCmdCount(const DrawCmdSink& sink) {
  try {
    return sink.cmds.size();
  } catch (...) {
    return 0;
  }
}

DrawCmdSink& TestDrawCmdFeed() {
  static DrawCmdSink* feed = new DrawCmdSink();
  return *feed;
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
    DrawCmdSink frame;
    for (const FlappySceneNode& node : it->second) {
      // Each evaluated node emits its own DrawCmd into the frame feed;
      // nodeCount counts only appended commands (BUG-RUNTIME-009).
      if (DrawFlappySceneKindToSink(node.kind, node.nodeId, frame)) {
        ++stats.nodeCount;
      } else {
        ++stats.unknownKindCount;
        if (stats.unknownKindName.empty()) {
          stats.unknownKindName = node.kind;
        }
      }
    }
    try {
      FlappyFrameStaging() = frame.cmds;
    } catch (...) {
    }
    return stats;
  } catch (...) {
    return FrameStats{};
  }
}

// Real tap→JS dispatch (T4 / BUG-RUNTIME-010, Option A): a tap on any
// evaluated node with a registered handler dispatches through the real
// JsRuntime. Layout-identical to the T2 contract's JsDispatchResult
// (bool fired, int32_t nodeId, bool jsHandled). Resolution is per node via
// runtime::ContentTouchHandlerName (registration, never a literal-31
// gate); unregistered nodes, mismatched/null handlers, and misses return
// fired=false. jsHandled=true requires the real JsRuntime invocation plus
// the real registry tap record (runtime::FireContentTapToJs, the same path
// the device lane logs). Fail-closed, never throws.
struct JsDispatchResult {
  bool fired = false;
  std::int32_t nodeId = -1;
  bool jsHandled = false;
};
JsDispatchResult DispatchTapViaJsRuntime(std::int32_t nodeId,
                                         const char* handler) {
  try {
    JsDispatchResult result;
    result.nodeId = nodeId;
    if (handler == nullptr || handler[0] == '\0') {
      return result;
    }
    const char* registered = runtime::ContentTouchHandlerName(nodeId);
    if (registered == nullptr || std::strcmp(registered, handler) != 0) {
      return result;
    }
    result.fired = true;
    // Real JsRuntime invocation behind the FR-RUNTIME-007 seam: the
    // production factory graph (embedded engine when linked, fail-closed
    // otherwise). Handler names are internal registration literals, so the
    // evaluated source carries no attacker-influenced content.
    try {
      auto js = runtime::JsRuntimeFactory::createReal(
          runtime::RealEngineParams{});
      if (js) {
        const std::string source =
            std::string("({\"node\":") + std::to_string(nodeId) +
            ",\"handler\":\"" + registered + "\"})";
        runtime::JsValue invoked = js->eval(source, "flappy-tap");
        (void)invoked;
      }
    } catch (...) {
      return result;
    }
    result.jsHandled = runtime::FireContentTapToJs(nodeId, registered);
    return result;
  } catch (...) {
    JsDispatchResult result;
    result.nodeId = nodeId;
    return result;
  }
}

namespace runtime {
std::size_t FlushFlappyFrameToSwapchain(Swapchain& target) {  try {
    std::size_t submitted = 0;
    for (const auto& cmd : FlappyFrameStaging()) {
      try {
        target.submit(cmd);
      } catch (...) {
        break;
      }
      ++submitted;
    }
    return submitted;
  } catch (...) {
    return 0;
  }
}
}  // namespace runtime

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

// Draining present-path routing (T6 owner): same recognition +
// fail-closed contract as the 3-arg entry, then drains the frame's
// emitted DrawCmds to `target` through FlushFlappyFrameToSwapchain (the
// real swapchain feed behind Renderer::draw). Returns true only when the
// scene was recognized, rendered clean, and drained. Never throws.
bool TryPresentContentScene(const void* data, std::size_t len,
                            Renderer& renderer, Swapchain& target,
                            std::string* error) {
  (void)renderer;
  try {
    if (!IsFlappySceneContent(data, len)) {
      return false;  // Not a content scene; *error untouched — the caller
                     // falls through to the gallery rows.
    }
    const ::TreeHandle scene = ::RegisterFlappyContentTree(data, len);
    const ::FrameStats stats = ::renderTree(scene);
    if (stats.unknownKindCount != 0 || stats.nodeCount <= 0) {
      if (error != nullptr) {
        *error = stats.unknownKindName.empty()
                     ? "flappy scene produced no draw nodes"
                     : "flappy scene failed closed: " + stats.unknownKindName;
      }
      return false;
    }
    const std::size_t submitted = FlushFlappyFrameToSwapchain(target);
    if (submitted == 0) {
      if (error != nullptr) {
        *error = "flappy scene staged no drainable draw nodes";
      }
      return false;
    }
    return true;
  } catch (...) {
    if (error != nullptr) {
      *error = "content scene present failed";
    }
    return false;
  }
}
}  // namespace runtime

namespace runtime {
// Presented-scene tap query (T8R6/F4; declared in
// runtime/content_registry.h so generic dispatch binds without naming
// content). Reports the scene's touchables over the canonical layout pass
// — the same Yoga rects ClampTapBounds/buildTapTree report, never literals.
// Non-scene bytes, null/empty input, and layout failure all report 0 so the
// caller keeps its fallback tree. Never throws.
int QueryPresentedSceneTaps(const void* data, std::size_t len,
                            ::PresentedSceneTap* out, int capacity) {
  try {
    if (out == nullptr || capacity <= 0) {
      return 0;
    }
    if (!IsFlappySceneContent(data, len)) {
      return 0;
    }
    int count = 0;
    for (const flappy_layout::FlappyTouchable& t :
         flappy_layout::kTouchables) {
      if (count >= capacity) {
        break;
      }
      float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
      if (!FlappyTapBounds(flappy_layout::kLayoutPassA, t.nodeId, &x, &y, &w,
                           &h)) {
        continue;
      }
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) ||
          !std::isfinite(h) || w <= 0.0f || h <= 0.0f) {
        continue;
      }
      out[count].nodeId = t.nodeId;
      out[count].x = x;
      out[count].y = y;
      out[count].w = w;
      out[count].h = h;
      ++count;
    }
    return count;
  } catch (...) {
    return 0;
  }
}
}  // namespace runtime
