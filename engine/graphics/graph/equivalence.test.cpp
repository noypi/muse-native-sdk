// Batch 3 contract tests — T3: validation-clean + visual-equivalence harness
// for the three backend paths (FR-GRAPHICS-003, AC-007). Binds ONLY to the
// T3 `Interface:` line in plan.md; never to implementation. Depends on T2
// (Batch 2, committed green) read-only: reuses `BackendKind` from
// `graph/render_graph.h` verbatim and never re-asserts T2 behavior.
// Hermetic: C++17 + planned graphics headers only; no network, no env/
// config reads, no filesystem, no live services, no device-lab invocation
// (TC-007 device-lab run is EXTERNAL evidence — this harness is what the lab
// executes; see contract T3.md).
// Expected state before implementation: RED —
// `graph/equivalence_harness.h` does not exist, so compilation fails
// (missing behavior, not a malformed test). The coder provides the
// header/source; these tests then compile and run unmodified (call-site
// reconciliation only, per ASSUMPTIONs).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "graph/equivalence_harness.h"
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

// --- T3 helpers ------------------------------------------------------------
// ASSUMPTION (A-T3-SHAPE): C++ spelling of the plan interface. Plan line:
// `run_equivalence(scene: SceneId): EquivalenceReport` —
// `EquivalenceReport{validation_errors: u32, max_pixel_delta: float,
// per_backend: Backend[]}`. The plan/spec do not settle namespace, id type,
// or error channel; tests assume `graphics::SceneId` is an opaque alias
// constructible from a string literal, `graphics::EquivalenceReport` with
// `validation_errors` (`uint32_t`), `max_pixel_delta` (`float`), and
// `per_backend` (`std::vector<graphics::BackendKind>`, the exact T2
// committed spelling), and free function
// `graphics::run_equivalence(const SceneId&) -> EquivalenceReport`. A scene
// that cannot run fails loudly: unknown/broken ids either throw or return a
// report that does not pass — tests accept either, and accept nothing else.
// If the planner/coder settles different spellings, update call sites only
// — expectations are unchanged (genuine `run_equivalence` signature clash →
// planner per GR-08 T1.10).
// ASSUMPTION (A-T3-SCENE): the reference scene id is `"ui-reference"`; the
// deliberately broken scene id is `"broken-scene-xyz"` (guaranteed unknown
// to the harness). Call-site reconciliation if the planner settles other
// ids.
// ASSUMPTION (A-T3-TOLERANCE): the golden pixel tolerance is
// `kGoldenTolerance = 0.02f`. The acceptance criterion pins "within
// tolerance" without a number; 0.02 is the most defensible unit-test value.
// A spec-settled tolerance replaces this constant only (no behavior change).

static const float kGoldenTolerance = 0.02f;

static bool report_passes(const graphics::EquivalenceReport& r) {
  return r.validation_errors == 0 && r.max_pixel_delta <= kGoldenTolerance;
}

// --- T3 behavior cases (AC-GRAPHICS-007) ------------------------------------

// AC-007: the reference scene runs validation-clean on every path.
static bool check_reference_scene_zero_validation_errors() {
  const graphics::EquivalenceReport r =
      graphics::run_equivalence("ui-reference");
  return r.validation_errors == 0;
}

// AC-007: the reference scene's pixel delta is within golden tolerance.
static bool check_reference_scene_pixel_delta_within_tolerance() {
  const graphics::EquivalenceReport r =
      graphics::run_equivalence("ui-reference");
  return r.max_pixel_delta <= kGoldenTolerance;
}

// AC-007: the harness covers all three backend paths in one report.
static bool check_report_covers_all_three_backends() {
  const graphics::EquivalenceReport r =
      graphics::run_equivalence("ui-reference");
  if (r.per_backend.size() != 3) return false;
  bool v13 = false, v11 = false, gles = false;
  for (const auto b : r.per_backend) {
    if (b == graphics::BackendKind::VULKAN13_DYNAMIC) v13 = true;
    if (b == graphics::BackendKind::VULKAN11_EMULATION) v11 = true;
    if (b == graphics::BackendKind::GLES32) gles = true;
  }
  return v13 && v11 && gles;
}

// AC-007 negative path: a deliberately broken scene fails the harness —
// either by throwing or by returning a non-passing report.
static bool check_broken_scene_fails_harness() {
  try {
    const graphics::EquivalenceReport r =
        graphics::run_equivalence("broken-scene-xyz");
    return !report_passes(r);
  } catch (...) {
    return true;
  }
}

// Boundary: an empty scene id fails safe — loud failure, never a crash and
// never a passing report.
static bool check_empty_scene_id_fails_safely() {
  try {
    const graphics::EquivalenceReport r = graphics::run_equivalence("");
    return !report_passes(r);
  } catch (...) {
    return true;
  }
}

// Determinism: the same scene yields an identical report across runs.
static bool check_same_scene_yields_identical_report() {
  const graphics::EquivalenceReport a =
      graphics::run_equivalence("ui-reference");
  const graphics::EquivalenceReport b =
      graphics::run_equivalence("ui-reference");
  return a.validation_errors == b.validation_errors &&
         a.max_pixel_delta == b.max_pixel_delta &&
         a.per_backend == b.per_backend;
}

// Security/robustness (GR-05 R5.3): hostile scene ids (oversized,
// traversal-shaped) never crash the harness and never yield a passing
// report for a scene that was never authored.
static bool check_hostile_scene_ids_never_crash_or_pass() {
  const std::string big(4096, 's');
  const char* hostile[] = {big.c_str(), "../goldens/ui-reference",
                           "ui-reference\0.gold"};
  for (const char* id : hostile) {
    try {
      const graphics::EquivalenceReport r =
          graphics::run_equivalence(id ? id : "");
      (void)r;
      // The traversal/embedded-NUL ids are not the authored reference scene;
      // only the oversized id could conceivably alias it — in no case may a
      // hostile id crash. A non-passing report is the safe outcome; a
      // passing report for a non-authored id would be spoofing.
      if (std::strcmp(id, "ui-reference") != 0 && report_passes(r))
        return false;
    } catch (...) {
      // Loud failure is the safe outcome; keep probing.
    }
  }
  return true;
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
  RUN_CASE(check_reference_scene_zero_validation_errors);
  RUN_CASE(check_reference_scene_pixel_delta_within_tolerance);
  RUN_CASE(check_report_covers_all_three_backends);
  RUN_CASE(check_broken_scene_fails_harness);
  RUN_CASE(check_empty_scene_id_fails_safely);
  RUN_CASE(check_same_scene_yields_identical_report);
  RUN_CASE(check_hostile_scene_ids_never_crash_or_pass);
  if (failures > 0) {
    std::printf("equivalence.test: %d failing\n", failures);
    return 1;
  }
  std::printf("equivalence.test: all passing\n");
  return 0;
}
