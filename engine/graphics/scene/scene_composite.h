// Composite 3D output into the same DAG frame graph under engine VRAM budget
// and frame pacing (FR-GRAPHICS-009, AC-GRAPHICS-024/025). Pure logic: no
// Filament/AHardwareBuffer calls. VRAM headroom comes from the T4/NFR-003
// budget shape; the frame-time deadline mirrors the T1/T2 pacing contract.
#ifndef ENGINE_GRAPHICS_SCENE_SCENE_COMPOSITE_H_
#define ENGINE_GRAPHICS_SCENE_SCENE_COMPOSITE_H_

#include "engine/graphics/graphics_status.h"
#include "engine/graphics/scene/scene_module.h"
#include "engine/graphics/textures/texture_pool.h"

struct SceneNode {
  int vramBytes = 0;
  int deadlineMs = 0;
  bool needsSwapchain = false;
};

struct FrameBudget {
  int frameTimeMs = 0;
  int vramHeadroomBytes = 0;
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
