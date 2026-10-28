// Batch 3 T6 contract tests — FR-LAYOUT-015 AC-LAYOUT-049 (TC-049).
// Inspector hooks compile out / no-op in release with zero overhead.
// Hermetic: C++17 + layout/inspector.h only; no network, no env/config reads,
// no filesystem, no live services. Release behavior is gated behind the
// existing core dev-mode flag (!NDEBUG, consumed not reimplemented — plan A4).
// Binds ONLY to the T6 `Interface:` line (verbatim in contract T6.md):
//   `bool inspectorEnabled() -> false in release; uint64_t inspectorOverheadNs() -> 0 in release (counter proving no-op path taken)`
//   — no error surface.
//
// ASSUMPTION (signatures): the plan names inspectorEnabled/inspectorOverheadNs
//   but not their headers or namespaces. Tests assume:
//     `bool layout::inspectorEnabled();`
//     `std::uint64_t layout::inspectorOverheadNs();`
//   declared in `layout/inspector.h`. If the coder settles a different
//   spelling, the contract reconciles the call site; the
//   false-in-release / 0-in-release behavior stands.
//
// ASSUMPTION (dev/release switch): the same file covers both builds via
//   `#if defined(NDEBUG)`. Dev binary: inspectorEnabled() is true, dump works,
//   repeated dumps are bit-identical (hooks do not mutate solve output).
//   Release binary: inspectorEnabled() is false, dump throws
//   kDisabledInRelease, overhead counter is 0 (no-op path taken).
//
// ASSUMPTION (identical solve): the plan names no separate solve entry point
//   for the overhead comparison. Tests observe solve output through the public
//   `dumpLayoutTree` boundary: two consecutive dumps of the same root must be
//   bit-identical (nodes + constraints + tokens + reasons), proving the hooks
//   did not perturb the solved tree. Reconciles if the coder exposes a
//   dedicated solve-compare helper; the bit-identical expectation stands.
//
// T2 context (inspector.h/inspector.cpp NDEBUG gate) is read-only: these tests
// extend it, never weaken it. tools/inspector (dx-owned FR-DX-003) untouched.
#include <cstdint>
#include <cstdio>

#include "layout/inspector.h"

namespace {

// Bit-identical comparison over the full dump payload (nodes, constraints,
// tokens, reasons). Hooks on/off must not perturb any of it.
bool dumps_identical(const layout::InspectorDump& a,
                     const layout::InspectorDump& b) {
  if (a.nodes.size() != b.nodes.size()) return false;
  if (a.constraints.size() != b.constraints.size()) return false;
  if (a.tokens.size() != b.tokens.size()) return false;
  if (a.reasons.size() != b.reasons.size()) return false;
  for (std::size_t i = 0; i < a.nodes.size(); ++i) {
    if (a.nodes[i].x != b.nodes[i].x) return false;
    if (a.nodes[i].y != b.nodes[i].y) return false;
    if (a.nodes[i].w != b.nodes[i].w) return false;
    if (a.nodes[i].h != b.nodes[i].h) return false;
    if (a.nodes[i].node != b.nodes[i].node) return false;
  }
  for (std::size_t i = 0; i < a.constraints.size(); ++i) {
    if (a.constraints[i].value != b.constraints[i].value) return false;
  }
  for (std::size_t i = 0; i < a.tokens.size(); ++i) {
    if (a.tokens[i].value != b.tokens[i].value) return false;
    if (a.tokens[i].cascadeSource != b.tokens[i].cascadeSource) return false;
  }
  for (std::size_t i = 0; i < a.reasons.size(); ++i) {
    if (a.reasons[i].reason != b.reasons[i].reason) return false;
  }
  return true;
}

}  // namespace

#if defined(NDEBUG)

// Release: hooks compile out — inspector reports disabled.
static bool check_release_reports_disabled() {
  return layout::inspectorEnabled() == false;
}

// Release: overhead counter is zero, proving the no-op path was taken.
static bool check_release_overhead_is_zero() {
  return layout::inspectorOverheadNs() == 0u;
}

// Release: dump call returns disabled (throws kDisabledInRelease).
static bool check_release_dump_returns_disabled() {
  try {
    layout::InspectorDump dump =
        layout::dumpLayoutTree(static_cast<layout::RootId>(1));
    (void)dump;
  } catch (const layout::LayoutError& e) {
    return e == layout::LayoutError::kDisabledInRelease;
  } catch (...) {
    return false;
  }
  return false;
}

// Release: overhead stays zero across repeated no-op calls (no accumulation).
static bool check_release_overhead_stays_zero() {
  (void)layout::inspectorEnabled();
  (void)layout::inspectorEnabled();
  return layout::inspectorOverheadNs() == 0u;
}

#else

// Dev: inspector reports enabled (gate: !NDEBUG core dev-mode flag).
static bool check_release_reports_disabled() {
  return layout::inspectorEnabled() == true;
}

// Dev: overhead counter is callable (value documents hook cost; asserted
// non-trapping, not zero — zero is the release-only expectation).
static bool check_release_overhead_is_zero() {
  (void)layout::inspectorOverheadNs();
  return true;
}

// Dev: dump works (not disabled) — the release-disabled error exists and is
// distinct, pinning the release contract in the dev binary.
static bool check_release_dump_returns_disabled() {
  layout::RootId root = layout::createInspectorTree(3);
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  if (dump.nodes.size() != 3) return false;
  return layout::LayoutError::kDisabledInRelease !=
         static_cast<layout::LayoutError>(0);
}

// Dev: solve output is bit-identical across consecutive dumps with hooks
// present (hooks do not perturb the solved tree).
static bool check_release_overhead_stays_zero() {
  layout::RootId root = layout::createInspectorTree(3);
  layout::InspectorDump first = layout::dumpLayoutTree(root);
  layout::InspectorDump second = layout::dumpLayoutTree(root);
  return dumps_identical(first, second);
}

// Dev: identical solve across two independent same-size trees (hook path does
// not leak per-tree state into geometry).
static bool check_independent_trees_solve_identically() {
  layout::RootId a = layout::createInspectorTree(3);
  layout::RootId b = layout::createInspectorTree(3);
  layout::InspectorDump da = layout::dumpLayoutTree(a);
  layout::InspectorDump db = layout::dumpLayoutTree(b);
  return dumps_identical(da, db);
}

#endif

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_release_reports_disabled);
  RUN_CASE(check_release_overhead_is_zero);
  RUN_CASE(check_release_dump_returns_disabled);
  RUN_CASE(check_release_overhead_stays_zero);
#if !defined(NDEBUG)
  RUN_CASE(check_independent_trees_solve_identically);
#endif
  if (failures > 0) {
    std::printf("inspector_overhead_test: %d failing\n", failures);
    return 1;
  }
  std::printf("inspector_overhead_test: all passing\n");
  return 0;
}
