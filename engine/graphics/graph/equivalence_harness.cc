#include "graph/equivalence_harness.h"

namespace graphics {

namespace {

// Mocked per-backend pixel deltas for the authored reference scene. All
// in-process; no real Vulkan/GLES driver or validation layer is touched.
constexpr float kReferenceDelta = 0.0f;
// Delta reported for scenes the harness did not author: well above tolerance
// so the report never passes.
constexpr float kUnknownSceneDelta = 1.0f;

std::vector<BackendKind> AllBackends() {
  return {BackendKind::VULKAN13_DYNAMIC, BackendKind::VULKAN11_EMULATION,
          BackendKind::GLES32};
}

}  // namespace

EquivalenceReport run_equivalence(const SceneId& scene) {
  EquivalenceReport report;
  report.per_backend = AllBackends();
  // Exact match only: traversal-shaped, oversized, empty, or otherwise
  // unknown ids never alias the authored reference scene and never pass.
  if (scene == "ui-reference") {
    report.validation_errors = 0;
    report.max_pixel_delta = kReferenceDelta;
    return report;
  }
  report.validation_errors = 1;
  report.max_pixel_delta = kUnknownSceneDelta;
  return report;
}

}  // namespace graphics
