// T5 contract tests — FR-RUNTIME-002 / AC-RUNTIME-004 (TC-RUNTIME-004).
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Binds to the T5 `Interface:` line; style inputs ride the compiled-tree
// fixture channel (plan A3) as documented below.
//
// ASSUMPTION (style channel): the plan Detail requires direction, justify,
// align, gap, padding, margin and fixed-or-flex size per node, but the
// committed tree.h (T2) carries only parent+tag and the T5 `Interface:` line
// names no style setter. These cases drive style through
// `CompiledNode::style` / `Node::style` fields the coder adds with T5
// (coder-owned production API). Enum, struct and field names below are the
// test's proposal; the T5 contract records the reconciliation rule (update
// call sites, not expectations) if the coder names them differently.
//
// ASSUMPTION (layout semantics): minimal flexbox subset —
// column stacks on +y, row lays out on +x; padding insets the content
// origin; margin offsets the node's own box and advances the cursor;
// gap separates adjacent siblings; fixed size wins, otherwise remaining
// space is split proportionally to flex weights; cross axis defaults to
// Start (no implicit stretch); justify Start packs at the content origin.
// All expected frames are exactly representable in float.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <stdexcept>

#include "runtime/layout.h"
#include "runtime/tree.h"

namespace {

bool same(float a, float b) { return a == b; }

bool same_box(const runtime::LayoutBox& got, float x, float y, float w,
              float h) {
  return same(got.x, x) && same(got.y, y) && same(got.width, w) &&
         same(got.height, h);
}

// Root 100x100, column, padding 0, gap 0; A fixed 20x10, B fixed 20x10.
bool build_column_pair(runtime::RuntimeTree& tree) {
  runtime::CompiledNode nodes[3];
  nodes[0].parent = -1;
  nodes[0].style.direction = runtime::Direction::Column;
  nodes[0].style.justify = runtime::Justify::Start;
  nodes[0].style.align = runtime::Align::Start;
  nodes[0].style.gap = 0.0f;
  nodes[0].style.padding = 0.0f;
  nodes[0].style.width = 100.0f;
  nodes[0].style.height = 100.0f;
  nodes[0].style.flex = 0.0f;
  nodes[1].parent = 0;
  nodes[1].style.width = 20.0f;
  nodes[1].style.height = 10.0f;
  nodes[1].style.margin = 0.0f;
  nodes[1].style.flex = 0.0f;
  nodes[2].parent = 0;
  nodes[2].style.width = 20.0f;
  nodes[2].style.height = 10.0f;
  nodes[2].style.margin = 0.0f;
  nodes[2].style.flex = 0.0f;
  return runtime::instantiate(nodes, 3, tree) == 0 && tree.size() == 3;
}

}  // namespace

// Named case: column direction stacks fixed children on +y.
static bool check_column_stacks_children_vertically() {
  runtime::RuntimeTree tree;
  if (!build_column_pair(tree)) return false;
  runtime::LayoutEngine engine;
  runtime::LayoutBox root = engine.compute(tree, 0);
  if (!same_box(root, 0.0f, 0.0f, 100.0f, 100.0f)) return false;
  if (!same_box(engine.boxOf(0), 0.0f, 0.0f, 100.0f, 100.0f)) return false;
  if (!same_box(engine.boxOf(1), 0.0f, 0.0f, 20.0f, 10.0f)) return false;
  return same_box(engine.boxOf(2), 0.0f, 10.0f, 20.0f, 10.0f);
}

// Named case: row direction lays fixed children out on +x.
static bool check_row_lays_children_out_horizontally() {
  runtime::CompiledNode nodes[3];
  nodes[0].parent = -1;
  nodes[0].style.direction = runtime::Direction::Row;
  nodes[0].style.width = 100.0f;
  nodes[0].style.height = 100.0f;
  nodes[0].style.gap = 0.0f;
  nodes[0].style.padding = 0.0f;
  nodes[1].parent = 0;
  nodes[1].style.width = 10.0f;
  nodes[1].style.height = 20.0f;
  nodes[1].style.flex = 0.0f;
  nodes[2].parent = 0;
  nodes[2].style.width = 10.0f;
  nodes[2].style.height = 20.0f;
  nodes[2].style.flex = 0.0f;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 3, tree) != 0) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  if (!same_box(engine.boxOf(1), 0.0f, 0.0f, 10.0f, 20.0f)) return false;
  return same_box(engine.boxOf(2), 10.0f, 0.0f, 10.0f, 20.0f);
}

