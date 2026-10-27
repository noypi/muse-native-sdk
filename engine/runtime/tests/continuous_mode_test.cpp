// T4 contract tests — FR-RUNTIME-006 / AC-RUNTIME-015 (TC-RUNTIME-015).
// Declared continuous mode: reschedules every pump until released, then
// the application returns to zero idle frames.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Binds ONLY to the T4 `Interface:` line; never to implementation.
#include <cstdint>
#include <cstdio>

#include "runtime/frame.h"
#include "runtime/tree.h"

namespace {

// Same 3-level binary tree fixture as idle_frames_test.cpp:
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

// Named case: enabling continuous reschedules every pump — N consecutive
// pumps each present exactly one frame.
static bool check_continuous_reschedules_every_pump() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.setContinuous(1, true);
  for (int i = 0; i < 5; ++i) {
    if (!scheduler.hasPending()) return false;
    if (scheduler.pumpFrame() != 1) return false;
  }
  if (scheduler.framesPresented() != 5) return false;
  // Still driving after 5 pumps.
  return scheduler.hasPending();
}

// Named case: disabling continuous returns to idle — next pump presents
// nothing when nothing is dirty.
static bool check_disable_continuous_returns_to_idle() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.setContinuous(1, true);
  if (scheduler.pumpFrame() != 1) return false;
  scheduler.setContinuous(1, false);
  if (scheduler.hasPending()) return false;
  for (int i = 0; i < 10; ++i) {
    if (scheduler.pumpFrame() != 0) return false;
  }
  return scheduler.framesPresented() == 1;
}

// Named case: releaseOwner stops that owner's continuous driver — pumps go
// idle with zero further frames.
static bool check_release_owner_stops_continuous() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.setContinuous(4, true);
  if (scheduler.pumpFrame() != 1) return false;
  if (scheduler.pumpFrame() != 1) return false;
  scheduler.releaseOwner(4);
  if (scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 0) return false;
  return scheduler.framesPresented() == 2;
}

// Named case: setContinuous(false) / releaseOwner for an unknown owner is
// a no-op — never crashes, never schedules, idle stays idle.
static bool check_unknown_owner_is_noop() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.setContinuous(777, false);
  scheduler.releaseOwner(777);
  if (scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 0) return false;
  // Unknown release must not disturb a live continuous driver.
  scheduler.setContinuous(2, true);
  scheduler.releaseOwner(888);
  if (scheduler.pumpFrame() != 1) return false;
  scheduler.setContinuous(2, false);
  return scheduler.framesPresented() == 1;
}

// Named case: continuous drivers are per-owner — releasing one owner leaves
// the other owner's driver running.
static bool check_continuous_is_per_owner() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.setContinuous(1, true);
  scheduler.setContinuous(2, true);
  if (scheduler.pumpFrame() != 1) return false;
  scheduler.releaseOwner(1);
  if (!scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 1) return false;
  scheduler.setContinuous(2, false);
  if (scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 0) return false;
  return scheduler.framesPresented() == 2;
}

// Named case: continuous + dirty interplay — disabling continuous with a
// dirty node pending still presents the dirty frame once, then goes idle.
static bool check_continuous_disable_with_dirty_presents_once() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.setContinuous(1, true);
  if (scheduler.pumpFrame() != 1) return false;
  scheduler.setContinuous(1, false);
  tree.markDirty(3);
  if (!scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 1) return false;
  if (scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 0) return false;
  return scheduler.framesPresented() == 2;
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
  RUN_CASE(check_continuous_reschedules_every_pump);
  RUN_CASE(check_disable_continuous_returns_to_idle);
  RUN_CASE(check_release_owner_stops_continuous);
  RUN_CASE(check_unknown_owner_is_noop);
  RUN_CASE(check_continuous_is_per_owner);
  RUN_CASE(check_continuous_disable_with_dirty_presents_once);
  if (failures > 0) {
    std::printf("continuous_mode_test: %d failing\n", failures);
    return 1;
  }
  std::printf("continuous_mode_test: all passing\n");
  return 0;
}
