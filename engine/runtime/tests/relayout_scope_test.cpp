// T6 contract tests — FR-RUNTIME-002 / AC-RUNTIME-005 (TC-RUNTIME-005).
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Binds ONLY to the T6 `Interface:` line plus the T2/T3 lines it depends
// on; never to implementation.
//
// ASSUMPTION (touched counting): "number of nodes touched" counts the
// nodes whose boxes were recomputed, i.e. exactly the nodes of the dirty
// subtree ("re-lays out only that subtree" — the item title). Ancestors'
// frames are adjusted without re-entering clean subtrees and are NOT
// counted. If the planner rules that adjusted ancestors count, the exact
// expectations below shift by the ancestor-path length and the contract
// will be updated (call sites only, per GR-08 T1.5).
//
// ASSUMPTION (resize trigger): no size-mutation setter exists in the
// committed headers; the geometry change itself rides the T5 style channel
// (coder-owned). These cases observe the scope contract — touched count
// and cache preservation — through `relayoutSubtree`, which is fully named
// by the T6 `Interface:` line.
#include <cstddef>
#include <cstdio>
#include <stdexcept>

#include "runtime/layout.h"
#include "runtime/tree.h"

namespace {

// Wide tree (13 nodes): root(0) -> {1, 2, 3};
// 1 -> {4, 5, 6}; 2 -> {7, 8, 9}; 3 -> {10, 11, 12}.
bool build_wide_tree(runtime::RuntimeTree& tree) {
  const int parents[13] = {-1, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3};
  runtime::CompiledNode nodes[13];
  for (int i = 0; i < 13; ++i) {
    nodes[i].parent = parents[i];
    nodes[i].tag = i;
  }
  return runtime::instantiate(nodes, 13, tree) == 0 && tree.size() == 13;
}

bool same_box(const runtime::LayoutBox& a, const runtime::LayoutBox& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width &&
         a.height == b.height;
}

}  // namespace

// Named case: re-laying out one branch touches exactly that subtree
// (branch + its 3 leaves = 4), strictly fewer than the 13-node tree.
static bool check_branch_relayout_touches_only_branch() {
  runtime::RuntimeTree tree;
  if (!build_wide_tree(tree)) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  std::size_t touched = engine.relayoutSubtree(tree, 1);
  if (touched != 4) return false;
  return touched < tree.size();
}

// Named case: a single leaf re-layout touches exactly one node.
static bool check_leaf_relayout_touches_only_leaf() {
  runtime::RuntimeTree tree;
  if (!build_wide_tree(tree)) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  std::size_t touched = engine.relayoutSubtree(tree, 4);
  if (touched != 1) return false;
  return touched < tree.size();
}

// Named case: sibling (clean) subtrees keep their cached boxes —
// re-laying out branch 1 leaves branch 2/3 boxes bit-identical.
static bool check_clean_siblings_keep_cached_boxes() {
  runtime::RuntimeTree tree;
  if (!build_wide_tree(tree)) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  runtime::LayoutBox before7 = engine.boxOf(7);
  runtime::LayoutBox before8 = engine.boxOf(8);
  runtime::LayoutBox before9 = engine.boxOf(9);
  runtime::LayoutBox before10 = engine.boxOf(10);
  runtime::LayoutBox before2 = engine.boxOf(2);
  runtime::LayoutBox before3 = engine.boxOf(3);
  engine.relayoutSubtree(tree, 1);
  if (!same_box(engine.boxOf(7), before7)) return false;
  if (!same_box(engine.boxOf(8), before8)) return false;
  if (!same_box(engine.boxOf(9), before9)) return false;
  if (!same_box(engine.boxOf(10), before10)) return false;
  if (!same_box(engine.boxOf(2), before2)) return false;
  return same_box(engine.boxOf(3), before3);
}

// Named case: re-laying out the root touches the whole tree.
static bool check_root_relayout_touches_all() {
  runtime::RuntimeTree tree;
  if (!build_wide_tree(tree)) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  return engine.relayoutSubtree(tree, 0) == tree.size();
}

// Named case: re-laying out an unknown id touches nothing and is safe.
static bool check_relayout_unknown_id_is_safe_noop() {
  runtime::RuntimeTree tree;
  if (!build_wide_tree(tree)) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  if (engine.relayoutSubtree(tree, 99) != 0) return false;
  return engine.relayoutSubtree(tree, runtime::kInvalidNodeId) == 0;
}

// Named case: the dirty subtree's flags clear while untouched nodes keep
// identity — one marked leaf resolves with zero re-instantiations.
static bool check_relayout_clears_subtree_dirty_flags() {
  runtime::RuntimeTree tree;
  if (!build_wide_tree(tree)) return false;
  tree.markDirty(4);
  if (tree.dirtyCount() != 1) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  std::size_t touched = engine.relayoutSubtree(tree, 1);
  if (touched != 4) return false;
  if (tree.dirtyCount() != 0) return false;
  if (tree.size() != 13) return false;
  for (runtime::NodeId id = 0; id < 13; ++id) {
    if (!tree.isInstantiated(id)) return false;
    if (tree.get(id).id != id) return false;
  }
  return true;
}

#define RUN_CASE(name)                                   \
  do {                                                   \
    if (!(name)()) {                                     \
      std::printf("FAIL %s\n", #name);                   \
      ++failures;                                        \
    } else {                                             \
      std::printf("PASS %s\n", #name);                   \
    }                                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_branch_relayout_touches_only_branch);
  RUN_CASE(check_leaf_relayout_touches_only_leaf);
  RUN_CASE(check_clean_siblings_keep_cached_boxes);
  RUN_CASE(check_root_relayout_touches_all);
  RUN_CASE(check_relayout_unknown_id_is_safe_noop);
  RUN_CASE(check_relayout_clears_subtree_dirty_flags);
  if (failures > 0) {
    std::printf("relayout_scope_test: %d failing\n", failures);
    return 1;
  }
  std::printf("relayout_scope_test: all passing\n");
  return 0;
}