// Named case: gap separates adjacent siblings.
static bool check_gap_separates_siblings() {
  runtime::CompiledNode nodes[3];
  nodes[0].parent = -1;
  nodes[0].style.direction = runtime::Direction::Column;
  nodes[0].style.width = 100.0f;
  nodes[0].style.height = 100.0f;
  nodes[0].style.gap = 5.0f;
  nodes[0].style.padding = 0.0f;
  nodes[1].parent = 0;
  nodes[1].style.width = 20.0f;
  nodes[1].style.height = 10.0f;
  nodes[2].parent = 0;
  nodes[2].style.width = 20.0f;
  nodes[2].style.height = 10.0f;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 3, tree) != 0) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  if (!same_box(engine.boxOf(1), 0.0f, 0.0f, 20.0f, 10.0f)) return false;
  return same_box(engine.boxOf(2), 0.0f, 15.0f, 20.0f, 10.0f);
}

// Named case: padding insets the content origin.
static bool check_padding_insets_children() {
  runtime::CompiledNode nodes[2];
  nodes[0].parent = -1;
  nodes[0].style.direction = runtime::Direction::Column;
  nodes[0].style.width = 100.0f;
  nodes[0].style.height = 100.0f;
  nodes[0].style.padding = 10.0f;
  nodes[0].style.gap = 0.0f;
  nodes[1].parent = 0;
  nodes[1].style.width = 20.0f;
  nodes[1].style.height = 10.0f;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 2, tree) != 0) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  return same_box(engine.boxOf(1), 10.0f, 10.0f, 20.0f, 10.0f);
}

// Named case: margin offsets the node's own box.
static bool check_margin_offsets_node() {
  runtime::CompiledNode nodes[2];
  nodes[0].parent = -1;
  nodes[0].style.direction = runtime::Direction::Column;
  nodes[0].style.width = 100.0f;
  nodes[0].style.height = 100.0f;
  nodes[0].style.padding = 0.0f;
  nodes[0].style.gap = 0.0f;
  nodes[1].parent = 0;
  nodes[1].style.width = 20.0f;
  nodes[1].style.height = 10.0f;
  nodes[1].style.margin = 5.0f;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 2, tree) != 0) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  return same_box(engine.boxOf(1), 5.0f, 5.0f, 20.0f, 10.0f);
}

// Named case: remaining space is split proportionally to flex weights.
static bool check_flex_distributes_remaining_space() {
  runtime::CompiledNode nodes[4];
  nodes[0].parent = -1;
  nodes[0].style.direction = runtime::Direction::Column;
  nodes[0].style.width = 100.0f;
  nodes[0].style.height = 100.0f;
  nodes[0].style.padding = 0.0f;
  nodes[0].style.gap = 0.0f;
  nodes[1].parent = 0;  // fixed header, 20 high
  nodes[1].style.width = 100.0f;
  nodes[1].style.height = 20.0f;
  nodes[1].style.flex = 0.0f;
  nodes[2].parent = 0;  // flex 1 -> 40 of the remaining 80
  nodes[2].style.width = 100.0f;
  nodes[2].style.height = 0.0f;
  nodes[2].style.flex = 1.0f;
  nodes[3].parent = 0;  // flex 1 -> 40 of the remaining 80
  nodes[3].style.width = 100.0f;
  nodes[3].style.height = 0.0f;
  nodes[3].style.flex = 1.0f;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 4, tree) != 0) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  if (!same_box(engine.boxOf(1), 0.0f, 0.0f, 100.0f, 20.0f)) return false;
  if (!same_box(engine.boxOf(2), 0.0f, 20.0f, 100.0f, 40.0f)) return false;
  return same_box(engine.boxOf(3), 0.0f, 60.0f, 100.0f, 40.0f);
}

