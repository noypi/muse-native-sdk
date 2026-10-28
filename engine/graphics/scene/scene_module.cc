#include "engine/graphics/scene/scene_module.h"

namespace {

// Process-global gate state. Starts unlinked; once linked it stays linked
// (no implicit unload). No dynamic-loader or Play API calls.
bool g_scene_module_linked = false;

}  // namespace

bool isSceneModuleLinked() { return g_scene_module_linked; }

Status linkSceneModule(const SceneModuleConfig& cfg) {
  if (cfg.gltfPath == nullptr || cfg.gltfPath[0] == '\0') {
    return Status::kInvalidArg;
  }
  if (!cfg.userRequested) {
    return Status::kNotLinked;
  }
  g_scene_module_linked = true;
  return Status::kOk;
}
