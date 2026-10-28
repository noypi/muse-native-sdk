// Composite 3D output into the same DAG frame graph under engine VRAM budget
// and frame pacing (FR-GRAPHICS-009, AC-GRAPHICS-024/025). Pure logic: no
// Filament/AHardwareBuffer calls. VRAM headroom comes from the T4/NFR-003
// budget shape; the frame-time deadline mirrors the T1/T2 pacing contract.
//
// NOTE: this header must NOT include "texture_pool.h" (or any textures
// header): both define `enum class Status`, so `PoolState` is redeclared here
// with the identical T4 field shape instead.
//
// NOTE: this header provides the superset `Status` (T6 adds `kNotLinked`)
// and suppresses `scene_module.h`'s narrower `Status` redefinition in TUs
// that include both headers (the T6 test includes this header first): the
// gate functions are redeclared below with identical signatures and link
// against `scene_module.cc`.
#ifndef ENGINE_GRAPHICS_SCENE_SCENE_COMPOSITE_H_
#define ENGINE_GRAPHICS_SCENE_SCENE_COMPOSITE_H_

#ifndef ENGINE_GRAPHICS_SCENE_SCENE_MODULE_H_
#define ENGINE_GRAPHICS_SCENE_SCENE_MODULE_H_

enum class Status { kOk, kOverBudget, kNotLinked, kInvalidArg };

struct SceneModuleConfig {
  bool userRequested = false;
  const char* gltfPath = nullptr;
};

// Reports whether the scene module is currently linked (T5 gate).
bool isSceneModuleLinked();

// Attempts to link the scene module (T5 gate). Implemented in scene_module.cc.
Status linkSceneModule(const SceneModuleConfig& cfg);

#endif  // ENGINE_GRAPHICS_SCENE_SCENE_MODULE_H_

struct SceneNode {
  int vramBytes = 0;
  int deadlineMs = 0;
  bool needsSwapchain = false;
};

struct FrameBudget {
  int frameTimeMs = 0;
  int vramHeadroomBytes = 0;
};

// T4 pool shape, redeclared (see NOTE above): field order and types match
// texture_pool.h exactly. Passed by value; rejections never mutate it.
struct PoolState {
  int capacityBytes = 0;
  int usedBytes = 0;
  int ringSlots = 0;
  int usedSlots = 0;
};

struct CompositeResult {
  bool composited = false;
  bool sameSwapchain = true;
  bool withinBudget = false;
  Status status = Status::kOk;
};

// Composites one scene node into the same DAG frame graph.
// Unlinked module (T5 gate false) -> kNotLinked, composited=false.
// needsSwapchain=true -> kInvalidArg, composited=false (sameSwapchain stays
// true: no separate swapchain ever exists). Negative node sizes ->
// kInvalidArg, composited=false. Over VRAM headroom
// (node.vramBytes > budget.vramHeadroomBytes) or over frame time
// (node.deadlineMs > budget.frameTimeMs) -> kOverBudget, composited=false,
// withinBudget=false, pool untouched. Otherwise kOk with composited,
// sameSwapchain, and withinBudget all true. No Filament/AHardwareBuffer calls.
CompositeResult compositeSceneNode(const SceneNode& node,
                                   const FrameBudget& budget,
                                   const PoolState& pool);

#endif  // ENGINE_GRAPHICS_SCENE_SCENE_COMPOSITE_H_