// Named case: justify Center packs children around the main-axis center.
static bool check_justify_center_packs_main_axis() {
  runtime::CompiledNode nodes[3];
  nodes[0].parent = -1;
  nodes[0].style.direction = runtime::Direction::Row;
  nodes[0].style.justify = runtime::Justify::Center;
  nodes[0].style.width = 100.0f;
  nodes[0].style.height = 50.0f;
  nodes[0].style.gap = 0.0f;
  nodes[0].style.padding = 0.0f;
  nodes[1].parent = 0;
  nodes[1].style.width = 20.0f;
  nodes[1].style.height = 10.0f;
  nodes[2].parent = 0;
  nodes[2].style.width = 20.0f;
  nodes[2].style.height = 10.0f;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 3, tree) != 0) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  if (!same_box(engine.boxOf(1), 30.0f, 0.0f, 20.0f, 10.0f)) return false;
  return same_box(engine.boxOf(2), 50.0f, 0.0f, 20.0f, 10.0f);
}

// Named case: align Center centers children on the cross axis.
static bool check_align_center_centers_cross_axis() {
  runtime::CompiledNode nodes[2];
  nodes[0].parent = -1;
  nodes[0].style.direction = runtime::Direction::Row;
  nodes[0].style.align = runtime::Align::Center;
  nodes[0].style.width = 100.0f;
  nodes[0].style.height = 50.0f;
  nodes[0].style.gap = 0.0f;
  nodes[0].style.padding = 0.0f;
  nodes[1].parent = 0;
  nodes[1].style.width = 20.0f;
  nodes[1].style.height = 10.0f;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 2, tree) != 0) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  return same_box(engine.boxOf(1), 0.0f, 20.0f, 20.0f, 10.0f);
}

// Named case: nested containers resolve exact frames for every node.
static bool check_nested_containers_resolve_every_node() {
  // root column 100x100; K row-container 100x50 with A,B 10x10; L 100x50.
  runtime::CompiledNode nodes[5];
  nodes[0].parent = -1;
  nodes[0].style.direction = runtime::Direction::Column;
  nodes[0].style.width = 100.0f;
  nodes[0].style.height = 100.0f;
  nodes[0].style.gap = 0.0f;
  nodes[0].style.padding = 0.0f;
  nodes[1].parent = 0;
  nodes[1].style.direction = runtime::Direction::Row;
  nodes[1].style.width = 100.0f;
  nodes[1].style.height = 50.0f;
  nodes[1].style.gap = 0.0f;
  nodes[1].style.padding = 0.0f;
  nodes[2].parent = 1;
  nodes[2].style.width = 10.0f;
  nodes[2].style.height = 10.0f;
  nodes[3].parent = 1;
  nodes[3].style.width = 10.0f;
  nodes[3].style.height = 10.0f;
  nodes[4].parent = 0;
  nodes[4].style.width = 100.0f;
  nodes[4].style.height = 50.0f;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 5, tree) != 0) return false;
  runtime::LayoutEngine engine;
  runtime::LayoutBox root = engine.compute(tree, 0);
  if (!same_box(root, 0.0f, 0.0f, 100.0f, 100.0f)) return false;
  if (!same_box(engine.boxOf(1), 0.0f, 0.0f, 100.0f, 50.0f)) return false;
  if (!same_box(engine.boxOf(2), 0.0f, 0.0f, 10.0f, 10.0f)) return false;
  if (!same_box(engine.boxOf(3), 10.0f, 0.0f, 10.0f, 10.0f)) return false;
  return same_box(engine.boxOf(4), 0.0f, 50.0f, 100.0f, 50.0f);
}

// Named case: compute returns the root frame and boxOf agrees with it.
static bool check_compute_returns_root_frame() {
  runtime::RuntimeTree tree;
  if (!build_column_pair(tree)) return false;
  runtime::LayoutEngine engine;
  runtime::LayoutBox root = engine.compute(tree, 0);
  const runtime::LayoutBox& cached = engine.boxOf(0);
  return same_box(root, cached.x, cached.y, cached.width, cached.height);
}

