// T4 contract tests — FR-RUNTIME-001 / AC-RUNTIME-003 (TC-RUNTIME-003).
// Idle application produces no frames and the loop never spins.
// NO continuous-driver API is referenced (FR-RUNTIME-006 out of scope).
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Binds ONLY to the T4 `Interface:` line; never to implementation.
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

// Named case: a freshly instantiated tree with no writes has nothing
// pending.
static bool check_no_pending_when_clean() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  return !scheduler.hasPending();
}

// Named case: 100 idle pumps present 0 frames.
static bool check_hundred_idle_pumps_present_zero_frames() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  for (int i = 0; i < 100; ++i) {
    if (scheduler.pumpFrame() != 0) return false;
  }
  return scheduler.framesPresented() == 0;
}

// Named case: 100 idle pumps touch 0 nodes (no dirty flags ever appear,
// tree size and identity unchanged).
static bool check_hundred_idle_pumps_touch_zero_nodes() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  for (int i = 0; i < 100; ++i) {
    if (scheduler.pumpFrame() != 0) return false;
    if (tree.dirtyCount() != 0) return false;
  }
  if (tree.size() != 7) return false;
  for (runtime::NodeId id = 0; id < 7; ++id) {
    if (!tree.isInstantiated(id)) return false;
  }
  return true;
}

// Named case: one dirty pump presents exactly one frame, then the loop
// goes idle again — further pumps present nothing.
static bool check_dirty_pump_presents_one_then_idle() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  tree.markDirty(2);
  if (!scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 1) return false;
  if (scheduler.framesPresented() != 1) return false;
  if (scheduler.hasPending()) return false;
  for (int i = 0; i < 100; ++i) {
    if (scheduler.pumpFrame() != 0) return false;
  }
  return scheduler.framesPresented() == 1;
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
  RUN_CASE(check_no_pending_when_clean);
  RUN_CASE(check_hundred_idle_pumps_present_zero_frames);
  RUN_CASE(check_hundred_idle_pumps_touch_zero_nodes);
  RUN_CASE(check_dirty_pump_presents_one_then_idle);
  if (failures > 0) {
    std::printf("idle_frames_test: %d failing\n", failures);
    return 1;
  }
  std::printf("idle_frames_test: all passing\n");
  return 0;
}
