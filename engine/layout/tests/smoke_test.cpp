// T1 contract tests — engine/layout scaffolding (no spec refs; FR-LAYOUT-006/007 groundwork).
// Hermetic: C++17 + layout/types.h only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T1 `Interface:` line:
//   `layout::NodeId allocateNode(layout::Tree& tree, layout::NodeId parent);`
//   `layout::Tree` — `struct Tree { std::uint32_t nodeCount() const; bool contains(NodeId) const; }`,
//   `using NodeId = std::uint32_t`, invalid id `layout::kInvalidNodeId`;
//   errors via return of `kInvalidNodeId`, never exceptions across the module boundary.
//
// ASSUMPTION (root allocation): a root node is allocated by passing
// `layout::kInvalidNodeId` as the parent. The plan does not name a separate
// root constructor, so parent == kInvalidNodeId means "no parent".
//
// ASSUMPTION (id stability): allocated ids are distinct and `contains` stays
// true for every id previously returned; `nodeCount` equals the number of
// successful allocations.
#include <cstdint>
#include <cstdio>

#include "layout/types.h"

namespace {

// Fresh tree is empty and rejects the invalid id.
bool fresh_tree_is_empty() {
  layout::Tree tree;
  return tree.nodeCount() == 0 && !tree.contains(layout::kInvalidNodeId);
}

}  // namespace

// Named case: allocating a root yields a valid id distinct from kInvalidNodeId.
static bool check_allocate_root_returns_valid_id() {
  layout::Tree tree;
  layout::NodeId root = layout::allocateNode(tree, layout::kInvalidNodeId);
  if (root == layout::kInvalidNodeId) return false;
  if (tree.nodeCount() != 1) return false;
  return tree.contains(root);
}

// Named case: allocating a child under a valid parent yields a distinct id.
static bool check_allocate_child_returns_distinct_id() {
  layout::Tree tree;
  layout::NodeId root = layout::allocateNode(tree, layout::kInvalidNodeId);
  if (root == layout::kInvalidNodeId) return false;
  layout::NodeId child = layout::allocateNode(tree, root);
  if (child == layout::kInvalidNodeId) return false;
  if (child == root) return false;
  if (tree.nodeCount() != 2) return false;
  return tree.contains(root) && tree.contains(child);
}

// Named case: allocating under an unknown parent fails via kInvalidNodeId
// (never an exception) and leaves the tree unchanged.
static bool check_allocate_unknown_parent_returns_invalid_id() {
  layout::Tree tree;
  layout::NodeId root = layout::allocateNode(tree, layout::kInvalidNodeId);
  if (root == layout::kInvalidNodeId) return false;
  bool threw = false;
  layout::NodeId bad = layout::kInvalidNodeId;
  try {
    bad = layout::allocateNode(tree, static_cast<layout::NodeId>(9999));
  } catch (...) {
    threw = true;
  }
  if (threw) return false;  // module boundary must not throw.
  if (bad != layout::kInvalidNodeId) return false;
  if (tree.nodeCount() != 1) return false;
  return tree.contains(root) && !tree.contains(bad);
}

// Named case: allocating under kInvalidNodeId-as-parent twice yields two
// distinct roots (forest), not a collision.
static bool check_two_roots_are_distinct() {
  layout::Tree tree;
  layout::NodeId a = layout::allocateNode(tree, layout::kInvalidNodeId);
  layout::NodeId b = layout::allocateNode(tree, layout::kInvalidNodeId);
  if (a == layout::kInvalidNodeId || b == layout::kInvalidNodeId) return false;
  if (a == b) return false;
  return tree.nodeCount() == 2;
}

// Named case: contains is false for never-allocated ids.
static bool check_contains_rejects_never_allocated_id() {
  layout::Tree tree;
  layout::NodeId root = layout::allocateNode(tree, layout::kInvalidNodeId);
  if (root == layout::kInvalidNodeId) return false;
  if (tree.contains(static_cast<layout::NodeId>(424242))) return false;
  return !tree.contains(layout::kInvalidNodeId);
}

// Named case: sequential allocations are all distinct and all contained.
static bool check_sequential_allocations_are_stable() {
  layout::Tree tree;
  layout::NodeId ids[8];
  ids[0] = layout::allocateNode(tree, layout::kInvalidNodeId);
  for (int i = 1; i < 8; ++i) ids[i] = layout::allocateNode(tree, ids[i - 1]);
  for (int i = 0; i < 8; ++i) {
    if (ids[i] == layout::kInvalidNodeId) return false;
    if (!tree.contains(ids[i])) return false;
    for (int j = 0; j < i; ++j)
      if (ids[i] == ids[j]) return false;
  }
  return tree.nodeCount() == 8;
}

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  if (!fresh_tree_is_empty()) {
    std::printf("FAIL fresh_tree_is_empty\n");
    ++failures;
  } else {
    std::printf("PASS fresh_tree_is_empty\n");
  }
  RUN_CASE(check_allocate_root_returns_valid_id);
  RUN_CASE(check_allocate_child_returns_distinct_id);
  RUN_CASE(check_allocate_unknown_parent_returns_invalid_id);
  RUN_CASE(check_two_roots_are_distinct);
  RUN_CASE(check_contains_rejects_never_allocated_id);
  RUN_CASE(check_sequential_allocations_are_stable);
  if (failures > 0) {
    std::printf("smoke_test: %d failing\n", failures);
    return 1;
  }
  std::printf("smoke_test: all passing\n");
  return 0;
}
