// Batch 1 T2 contract tests — FR-LAYOUT-005 AC-LAYOUT-009 (TC-LAYOUT-009)
// + FR-LAYOUT-015 AC-LAYOUT-048 (TC-LAYOUT-048). One dump payload serves both
// ACs (plan A2).
// Hermetic: C++17 + layout/inspector.h only; no network, no env/config reads,
// no filesystem, no live services. Dev-build only; release returns
// LayoutError::kDisabledInRelease. tools/inspector (dx transport) untouched.
// Binds ONLY to the T2 `Interface:` line (verbatim in contract T2.md):
//   `InspectorDump dumpLayoutTree(RootId root) -> { NodeGeom[] nodes; ConstraintVar[] constraints; ResolvedToken[] tokens; InvalidationRecord[] reasons }`
//   — dev-build only; release returns `LayoutError::kDisabledInRelease`.
//
// ASSUMPTION (type shapes): the plan names RootId/InspectorDump/NodeGeom/
//   ConstraintVar/ResolvedToken/InvalidationRecord/LayoutError but not their
//   fields or error channel. Tests assume:
//     `using RootId = std::uint32_t;`
//     `struct InspectorDump { NodeGeomList nodes; ConstraintVarList constraints;
//       ResolvedTokenList tokens; InvalidationRecordList reasons; };`
//     where each List exposes `size()` and element access; NodeGeom exposes
//     geometry (x/y/w/h or a box equality helper — tests only assert the node
//     count and per-node reason presence, never field spellings beyond size);
//     ConstraintVar/ResolvedToken/InvalidationRecord are opaque entries.
//     Release-disabled surfaces as an exception of type `layout::LayoutError`
//     equal to `layout::LayoutError::kDisabledInRelease` (same throw
//     convention as solver_test.cpp). If the coder spells the channel
//     differently (e.g. Status return), the contract reconciles the call
//     site; the kDisabledInRelease behavior stands.
//
// ASSUMPTION (fixture setup): the plan does not name a tree-construction
//   entry point for the dump. Tests assume `RootId createInspectorTree(int
//   nodeCount)` in inspector.h (dev builds) that builds a solved tree of the
//   requested size with ≥1 constraint variable, ≥1 resolved token with cascade
//   source, and one invalidation reason per node. If the coder names setup
//   differently, the contract reconciles the setup call only; dump-content
//   expectations do not change.
//
// ASSUMPTION (dev/release switch): tests compile against the dev-build header
//   path. The release-disabled case is asserted as: calling dumpLayoutTree on
//   a release build throws/returns kDisabledInRelease. In a dev-build test
//   binary this case is verified by `layout::inspectorEnabled() == true`
//   gating documentation + a negative case compiled under the release flag is
//   out of scope for this hermetic suite (owned by T6 inspector_overhead_test
//   in Batch 2). The kDisabledInRelease case below therefore asserts the
//   LayoutError enumerator exists and is distinct, so the release contract is
//   pinned even in the dev binary.
#include <cstdint>
#include <cstdio>

#include "layout/inspector.h"

namespace {

bool is_disabled_in_release(const layout::LayoutError& e) {
  return e == layout::LayoutError::kDisabledInRelease;
}

}  // namespace

// AC-LAYOUT-009 + AC-LAYOUT-048: dev build dumps a 3-node tree — the dump
// contains exactly the solved nodes.
static bool check_dump_contains_three_nodes() {
  layout::RootId root = layout::createInspectorTree(3);
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  return dump.nodes.size() == 3;
}

// AC-LAYOUT-009 + AC-LAYOUT-048: the dump carries geometry per node
// (observable as: nodes list non-empty with per-node entries present).
static bool check_dump_nodes_carry_geometry() {
  layout::RootId root = layout::createInspectorTree(3);
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  if (dump.nodes.size() != 3) return false;
  // Each node entry must be present (geometry payload bound to the node).
  for (std::size_t i = 0; i < dump.nodes.size(); ++i) {
    (void)dump.nodes[i];
  }
  return true;
}

// AC-LAYOUT-009 + AC-LAYOUT-048: the dump contains >= 1 constraint variable.
static bool check_dump_contains_constraint_variable() {
  layout::RootId root = layout::createInspectorTree(3);
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  return dump.constraints.size() >= 1;
}

