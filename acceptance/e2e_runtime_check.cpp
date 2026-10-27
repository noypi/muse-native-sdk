#include <cstdio>
#include <string>

#include "runtime/frame.h"
#include "runtime/layout.h"
#include "runtime/tree.h"

static int failures = 0;
#define CHECK(cond, name)                                             \
  do {                                                                \
    if (cond) {                                                       \
      std::printf("PASS %s\n", name);                                 \
    } else {                                                          \
      std::printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__);     \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

using runtime::Align;
using runtime::CompiledNode;
using runtime::Direction;
using runtime::FrameScheduler;
using runtime::Justify;
using runtime::LayoutEngine;
using runtime::NodeId;
using runtime::RuntimeTree;
using runtime::Style;

int main() {
  // Build a 3-level tree: root (column, 100x100) -> row container + leaf.
  Style root_style;
  root_style.direction = Direction::Column;
  root_style.width = 100.0f;
  root_style.height = 100.0f;
  Style row_style;
  row_style.direction = Direction::Row;
  row_style.width = 100.0f;
  row_style.height = 60.0f;
  row_style.gap = 10.0f;
  Style leaf_a_style;
  leaf_a_style.width = 20.0f;
  leaf_a_style.height = 60.0f;
  Style leaf_b_style;
  leaf_b_style.width = 30.0f;
  leaf_b_style.height = 60.0f;
  Style leaf_c_style;  // second branch leaf under root
  leaf_c_style.width = 100.0f;
  leaf_c_style.height = 40.0f;

  CompiledNode nodes[5];
  nodes[0].parent = -1;
  nodes[0].style = root_style;
  nodes[1].parent = 0;
  nodes[1].style = row_style;
  nodes[2].parent = 1;
  nodes[2].style = leaf_a_style;
  nodes[3].parent = 1;
  nodes[3].style = leaf_b_style;
  nodes[4].parent = 0;
  nodes[4].style = leaf_c_style;

  RuntimeTree tree;
  NodeId root = runtime::instantiate(nodes, 5, tree);
  CHECK(root == 0, "e2e_root_id_zero");
  CHECK(tree.size() == 5, "e2e_tree_size_five");
  CHECK(tree.get(2).id == 2 && tree.get(4).id == 4, "e2e_ids_match_indices");

  // AC-RUNTIME-004: exact frames for every node.
  LayoutEngine layout;
  auto root_box = layout.compute(tree, root);
  CHECK(root_box.x == 0 && root_box.y == 0 && root_box.width == 100 &&
            root_box.height == 100,
        "e2e_root_frame_exact");
  CHECK(layout.boxOf(1).x == 0 && layout.boxOf(1).y == 0, "e2e_row_origin");
  CHECK(layout.boxOf(2).x == 0 && layout.boxOf(2).y == 0, "e2e_leaf_a_origin");
  CHECK(layout.boxOf(3).x == 30 && layout.boxOf(3).y == 0,
        "e2e_leaf_b_gap_offset");  // 20 width + 10 gap
  CHECK(layout.boxOf(4).x == 0 && layout.boxOf(4).y == 60,
        "e2e_leaf_c_below_row");

  // AC-RUNTIME-001: ids stable across update (dirty + pump, no re-instantiate).
  FrameScheduler sched(tree);
  tree.markDirty(3);
  CHECK(tree.dirtyCount() == 1, "e2e_one_write_one_dirty");
  int presented = sched.pumpFrame();
  CHECK(presented == 1 && sched.framesPresented() == 1, "e2e_pump_presents_one");
  CHECK(tree.dirtyCount() == 0, "e2e_pump_clears_dirty");
  CHECK(tree.get(2).id == 2 && tree.get(3).id == 3 && tree.get(4).id == 4 &&
            tree.isInstantiated(0),
        "e2e_ids_stable_after_pump");

  // AC-RUNTIME-005: single-node resize touches only that subtree.
  tree.markDirty(3);
  std::size_t touched = layout.relayoutSubtree(tree, 3);
  CHECK(touched == 1, "e2e_leaf_relayout_touches_one");
  CHECK(touched < tree.size(), "e2e_touched_less_than_tree");
  CHECK(tree.dirtyCount() == 0, "e2e_relayout_clears_flags");

  // AC-RUNTIME-003: 100 idle pumps -> 0 frames, 0 nodes touched.
  std::uint64_t before = sched.framesPresented();
  int idle_frames = 0;
  for (int i = 0; i < 100; ++i) {
    idle_frames += sched.pumpFrame();
  }
  CHECK(idle_frames == 0, "e2e_hundred_idle_pumps_zero_frames");
  CHECK(sched.framesPresented() == before, "e2e_no_frames_presented_when_idle");
  CHECK(tree.dirtyCount() == 0, "e2e_no_nodes_touched_when_idle");
  CHECK(!sched.hasPending(), "e2e_no_pending_when_idle");

  if (failures == 0) {
    std::printf("e2e_runtime_check: all passing\n");
    return 0;
  }
  std::printf("e2e_runtime_check: %d failing\n", failures);
  return 1;
}
