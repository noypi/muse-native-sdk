// Same-graph scene composite under VRAM budget and frame pacing
// (FR-GRAPHICS-009). Pure logic, no Filament/AHardwareBuffer calls.
#include "engine/graphics/scene/scene_composite.h"

CompositeResult compositeSceneNode(const SceneNode& node,
                                   const FrameBudget& budget,
                                   const PoolState& pool) {
  (void)pool;  // Rejections never mutate pool state; admission is readout-only.
  CompositeResult out;
  out.composited = false;
  out.sameSwapchain = true;  // No separate swapchain ever exists.
  out.withinBudget = false;
  out.status = Status::kOk;
  if (!isSceneModuleLinked()) {
    out.status = Status::kNotLinked;
    return out;
  }
  if (node.needsSwapchain) {
    out.status = Status::kInvalidArg;
    return out;
  }
  if (node.vramBytes < 0 || node.deadlineMs < 0) {
    out.status = Status::kInvalidArg;
    return out;
  }
  if (node.vramBytes > budget.vramHeadroomBytes ||
      node.deadlineMs > budget.frameTimeMs) {
    out.status = Status::kOverBudget;
    out.withinBudget = false;
    return out;
  }
  out.composited = true;
  out.withinBudget = true;
  out.status = Status::kOk;
  return out;
}
