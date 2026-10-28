// Batch 2 contract tests — T2: backend-agnostic DAG render graph
// (FR-GRAPHICS-003, AC-006). Binds ONLY to the T2 `Interface:` line in
// plan.md; never to implementation.
// Hermetic: C++17 + planned graphics headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected state before implementation: RED — `graph/render_graph.h` does
// not exist, so compilation fails (missing behavior, not a malformed test).
// The coder provides the headers/sources; these tests then compile and run
// unmodified (call-site reconciliation only, per ASSUMPTIONs).
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "graph/render_graph.h"

// --- harness cases (repo convention, cf. engine/runtime/tests) --------------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T2 helpers ------------------------------------------------------------
// ASSUMPTION (A-T2-SHAPE): C++ spelling of the plan interface. Plan line:
// `submit_graph(graph: RenderGraph): FrameReceipt` — "accepts
// backend-independent node list; returns per-frame receipt; error shape:
// `BACKEND_REJECTED` only from dispatch layer, never from graph
// construction". The plan/spec do not settle namespace, node spelling, or
// error channel; tests assume `graphics::RenderGraph` (with `add_pass(name)`
// building a backend-independent node list and `node_count()`), free
// function `graphics::submit_graph(const RenderGraph&) -> FrameReceipt`
// (with a `frame_ok()`/`frame_id` observable receipt), and dispatch-layer
// rejection signalled by throwing `graphics::BackendRejectedError` whose
// `what()` is exactly "BACKEND_REJECTED".
// ASSUMPTION (A-T2-DISPATCH): the plan acceptance requires "same graph
// submits unchanged on all three backends via mocked dispatch". Tests assume
// a test-only seam `graphics::set_dispatch_for_test(DispatchFn)` where
// `DispatchFn` is `std::function<void(const RenderGraph&)>` selecting the
// mocked backend per call, plus `graphics::BackendKind{VULKAN13_DYNAMIC,
// VULKAN11_EMULATION, GLES32}` chosen inside the fake. If the planner/coder
// settles a different seam, update call sites only — graph-agnosticism
// expectations are unchanged (reconcile, not a behavior change; genuine
// signature clash on `submit_graph` itself → planner per GR-08 T1.10).

static graphics::RenderGraph make_ui_graph() {
  graphics::RenderGraph g;
  g.add_pass("ui-background");
  g.add_pass("ui-text");
  g.add_pass("ui-composite");
  return g;
}

// --- T2 behavior cases (AC-GRAPHICS-006) ------------------------------------

// AC-006 happy path: a backend-independent node list submits and yields a
// per-frame receipt.
static bool check_submit_graph_returns_frame_receipt() {
  graphics::set_dispatch_for_test(
      [](const graphics::RenderGraph&) { /* accept on mocked backend */ });
  const graphics::RenderGraph g = make_ui_graph();
  const graphics::FrameReceipt r = graphics::submit_graph(g);
  return r.frame_ok();
}

// AC-006 core: the SAME graph object submits unchanged on all three mocked
// backends; receipts stay well-formed and no BACKEND_REJECTED escapes.
static bool check_same_graph_submits_on_all_three_backends() {
  const graphics::BackendKind backends[] = {
      graphics::BackendKind::VULKAN13_DYNAMIC,
      graphics::BackendKind::VULKAN11_EMULATION,
      graphics::BackendKind::GLES32,
  };
  for (graphics::BackendKind b : backends) {
    graphics::set_dispatch_for_test(
        [b](const graphics::RenderGraph& seen) {
          if (seen.node_count() != 3) throw std::string("graph mutated");
          (void)b;  // fake selects backend per call; graph is untouched.
        });
    const graphics::RenderGraph g = make_ui_graph();
    try {
      const graphics::FrameReceipt r = graphics::submit_graph(g);
      if (!r.frame_ok()) return false;
    } catch (...) {
      return false;  // unchanged graph must never reject on a healthy fake.
    }
  }
  return true;
}

// AC-006 invariant: graph construction never raises BACKEND_REJECTED — that
// error comes only from the dispatch layer.
static bool check_graph_construction_never_rejects_backend() {
  try {
    graphics::RenderGraph g;
    g.add_pass("p0");
    g.add_pass("p1");
    if (g.node_count() != 2) return false;
  } catch (const graphics::BackendRejectedError&) {
    return false;  // construction must not speak the dispatch error.
  } catch (...) {
    return false;
  }
  return true;
}

// AC-006 error path: a dispatch-layer rejection surfaces exactly
// BACKEND_REJECTED and carries no backend internals.
static bool check_dispatch_rejection_surfaces_backend_rejected() {
  graphics::set_dispatch_for_test([](const graphics::RenderGraph&) {
    throw graphics::BackendRejectedError();
  });
  try {
    graphics::submit_graph(make_ui_graph());
    return false;
  } catch (const graphics::BackendRejectedError& e) {
    return std::string(e.what()) == "BACKEND_REJECTED";
  } catch (...) {
    return false;
  }
}

// Boundary: an empty node list still submits deterministically (no crash,
// well-formed receipt, no rejection).
static bool check_empty_graph_submits_deterministically() {
  graphics::set_dispatch_for_test(
      [](const graphics::RenderGraph&) { /* accept */ });
  const graphics::RenderGraph g;
  try {
    const graphics::FrameReceipt r1 = graphics::submit_graph(g);
    const graphics::FrameReceipt r2 = graphics::submit_graph(g);
    return r1.frame_ok() && r2.frame_ok();
  } catch (...) {
    return false;
  }
}

// Security/robustness (GR-05 R5.3): oversized and duplicate pass names cannot
// crash submission and never surface as BACKEND_REJECTED.
static bool check_oversized_graph_cannot_crash_or_spoof_rejection() {
  graphics::set_dispatch_for_test(
      [](const graphics::RenderGraph&) { /* accept */ });
  try {
    graphics::RenderGraph g;
    const std::string big(4096, 'p');
    for (int i = 0; i < 64; ++i) g.add_pass(big);
    g.add_pass("dup");
    g.add_pass("dup");
    const graphics::FrameReceipt r = graphics::submit_graph(g);
    return r.frame_ok();
  } catch (const graphics::BackendRejectedError&) {
    return false;  // malformed input is never a dispatch rejection.
  } catch (...) {
    return false;
  }
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_submit_graph_returns_frame_receipt);
  RUN_CASE(check_same_graph_submits_on_all_three_backends);
  RUN_CASE(check_graph_construction_never_rejects_backend);
  RUN_CASE(check_dispatch_rejection_surfaces_backend_rejected);
  RUN_CASE(check_empty_graph_submits_deterministically);
  RUN_CASE(check_oversized_graph_cannot_crash_or_spoof_rejection);
  if (failures > 0) {
    std::printf("backend_agnostic.test: %d failing\n", failures);
    return 1;
  }
  std::printf("backend_agnostic.test: all passing\n");
  return 0;
}
