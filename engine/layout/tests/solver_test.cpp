// T2 contract tests — FR-LAYOUT-006 / AC-LAYOUT-010 (TC-LAYOUT-010),
// NFR-LAYOUT-001/AC-LAYOUT-001 as constraint (pure C++ types, no JNI).
// Hermetic: C++17 + layout/solver.h only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T2 `Interface:` line:
//   `enum class layout::Backend { Flex, Constraint };`
//   `struct layout::Constraint { NodeId node; int attribute; float constant; float weight; };`
//   `class layout::Solver { public: NodeId addNode(Backend b, NodeId parent);
//    void setFlex(NodeId id, float w, float h, float flexGrow);
//    void addConstraint(const Constraint& c);
//    layout::Box solve(NodeId root); const layout::Box& boxOf(NodeId id) const; };`
//   `struct layout::Box { float x,y,w,h; };`
//   unknown id throws `std::out_of_range`; `boxOf` before `solve` throws
//   `std::out_of_range`; pure C++ types only.
//
// ASSUMPTION (root parent): as in T1, a root node is added with parent
// `layout::kInvalidNodeId`.
//
// ASSUMPTION (constraint attribute encoding): the plan names `attribute` as
// a plain int with no enumerator. These cases use 0=x, 1=y, 2=w, 3=h and
// assert only the width channel (attribute 2 applies `constant` to the
// node's width). If the planner/coder settles a different encoding, the
// width-channel expectations pin the behavior, not the integer value.
//
// ASSUMPTION (flex semantics): a Flex node with fixed w/h keeps that size;
// remaining space on the main (+x) axis is split proportionally to
// flexGrow among siblings with no fixed size. All expected frames are
// exactly representable in float.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <type_traits>

#include "layout/solver.h"

namespace {

bool same(float a, float b) { return a == b; }

bool same_box(const layout::Box& got, float x, float y, float w, float h) {
  return same(got.x, x) && same(got.y, y) && same(got.w, w) && same(got.h, h);
}

}  // namespace

// NFR-LAYOUT-001 constraint: the solver surface is pure C++ — Box is a
// trivially copyable POD of four floats, no object references cross it.
static_assert(std::is_standard_layout<layout::Box>::value,
              "Box must be a plain C++ layout type");
static_assert(std::is_trivially_copyable<layout::Box>::value,
              "Box must be trivially copyable");
static_assert(sizeof(layout::Box) == 4 * sizeof(float),
              "Box must carry exactly x,y,w,h floats");

// Named case (AC-LAYOUT-010): Flex and Constraint backends coexist in one
// tree — a flex root with a constraint child solves without error.
static bool check_hybrid_tree_solves() {
  layout::Solver solver;
  layout::NodeId root =
      solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 100.0f, 100.0f, 0.0f);
  layout::NodeId child = solver.addNode(layout::Backend::Constraint, root);
  layout::Constraint c;
  c.node = child;
  c.attribute = 2;  // width channel (see ASSUMPTION).
  c.constant = 40.0f;
  c.weight = 1.0f;
  solver.addConstraint(c);
  layout::Box box = solver.solve(root);
  if (!same_box(box, 0.0f, 0.0f, 100.0f, 100.0f)) return false;
  return same(solver.boxOf(child).w, 40.0f);
}

// Named case (AC-LAYOUT-010): a pure flex row splits remaining width by
// flexGrow weights.
static bool check_flex_distributes_remaining_space() {
  layout::Solver solver;
  layout::NodeId root =
      solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 100.0f, 20.0f, 0.0f);
  layout::NodeId a = solver.addNode(layout::Backend::Flex, root);
  solver.setFlex(a, 0.0f, 20.0f, 1.0f);
  layout::NodeId b = solver.addNode(layout::Backend::Flex, root);
  solver.setFlex(b, 0.0f, 20.0f, 1.0f);
  solver.solve(root);
  if (!same_box(solver.boxOf(a), 0.0f, 0.0f, 50.0f, 20.0f)) return false;
  return same_box(solver.boxOf(b), 50.0f, 0.0f, 50.0f, 20.0f);
}