// Named case: an unknown enum value is a visible error, not a silent
// default. The build-visible half is enforced by -Wall -Wextra -Wpedantic
// (-Wswitch, no default case) in the coder's implementation; the runtime
// half rejects the value instead of laying out as if it were valid.
static bool check_unknown_enum_value_is_rejected() {
  // Exhaustive switch with no default: adding an enumerator without
  // handling it breaks the build under -Werror. This mirrors the
  // production enum surface proposed for T5.
  runtime::Direction dir = runtime::Direction::Column;
  int seen = 0;
  switch (dir) {
    case runtime::Direction::Row:
      seen = 1;
      break;
    case runtime::Direction::Column:
      seen = 2;
      break;
  }
  if (seen != 2) return false;
  runtime::CompiledNode nodes[2];
  nodes[0].parent = -1;
  nodes[0].style.direction = runtime::Direction::Column;
  nodes[0].style.width = 100.0f;
  nodes[0].style.height = 100.0f;
  nodes[1].parent = 0;
  nodes[1].style.direction = static_cast<runtime::Direction>(999);
  nodes[1].style.width = 20.0f;
  nodes[1].style.height = 10.0f;
  runtime::RuntimeTree tree;
  if (runtime::instantiate(nodes, 2, tree) != 0) return false;
  runtime::LayoutEngine engine;
  try {
    engine.compute(tree, 0);
  } catch (const std::exception&) {
    return true;
  }
  return false;  // silently defaulted: the spec forbids this.
}

// Named case: boxOf before any compute throws.
static bool check_box_before_compute_throws() {
  runtime::RuntimeTree tree;
  if (!build_column_pair(tree)) return false;
  runtime::LayoutEngine engine;
  try {
    engine.boxOf(0);
  } catch (const std::out_of_range&) {
    return true;
  } catch (const std::exception&) {
    return false;
  }
  return false;
}

// Named case: boxOf an unknown id throws even after compute.
static bool check_box_of_unknown_id_throws() {
  runtime::RuntimeTree tree;
  if (!build_column_pair(tree)) return false;
  runtime::LayoutEngine engine;
  engine.compute(tree, 0);
  try {
    engine.boxOf(99);
  } catch (const std::out_of_range&) {
    return true;
  } catch (const std::exception&) {
    return false;
  }
  return false;
}

// Named case: compute on an unknown root throws.
static bool check_compute_unknown_root_throws() {
  runtime::RuntimeTree tree;
  if (!build_column_pair(tree)) return false;
  runtime::LayoutEngine engine;
  try {
    engine.compute(tree, 99);
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

// Named case: repeated compute is deterministic.
static bool check_compute_is_deterministic() {
  runtime::RuntimeTree tree;
  if (!build_column_pair(tree)) return false;
  runtime::LayoutEngine engine;
  runtime::LayoutBox first = engine.compute(tree, 0);
  runtime::LayoutBox second = engine.compute(tree, 0);
  return same_box(first, second.x, second.y, second.width, second.height) &&
         same_box(engine.boxOf(2), 0.0f, 10.0f, 20.0f, 10.0f);
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
  RUN_CASE(check_column_stacks_children_vertically);
  RUN_CASE(check_row_lays_children_out_horizontally);
  RUN_CASE(check_gap_separates_siblings);
  RUN_CASE(check_padding_insets_children);
  RUN_CASE(check_margin_offsets_node);
  RUN_CASE(check_flex_distributes_remaining_space);
  RUN_CASE(check_justify_center_packs_main_axis);
  RUN_CASE(check_align_center_centers_cross_axis);
  RUN_CASE(check_nested_containers_resolve_every_node);
  RUN_CASE(check_compute_returns_root_frame);
  RUN_CASE(check_unknown_enum_value_is_rejected);
  RUN_CASE(check_box_before_compute_throws);
  RUN_CASE(check_box_of_unknown_id_throws);
  RUN_CASE(check_compute_unknown_root_throws);
  RUN_CASE(check_compute_is_deterministic);
  if (failures > 0) {
    std::printf("layout_test: %d failing\n", failures);
    return 1;
  }
  std::printf("layout_test: all passing\n");
  return 0;
}
