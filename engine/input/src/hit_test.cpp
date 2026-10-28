#include "hit_test.h"

bool HitRect::contains(float px, float py) const {
  return px >= x && py >= y && px < x + w && py < y + h;
}

void HitTestTree::AddNode(int32_t id, int32_t parentId, HitRect bounds) {
  for (auto& node : nodes_) {
    if (!node.used) {
      node.used = true;
      node.id = id;
      node.parentId = parentId;
      node.bounds = bounds;
      return;
    }
  }
}

int HitTestTree::find_index(int32_t id) const {
  for (int i = 0; i < kMaxNodes; ++i) {
    if (nodes_[i].used && nodes_[i].id == id) return i;
  }
  return -1;
}

int HitTestTree::depth_of(int index) const {
  int depth = 0;
  int32_t parent = nodes_[index].parentId;
  // Walk up the parent chain; cap iterations to avoid cycles.
  for (int i = 0; i < kMaxNodes && parent != 0; ++i) {
    int pi = find_index(parent);
    if (pi < 0) break;
    ++depth;
    parent = nodes_[pi].parentId;
  }
  return depth;
}

int32_t HitTestTree::HitTest(float x, float y) const {
  int32_t best = -1;
  int bestDepth = -1;
  for (int i = 0; i < kMaxNodes; ++i) {
    if (!nodes_[i].used) continue;
    if (!nodes_[i].bounds.contains(x, y)) continue;
    int depth = depth_of(i);
    if (depth > bestDepth) {
      bestDepth = depth;
      best = nodes_[i].id;
    }
  }
  return best;
}
