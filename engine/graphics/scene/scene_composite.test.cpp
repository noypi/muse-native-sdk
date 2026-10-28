// T6 hermetic unit test — 3D output composites into the same DAG frame graph
// under engine VRAM budget and frame pacing (FR-GRAPHICS-009,
// AC-GRAPHICS-024/025). Pure logic: no Filament/AHardwareBuffer calls.
// Depends on the T5 gate: the unlinked case runs FIRST in a fresh process,
// then the test opts in via linkSceneModule and covers the composite paths.
// Style: plain asserts, no dependencies, no I/O, no network.
#include <cassert>
#include <cstdio>

#include "engine/graphics/scene/scene_composite.h"
#include "engine/graphics/scene/scene_module.h"

static SceneNode Node(int vramBytes, int deadlineMs, bool needsSwapchain) {
  SceneNode n;
  n.vramBytes = vramBytes;
  n.deadlineMs = deadlineMs;
  n.needsSwapchain = needsSwapchain;
  return n;
}

static FrameBudget Budget(int frameTimeMs, int vramHeadroomBytes) {
  FrameBudget b;
  b.frameTimeMs = frameTimeMs;
  b.vramHeadroomBytes = vramHeadroomBytes;
  return b;
}

static PoolState Pool(int cap, int used, int slots, int usedSlots) {
  PoolState p;
  p.capacityBytes = cap;
  p.usedBytes = used;
  p.ringSlots = slots;
  p.usedSlots = usedSlots;
  return p;
}

static bool SamePool(const PoolState& a, const PoolState& b) {
  return a.capacityBytes == b.capacityBytes && a.usedBytes == b.usedBytes &&
         a.ringSlots == b.ringSlots && a.usedSlots == b.usedSlots;
}

int main() {
  // 1. Unlinked module rejects every composite (T5 gate false).
  {
    CompositeResult r =
        compositeSceneNode(Node(64, 4, false), Budget(16, 1024), Pool(1024, 0, 8, 0));
    assert(r.status == Status::kNotLinked);
    assert(r.composited == false);
    assert(r.sameSwapchain == true);  // no separate swapchain ever exists
    printf("PASS unlinked-rejects-not-linked\n");
  }

  // Opt in once for the remaining cases.
  {
    SceneModuleConfig cfg;
    cfg.userRequested = true;
    cfg.gltfPath = "assets/test.glb";
    assert(linkSceneModule(cfg) == Status::kOk);
    assert(isSceneModuleLinked() == true);
  }

  // 2. Happy path: within VRAM headroom and frame time, same swapchain.
  {
    CompositeResult r =
        compositeSceneNode(Node(256, 8, false), Budget(16, 1024), Pool(2048, 512, 8, 2));
    assert(r.status == Status::kOk);
    assert(r.composited == true);
    assert(r.sameSwapchain == true);
    assert(r.withinBudget == true);
    printf("PASS happy-path-same-swapchain-within-budget\n");
  }

  // 3. Exact-fit boundary: vram == headroom and deadline == frame time.
  {
    CompositeResult r =
        compositeSceneNode(Node(1024, 16, false), Budget(16, 1024), Pool(2048, 512, 8, 2));
    assert(r.status == Status::kOk);
    assert(r.composited == true);
    assert(r.sameSwapchain == true);
    assert(r.withinBudget == true);
    printf("PASS exact-fit-boundary-composites\n");
  }

  // 4. Over VRAM headroom: kOverBudget, no composite, pool untouched.
  {
    PoolState before = Pool(2048, 512, 8, 2);
    PoolState pool = before;
    CompositeResult r =
        compositeSceneNode(Node(2048, 8, false), Budget(16, 1024), pool);
    assert(r.status == Status::kOverBudget);
    assert(r.composited == false);
    assert(r.withinBudget == false);
    assert(r.sameSwapchain == true);
    assert(SamePool(pool, before));  // rejected composite mutates no pool state
    printf("PASS over-vram-headroom-over-budget-no-pool-mutation\n");
  }

  // 5. Over frame time: kOverBudget, no composite.
  {
    CompositeResult r =
        compositeSceneNode(Node(128, 33, false), Budget(16, 4096), Pool(8192, 0, 8, 0));
    assert(r.status == Status::kOverBudget);
    assert(r.composited == false);
    assert(r.withinBudget == false);
    assert(r.sameSwapchain == true);
    printf("PASS over-frame-time-over-budget\n");
  }

  // 6. Separate swapchain request is rejected: kInvalidArg, no composite,
  //    but sameSwapchain still reports true (no separate swapchain exists).
  {
    CompositeResult r =
        compositeSceneNode(Node(128, 8, true), Budget(16, 4096), Pool(8192, 0, 8, 0));
    assert(r.status == Status::kInvalidArg);
    assert(r.composited == false);
    assert(r.sameSwapchain == true);
    printf("PASS separate-swapchain-invalid-arg\n");
  }

  // 7. Negative sizes are rejected: kInvalidArg, no composite.
  {
    CompositeResult r1 =
        compositeSceneNode(Node(-8, 8, false), Budget(16, 4096), Pool(8192, 0, 8, 0));
    assert(r1.status == Status::kInvalidArg);
    assert(r1.composited == false);
    assert(r1.sameSwapchain == true);
    CompositeResult r2 =
        compositeSceneNode(Node(128, -1, false), Budget(16, 4096), Pool(8192, 0, 8, 0));
    assert(r2.status == Status::kInvalidArg);
    assert(r2.composited == false);
    printf("PASS negative-sizes-invalid-arg\n");
  }

  printf("ALL T6 TESTS PASSED\n");
  return 0;
}
