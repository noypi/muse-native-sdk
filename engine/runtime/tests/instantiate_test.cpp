// T2 contract tests — FR-RUNTIME-001 / AC-RUNTIME-001 (TC-RUNTIME-001).
// Extends the T1 skeleton: the three harness cases are preserved verbatim.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Binds ONLY to the T2 `Interface:` line; never to implementation.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "runtime/tree.h"

// --- T1 harness cases (preserved; do not alter) ----------------------------

// Named case: the harness executable entry point exists and runs.
static bool check_harness_entry_reached() { return true; }

// Named case: pass protocol reserves return 0 for all-green.
static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

// Named case: failure protocol reserves non-zero + failing name on stdout.
static bool check_failure_protocol_shape() {
  // Protocol shape only: a failing case must be nameable on stdout.
  // This skeleton has no failing case; assert the naming slot exists.
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T2 helpers ------------------------------------------------------------

namespace {

// 3-level binary tree fixture: root(0) -> {1, 2}; 1 -> {3, 4}; 2 -> {5, 6}.
// `parent` is the parent index in the input array, -1 for the root.
void make_binary_tree(runtime::CompiledNode* nodes) {
  const int parents[7] = {-1, 0, 0, 1, 1, 2, 2};
  for (int i = 0; i < 7; ++i) {
    nodes[i].parent = parents[i];
    nodes[i].tag = 100 + i;
  }
}

}  // namespace

// Named case: a 3-level binary tree instantiates with every node reachable
// by a stable id (AC-RUNTIME-001 happy path).
static bool check_binary_tree_instantiates_with_stable_ids() {
  runtime::CompiledNode nodes[7];
  make_binary_tree(nodes);
  runtime::RuntimeTree tree;
  runtime::NodeId root = runtime::instantiate(nodes, 7, tree);
  if (root != 0) return false;
  if (tree.size() != 7) return false;
  for (runtime::NodeId id = 0; id < 7; ++id) {
    const runtime::Node& node = tree.get(id);
    if (node.id != id) return false;
  }
  if (tree.get(1).parent != 0) return false;
  if (tree.get(2).parent != 0) return false;
  if (tree.get(3).parent != 1) return false;
  if (tree.get(5).parent != 2) return false;
  if (tree.get(0).tag != 100) return false;
  if (tree.get(6).tag != 106) return false;
  return true;
}

// Named case: re-running the update pass leaves all ids unchanged
// (AC-RUNTIME-001 stability half).
static bool check_ids_stable_across_update_pass() {
  runtime::CompiledNode nodes[7];
  make_binary_tree(nodes);
  runtime::RuntimeTree tree;
  runtime::NodeId root1 = runtime::instantiate(nodes, 7, tree);
  if (root1 != 0) return false;
  runtime::NodeId root2 = runtime::instantiate(nodes, 7, tree);
  if (root2 != root1) return false;
  if (tree.size() != 7) return false;
  for (runtime::NodeId id = 0; id < 7; ++id) {
    if (tree.get(id).id != id) return false;
  }
  return true;
}

// Named case: get on an unknown id throws std::out_of_range.
static bool check_get_unknown_id_throws() {
  runtime::CompiledNode nodes[7];
  make_binary_tree(nodes);
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 7, tree) != 0) return false;
  bool threw_invalid = false;
  try {
    tree.get(runtime::kInvalidNodeId);
  } catch (const std::out_of_range&) {
    threw_invalid = true;
  }
  if (!threw_invalid) return false;
  bool threw_past_end = false;
  try {
    tree.get(7);
  } catch (const std::out_of_range&) {
    threw_past_end = true;
  }
  return threw_past_end;
}

// Named case: duplicate-root input returns an error code, never a crash.
static bool check_duplicate_root_returns_error() {
  runtime::CompiledNode nodes[3];
  nodes[0].parent = -1;
  nodes[1].parent = -1;  // second root: duplicate
  nodes[2].parent = 0;
  runtime::RuntimeTree tree;
  return runtime::instantiate(nodes, 3, tree) == runtime::kInvalidNodeId;
}

// Named case: orphan input (parent index outside the array) returns an
// error code, never a crash.
static bool check_orphan_parent_returns_error() {
  runtime::CompiledNode nodes[2];
  nodes[0].parent = -1;
  nodes[1].parent = 9;  // orphan: no such parent index
  runtime::RuntimeTree tree;
  return runtime::instantiate(nodes, 2, tree) == runtime::kInvalidNodeId;
}

// Named case: empty input instantiates to an empty tree without crashing.
static bool check_empty_input_gives_empty_tree() {
  runtime::RuntimeTree tree;
  runtime::NodeId root = runtime::instantiate(nullptr, 0, tree);
  if (root != runtime::kInvalidNodeId) return false;
  return tree.size() == 0;
}

// Named case (security): self-parent input returns kInvalidNodeId and
// leaves the tree empty, never recursing.
static bool check_self_parent_returns_error() {
  runtime::CompiledNode nodes[2];
  nodes[0].parent = -1;
  nodes[0].tag = 100;
  nodes[1].parent = 1;  // self-parent: parent == self index
  nodes[1].tag = 101;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 2, tree) != runtime::kInvalidNodeId)
    return false;
  return tree.size() == 0;
}

// Named case (security): detached 2-cycle unreachable from the root returns
// kInvalidNodeId and leaves the tree empty, never recursing.
static bool check_detached_cycle_returns_error() {
  runtime::CompiledNode nodes[3];
  nodes[0].parent = -1;
  nodes[0].tag = 100;
  nodes[1].parent = 2;
  nodes[1].tag = 101;
  nodes[2].parent = 1;  // 1 <-> 2 cycle, unreachable from root 0
  nodes[2].tag = 102;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 3, tree) != runtime::kInvalidNodeId)
    return false;
  return tree.size() == 0;
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
  RUN_CASE(check_binary_tree_instantiates_with_stable_ids);
  RUN_CASE(check_ids_stable_across_update_pass);
  RUN_CASE(check_get_unknown_id_throws);
  RUN_CASE(check_duplicate_root_returns_error);
  RUN_CASE(check_orphan_parent_returns_error);
  RUN_CASE(check_empty_input_gives_empty_tree);
  RUN_CASE(check_self_parent_returns_error);
  RUN_CASE(check_detached_cycle_returns_error);
  if (failures > 0) {
    std::printf("instantiate_test: %d failing\n", failures);
    return 1;
  }
  std::printf("instantiate_test: all passing\n");
  return 0;
}