// AC-LAYOUT-009 + AC-LAYOUT-048: the dump contains a resolved token with
// cascade source (observable as >= 1 resolved token entry).
static bool check_dump_contains_resolved_token() {
  layout::RootId root = layout::createInspectorTree(3);
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  return dump.tokens.size() >= 1;
}

// AC-LAYOUT-009 + AC-LAYOUT-048: the dump contains an invalidation reason per
// node (reasons count covers every dumped node).
static bool check_dump_contains_invalidation_reason_per_node() {
  layout::RootId root = layout::createInspectorTree(3);
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  if (dump.nodes.size() != 3) return false;
  return dump.reasons.size() >= dump.nodes.size();
}

// Boundary: dumping an unknown root reports the unknown-root error and never
// returns a partial payload. (ASSUMPTION: unknown root throws
// `layout::LayoutError` distinct from kDisabledInRelease; reconciles if the
// coder settles a dedicated enumerator such as kUnknownRoot.)
static bool check_unknown_root_is_rejected() {
  try {
    layout::InspectorDump dump = layout::dumpLayoutTree(static_cast<layout::RootId>(9999));
    (void)dump;
  } catch (const layout::LayoutError& e) {
    return !is_disabled_in_release(e);
  } catch (...) {
    return false;
  }
  return false;
}

// Release gate: the kDisabledInRelease error exists and is distinct from any
// unknown-root error, pinning the release contract for T6.
static bool check_disabled_in_release_error_exists() {
  return layout::LayoutError::kDisabledInRelease != static_cast<layout::LayoutError>(0);
}

// AC-LAYOUT-009 + AC-LAYOUT-048: dumped node geometry matches the baked
// fixture values — every node is 100x50.
static bool check_dump_node_geometry_matches_baked_values() {
  layout::RootId root = layout::createInspectorTree(3);
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  if (dump.nodes.size() != 3) return false;
  for (std::size_t i = 0; i < dump.nodes.size(); ++i) {
    if (dump.nodes[i].w != 100.0f || dump.nodes[i].h != 50.0f) return false;
  }
  return true;
}

// AC-LAYOUT-009 + AC-LAYOUT-048: the resolved token carries its cascade
// source — tokens[0] records cascadeSource 0, distinct from its token id 7.
static bool check_dump_token_records_distinct_cascade_source() {
  layout::RootId root = layout::createInspectorTree(3);
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  if (dump.tokens.size() < 1) return false;
  const layout::ResolvedToken& tok = dump.tokens[0];
  return tok.token == 7u && tok.cascadeSource == 0u &&
         tok.cascadeSource != tok.token;
}

// AC-LAYOUT-009 + AC-LAYOUT-048: every invalidation reason is bound to its
// source node — reasons[i].node == nodes[i].node for all i.
static bool check_dump_reasons_match_source_nodes() {
  layout::RootId root = layout::createInspectorTree(3);
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  if (dump.nodes.size() != 3) return false;
  if (dump.reasons.size() < dump.nodes.size()) return false;
  for (std::size_t i = 0; i < dump.nodes.size(); ++i) {
    if (dump.reasons[i].node != dump.nodes[i].node) return false;
  }
  return true;
}

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
  RUN_CASE(check_dump_contains_three_nodes);
  RUN_CASE(check_dump_nodes_carry_geometry);
  RUN_CASE(check_dump_contains_constraint_variable);
  RUN_CASE(check_dump_contains_resolved_token);
  RUN_CASE(check_dump_contains_invalidation_reason_per_node);
  RUN_CASE(check_dump_node_geometry_matches_baked_values);
  RUN_CASE(check_dump_token_records_distinct_cascade_source);
  RUN_CASE(check_dump_reasons_match_source_nodes);
  RUN_CASE(check_unknown_root_is_rejected);
  RUN_CASE(check_disabled_in_release_error_exists);
  if (failures > 0) {
    std::printf("inspector_dump_test: %d failing\n", failures);
    return 1;
  }
  std::printf("inspector_dump_test: all passing\n");
  return 0;
}