// Named case (AC-LAYOUT-010): constraints apply where flex cannot — the
// width constant pins the node's width exactly.
static bool check_constraint_pins_width() {
  layout::Solver solver;
  layout::NodeId root =
      solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 200.0f, 100.0f, 0.0f);
  layout::NodeId child = solver.addNode(layout::Backend::Constraint, root);
  layout::Constraint c;
  c.node = child;
  c.attribute = 2;
  c.constant = 75.0f;
  c.weight = 1.0f;
  solver.addConstraint(c);
  solver.solve(root);
  return same(solver.boxOf(child).w, 75.0f);
}

// Named case: solve returns the root frame and boxOf agrees with it.
static bool check_solve_returns_root_frame() {
  layout::Solver solver;
  layout::NodeId root =
      solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 100.0f, 50.0f, 0.0f);
  layout::Box box = solver.solve(root);
  const layout::Box& cached = solver.boxOf(root);
  return same_box(box, cached.x, cached.y, cached.w, cached.h);
}

// Named case: boxOf before any solve throws std::out_of_range.
static bool check_box_before_solve_throws() {
  layout::Solver solver;
  layout::NodeId root =
      solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 100.0f, 50.0f, 0.0f);
  try {
    solver.boxOf(root);
  } catch (const std::out_of_range&) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

// Named case: boxOf an unknown id throws std::out_of_range after solve.
static bool check_box_of_unknown_id_throws() {
  layout::Solver solver;
  layout::NodeId root =
      solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 100.0f, 50.0f, 0.0f);
  solver.solve(root);
  try {
    solver.boxOf(static_cast<layout::NodeId>(9999));
  } catch (const std::out_of_range&) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

// Named case: setFlex on an unknown id throws std::out_of_range.
static bool check_setflex_unknown_id_throws() {
  layout::Solver solver;
  layout::NodeId root =
      solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 100.0f, 50.0f, 0.0f);
  try {
    solver.setFlex(static_cast<layout::NodeId>(9999), 10.0f, 10.0f, 0.0f);
  } catch (const std::out_of_range&) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

// Named case: addConstraint for an unknown node throws std::out_of_range.
static bool check_constraint_unknown_node_throws() {
  layout::Solver solver;
  layout::NodeId root =
      solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 100.0f, 50.0f, 0.0f);
  layout::Constraint c;
  c.node = static_cast<layout::NodeId>(9999);
  c.attribute = 2;
  c.constant = 10.0f;
  c.weight = 1.0f;
  try {
    solver.addConstraint(c);
  } catch (const std::out_of_range&) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

// Named case: solving an unknown root throws std::out_of_range.
static bool check_solve_unknown_root_throws() {
  layout::Solver solver;
  layout::NodeId root =
      solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 100.0f, 50.0f, 0.0f);
  try {
    solver.solve(static_cast<layout::NodeId>(9999));
  } catch (const std::out_of_range&) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

// Named case: repeated solve is deterministic.
static bool check_solve_is_deterministic() {
  layout::Solver solver;
  layout::NodeId root =
      solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 100.0f, 50.0f, 0.0f);
  layout::NodeId a = solver.addNode(layout::Backend::Flex, root);
  solver.setFlex(a, 0.0f, 50.0f, 1.0f);
  layout::Box first = solver.solve(root);
  layout::Box second = solver.solve(root);
  return same_box(first, second.x, second.y, second.w, second.h) &&
         same(solver.boxOf(a).w, 100.0f);
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
  RUN_CASE(check_hybrid_tree_solves);
  RUN_CASE(check_flex_distributes_remaining_space);
  RUN_CASE(check_constraint_pins_width);
  RUN_CASE(check_solve_returns_root_frame);
  RUN_CASE(check_box_before_solve_throws);
  RUN_CASE(check_box_of_unknown_id_throws);
  RUN_CASE(check_setflex_unknown_id_throws);
  RUN_CASE(check_constraint_unknown_node_throws);
  RUN_CASE(check_solve_unknown_root_throws);
  RUN_CASE(check_solve_is_deterministic);
  if (failures > 0) {
    std::printf("solver_test: %d failing\n", failures);
    return 1;
  }
  std::printf("solver_test: all passing\n");
  return 0;
}
