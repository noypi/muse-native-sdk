// T4 hermetic unit test — offscreen blur/filter graph (FR-GRAPHICS-002,
// FR-GRAPHICS-004, AC-GRAPHICS-003).
// Style: plain asserts, no dependencies, no I/O, no network.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "engine/graphics/color/filter_graph.h"

static filter_graph::PsoCache FullCache() {
  filter_graph::PsoCache c;
  c.handles[filter_graph::kBlurHKey] = 1101u;
  c.handles[filter_graph::kBlurVKey] = 1102u;
  c.handles[static_cast<int>(filter_graph::FilterPassType::kBrightness)] = 1201u;
  return c;
}

static filter_graph::FilterSpec Blur9Spec() {
  filter_graph::FilterSpec s;
  s.passes = {filter_graph::FilterPassType::kGaussBlur};
  s.blurRadius = 9.0f;
  s.extents = {1280u, 720u};
  return s;
}

int main() {
  // 1. Golden: {blur radius 9} yields exactly 2 ordered passes H then V with
  // bound PSO ids.
  {
    filter_graph::FilterPlan p =
        filter_graph::buildFilterGraph(Blur9Spec(), FullCache());
    assert(p.status == filter_graph::Status::kOk);
    assert(p.passes.size() == 2u);
    assert(p.passes[0].name == "blur_h");
    assert(p.passes[1].name == "blur_v");
    assert(p.passes[0].pso == 1101u);
    assert(p.passes[1].pso == 1102u);
    assert(p.passes[0].offscreen);
    assert(p.passes[1].offscreen);
    printf("PASS blur9-folds-to-ordered-h-then-v\n");
  }
  // 2. Unknown pass type yields kUnsupportedFilter.
  {
    filter_graph::FilterSpec s;
    s.passes = {filter_graph::FilterPassType::kUnknown};
    s.extents = {64u, 64u};
    filter_graph::FilterPlan p = filter_graph::buildFilterGraph(s, FullCache());
    assert(p.status == filter_graph::Status::kUnsupportedFilter);
    printf("PASS unknown-pass-unsupported-filter\n");
  }
  // 3. Missing PSO handle yields kMissingPso.
  {
    filter_graph::PsoCache empty;
    filter_graph::FilterPlan p = filter_graph::buildFilterGraph(Blur9Spec(), empty);
    assert(p.status == filter_graph::Status::kMissingPso);
    printf("PASS missing-pso-reported\n");
  }
  // 4. Brightness single pass binds its PSO id, offscreen only.
  {
    filter_graph::FilterSpec s;
    s.passes = {filter_graph::FilterPassType::kBrightness};
    s.extents = {64u, 64u};
    filter_graph::FilterPlan p = filter_graph::buildFilterGraph(s, FullCache());
    assert(p.status == filter_graph::Status::kOk);
    assert(p.passes.size() == 1u);
    assert(p.passes[0].name == "brightness");
    assert(p.passes[0].pso == 1201u);
    assert(p.passes[0].offscreen);
    printf("PASS brightness-single-pass-bound-pso\n");
  }
  // 5. Boundary: blur + brightness chains H, V, then brightness in order.
  {
    filter_graph::FilterSpec s;
    s.passes = {filter_graph::FilterPassType::kGaussBlur,
                filter_graph::FilterPassType::kBrightness};
    s.blurRadius = 9.0f;
    s.extents = {320u, 200u};
    filter_graph::FilterPlan p = filter_graph::buildFilterGraph(s, FullCache());
    assert(p.status == filter_graph::Status::kOk);
    assert(p.passes.size() == 3u);
    assert(p.passes[0].name == "blur_h");
    assert(p.passes[1].name == "blur_v");
    assert(p.passes[2].name == "brightness");
    printf("PASS blur-then-brightness-ordered\n");
  }
  // 6. Boundary: missing brightness PSO yields kMissingPso even when blur
  // PSOs are present.
  {
    filter_graph::PsoCache c;
    c.handles[filter_graph::kBlurHKey] = 1101u;
    c.handles[filter_graph::kBlurVKey] = 1102u;
    filter_graph::FilterSpec s;
    s.passes = {filter_graph::FilterPassType::kBrightness};
    s.extents = {64u, 64u};
    filter_graph::FilterPlan p = filter_graph::buildFilterGraph(s, c);
    assert(p.status == filter_graph::Status::kMissingPso);
    printf("PASS missing-brightness-pso-reported\n");
  }
  // 7. Security: >16 spec passes short-circuit with kUnsupportedFilter and
  // empty passes (bounds the 2x blur amplification).
  {
    filter_graph::FilterSpec s;
    s.passes = std::vector<filter_graph::FilterPassType>(
        17, filter_graph::FilterPassType::kBrightness);
    s.extents = {64u, 64u};
    filter_graph::FilterPlan p = filter_graph::buildFilterGraph(s, FullCache());
    assert(p.status == filter_graph::Status::kUnsupportedFilter);
    assert(p.passes.empty());
    printf("PASS oversize-pass-list-unsupported-filter\n");
  }
  printf("ALL T4 TESTS PASSED\n");
  return 0;
}
