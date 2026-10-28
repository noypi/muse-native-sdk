#ifndef GRAPHICS_GRAPH_EQUIVALENCE_HARNESS_H_
#define GRAPHICS_GRAPH_EQUIVALENCE_HARNESS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "graph/render_graph.h"

namespace graphics {

// Opaque scene identifier; constructible from a string literal.
using SceneId = std::string;

// Golden pixel tolerance for visual equivalence (AC-007).
inline constexpr float kGoldenTolerance = 0.02f;

struct EquivalenceReport {
  uint32_t validation_errors = 0;
  float max_pixel_delta = 0.0f;
  std::vector<BackendKind> per_backend;
};

// Runs the validation-clean + visual-equivalence check for a scene across
// all three mocked backend paths. Hermetic: no GPU, no filesystem access.
// Unknown/unrunnable scenes return a non-passing report (never throws,
// never crashes).
EquivalenceReport run_equivalence(const SceneId& scene);

}  // namespace graphics

#endif  // GRAPHICS_GRAPH_EQUIVALENCE_HARNESS_H_
