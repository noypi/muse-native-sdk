// Offscreen multi-pass blur/filter graph (FR-GRAPHICS-002, FR-GRAPHICS-004,
// AC-GRAPHICS-003). Pure pass-ordering logic, no Vulkan calls.
#include "engine/graphics/color/filter_graph.h"

FilterPlan buildFilterGraph(const FilterSpec& spec, const PsoCache& psos) {
  FilterPlan plan;
  for (FilterPassType pass : spec.passes) {
    switch (pass) {
      case FilterPassType::kGaussBlur: {
        auto h = psos.handles.find(kBlurHKey);
        auto v = psos.handles.find(kBlurVKey);
        if (h == psos.handles.end() || v == psos.handles.end()) {
          plan.status = Status::kMissingPso;
          plan.passes.clear();
          return plan;
        }
        plan.passes.push_back(ComputePass{"blur_h", h->second, true});
        plan.passes.push_back(ComputePass{"blur_v", v->second, true});
        break;
      }
      case FilterPassType::kBrightness: {
        auto it = psos.handles.find(static_cast<int>(FilterPassType::kBrightness));
        if (it == psos.handles.end()) {
          plan.status = Status::kMissingPso;
          plan.passes.clear();
          return plan;
        }
        plan.passes.push_back(ComputePass{"brightness", it->second, true});
        break;
      }
      default:
        plan.status = Status::kUnsupportedFilter;
        plan.passes.clear();
        return plan;
    }
  }
  plan.status = Status::kOk;
  return plan;
}
