// Offscreen multi-pass blur/filter graph (FR-GRAPHICS-002, FR-GRAPHICS-004,
// AC-GRAPHICS-003). Pure pass-ordering logic: folds each GAUSS_BLUR spec pass
// into separable H+V compute passes, binds pre-compiled PSO ids from the
// injected FR-GRAPHICS-006 cache. Offscreen passes only, no Vulkan calls.
#ifndef ENGINE_GRAPHICS_COLOR_FILTER_GRAPH_H_
#define ENGINE_GRAPHICS_COLOR_FILTER_GRAPH_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace filter_graph {

enum class Status { kOk = 0, kUnsupportedFilter = 1, kMissingPso = 2 };

enum class FilterPassType {
  kGaussBlur = 0,
  kBrightness = 1,
  kUnknown = 999,
};

struct Extents {
  uint32_t width = 0;
  uint32_t height = 0;
};

struct FilterSpec {
  std::vector<FilterPassType> passes;
  float blurRadius = 0.0f;
  Extents extents;
};

// Injected pre-compiled PSO cache (FR-GRAPHICS-006 interface reused):
// key = static_cast<int>(FilterPassType) for single-pass kinds; separable
// blur uses kBlurHKey/kBlurVKey.
static const int kBlurHKey = 100;
static const int kBlurVKey = 101;

struct PsoCache {
  std::map<int, uint64_t> handles;  // key -> pre-compiled PSO id.
};

struct ComputePass {
  std::string name;  // "blur_h", "blur_v", "brightness".
  uint64_t pso = 0;  // Bound pre-compiled PSO id.
  bool offscreen = true;
};

struct FilterPlan {
  Status status = Status::kOk;
  std::vector<ComputePass> passes;
};

// Builds the ordered offscreen compute-pass list for spec, binding each
// pass's pre-compiled PSO id from psos. Unknown pass type short-circuits
// with Status::kUnsupportedFilter; absent PSO entry short-circuits with
// Status::kMissingPso (no partial passes returned on error).
FilterPlan buildFilterGraph(const FilterSpec& spec, const PsoCache& psos);

}  // namespace filter_graph

#endif  // ENGINE_GRAPHICS_COLOR_FILTER_GRAPH_H_
