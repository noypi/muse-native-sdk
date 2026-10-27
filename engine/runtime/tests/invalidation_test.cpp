// T3 contract tests — FR-RUNTIME-001 / AC-RUNTIME-002 (TC-RUNTIME-002).
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Binds ONLY to the T3 `Interface:` line; never to implementation.
#include <cstddef>
#include <cstdio>

#include "runtime/frame.h"
#include "runtime/tree.h"

namespace {

// Same 3-level binary tree fixture as instantiate_test.cpp:
// root(0) -> {1, 2}; 1 -> {3, 4}; 2 -> {5, 6}.
void make_binary_tree(runtime::CompiledNode* nodes) {
  const int parents[7] = {-1, 0, 0, 1, 1, 2, 2};
  for (int i = 0; i < 7; ++i) {
    nodes[i].parent = parents[i];
    nodes[i].tag = 100 + i;
  }
}

bool instantiate_binary_tree(runtime::RuntimeTree& tree) {
  runtime::CompiledNode nodes[7];
  make_binary_tree(nodes);
  return runtime::instantiate(nodes, 7, tree) == 0 && tree.size() == 7;
}

}  // namespace

// Named case: one state write dirties exactly one node.
static bool check_state_write_dirties_exactly_one_node() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  if (tree.dirtyCount() != 0) return false;
  tree.markDirty(3);
  if (tree.dirtyCount() != 1) return false;
  for (runtime::NodeId id = 0; id < 7; ++id) {
    if (id == 3) continue;
    if (tree.get(id).dirty) return false;
  }
  return tree.get(3).dirty;
}

// Named case: pumpFrame applies pending invalidations, lays out dirty
// subtrees, presents exactly once, and clears the flags.
static bool check_pump_applies_and_presents_once() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  tree.markDirty(3);
  runtime::FrameScheduler scheduler(tree);
  if (scheduler.pumpFrame() != 1) return false;
  if (scheduler.framesPresented() != 1) return false;
  return tree.dirtyCount() == 0;
}

// Named case: unmarked nodes keep identity across the frame —
// isInstantiated stays true and every id is unchanged.
static bool check_unmarked_nodes_keep_identity() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  tree.markDirty(4);
  runtime::FrameScheduler scheduler(tree);
  if (scheduler.pumpFrame() != 1) return false;
  if (tree.size() != 7) return false;
  for (runtime::NodeId id = 0; id < 7; ++id) {
    if (!tree.isInstantiated(id)) return false;
    if (tree.get(id).id != id) return false;
  }
  return !tree.isInstantiated(runtime::kInvalidNodeId);
}

// Named case: a second pump with nothing dirty presents nothing and
// performs no re-instantiation.
static bool check_second_pump_with_no_dirty_presents_nothing() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  tree.markDirty(1);
  runtime::FrameScheduler scheduler(tree);
  if (scheduler.pumpFrame() != 1) return false;
  if (scheduler.pumpFrame() != 0) return false;
  if (scheduler.framesPresented() != 1) return false;
  return tree.size() == 7;
}

// Named case: marking an unknown id is a safe no-op (no crash, no dirt).
static bool check_mark_unknown_id_is_safe_noop() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  tree.markDirty(runtime::kInvalidNodeId);
  tree.markDirty(99);
  return tree.dirtyCount() == 0;
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
  RUN_CASE(check_state_write_dirties_exactly_one_node);
  RUN_CASE(check_pump_applies_and_presents_once);
  RUN_CASE(check_unmarked_nodes_keep_identity);
  RUN_CASE(check_second_pump_with_no_dirty_presents_nothing);
  RUN_CASE(check_mark_unknown_id_is_safe_noop);
  if (failures > 0) {
    std::printf("invalidation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("invalidation_test: all passing\n");
  return 0;
}
