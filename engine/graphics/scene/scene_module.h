// Off-by-default 3D dynamic-feature module boundary (FR-GRAPHICS-009).
// Gate-state model only: no dynamic-loader or Play API calls. The module
// starts unlinked and links only on explicit user opt-in with a valid
// glTF path; all other inputs leave the gate unlinked.
#ifndef ENGINE_GRAPHICS_SCENE_SCENE_MODULE_H_
#define ENGINE_GRAPHICS_SCENE_SCENE_MODULE_H_

enum class Status { kOk, kNotLinked, kInvalidArg };

struct SceneModuleConfig {
  bool userRequested = false;
  const char* gltfPath = nullptr;
};

// Reports whether the scene module is currently linked.
bool isSceneModuleLinked();

// Attempts to link the scene module. Returns kOk and links on explicit
// opt-in (userRequested=true) with a valid non-empty gltfPath; returns
// kNotLinked and links nothing when userRequested=false; returns
// kInvalidArg and links nothing when gltfPath is null or empty.
Status linkSceneModule(const SceneModuleConfig& cfg);

#endif  // ENGINE_GRAPHICS_SCENE_SCENE_MODULE_H_
