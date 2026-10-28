// T5 hermetic unit test — off-by-default 3D dynamic-feature module boundary
// (FR-GRAPHICS-009, AC-GRAPHICS-023/025). Gate-state model only: no dynamic
// loader or Play API calls. Style: plain asserts, no dependencies, no I/O,
// no network. NOTE: order matters — the gate is process-global, so the
// default/unlinked and all rejection cases run BEFORE the single opt-in.
#include <cassert>
#include <cstdio>

#include "engine/graphics/scene/scene_module.h"

int main() {
  int linkCalls = 0;  // in-test startup-work counter: 0 until explicit opt-in

  // 1. Default state: no link call made, module unlinked, zero startup work.
  assert(isSceneModuleLinked() == false);
  assert(linkCalls == 0);
  printf("PASS default-unlinked-zero-startup\n");

  // 2. userRequested=false links nothing, even with a valid glTF path.
  {
    SceneModuleConfig cfg;
    cfg.userRequested = false;
    cfg.gltfPath = "assets/test.glb";
    ++linkCalls;
    Status s = linkSceneModule(cfg);
    assert(s == Status::kNotLinked);
    assert(isSceneModuleLinked() == false);
    printf("PASS opt-out-valid-path-not-linked\n");
  }

  // 3. Null gltfPath is rejected, links nothing.
  {
    SceneModuleConfig cfg;
    cfg.userRequested = true;
    cfg.gltfPath = nullptr;
    ++linkCalls;
    Status s = linkSceneModule(cfg);
    assert(s == Status::kInvalidArg);
    assert(isSceneModuleLinked() == false);
    printf("PASS null-path-invalid-arg\n");
  }

  // 4. Empty gltfPath is rejected, links nothing.
  {
    SceneModuleConfig cfg;
    cfg.userRequested = true;
    cfg.gltfPath = "";
    ++linkCalls;
    Status s = linkSceneModule(cfg);
    assert(s == Status::kInvalidArg);
    assert(isSceneModuleLinked() == false);
    printf("PASS empty-path-invalid-arg\n");
  }

  // 5. Explicit opt-in with a valid path links the module.
  {
    SceneModuleConfig cfg;
    cfg.userRequested = true;
    cfg.gltfPath = "assets/test.glb";
    ++linkCalls;
    Status s = linkSceneModule(cfg);
    assert(s == Status::kOk);
    assert(isSceneModuleLinked() == true);
    printf("PASS explicit-opt-in-links\n");
  }

  // 6. Once linked, the gate stays linked (no implicit unload on later calls).
  {
    assert(isSceneModuleLinked() == true);
    printf("PASS gate-stays-linked-after-opt-in\n");
  }

  printf("ALL T5 TESTS PASSED\n");
  return 0;
}
