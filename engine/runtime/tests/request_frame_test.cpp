// T3 contract tests — FR-RUNTIME-006 / AC-RUNTIME-014 (TC-RUNTIME-014).
// Single-shot requestFrame: schedules exactly one frame, no reschedule.
// Also covers releaseOwner(ComponentId) (T0-noted T3 prerequisite): release
// clears a pending explicit request and continuous drivers for that owner.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Binds ONLY to the T3 `Interface:` line; never to implementation.
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

// Named case: requestFrame schedules exactly one frame — hasPending true,
// next pumpFrame returns 1.
static bool check_request_frame_schedules_one_frame() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  const runtime::ComponentId owner = 1;
  scheduler.requestFrame(owner);
  if (!scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 1) return false;
  return scheduler.framesPresented() == 1;
}

// Named case: no reschedule — the pump after a requested frame presents
// nothing when nothing else is dirty or requested.
static bool check_requested_frame_does_not_reschedule() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.requestFrame(1);
  if (scheduler.pumpFrame() != 1) return false;
  if (scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 0) return false;
  return scheduler.framesPresented() == 1;
}

// Named case: two owners requesting yields two frames, then idle
// (one shot each, no further reschedule).
static bool check_two_owners_request_two_frames_then_idle() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.requestFrame(1);
  scheduler.requestFrame(2);
  if (scheduler.pumpFrame() != 1) return false;
  if (scheduler.pumpFrame() != 1) return false;
  if (scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 0) return false;
  return scheduler.framesPresented() == 2;
}

// Named case: releaseOwner clears a pending explicit request — pump after
// release presents nothing and the loop is idle.
static bool check_release_owner_clears_pending_request() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.requestFrame(7);
  scheduler.releaseOwner(7);
  if (scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 0) return false;
  return scheduler.framesPresented() == 0;
}

// Named case: releaseOwner on an unknown owner is a no-op — never crashes,
// never schedules, idle stays idle.
static bool check_release_unknown_owner_is_noop() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.releaseOwner(999);
  if (scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 0) return false;
  // Unknown release must not disturb another owner's live request.
  scheduler.requestFrame(3);
  scheduler.releaseOwner(4242);
  if (scheduler.pumpFrame() != 1) return false;
  return scheduler.framesPresented() == 1;
}

// Named case: releaseOwner clears that owner's continuous driver too —
// after release, pumps go idle (continuous + explicit interplay).
static bool check_release_owner_clears_continuous_driver() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.setContinuous(5, true);
  if (scheduler.pumpFrame() != 1) return false;
  scheduler.releaseOwner(5);
  if (scheduler.hasPending()) return false;
  if (scheduler.pumpFrame() != 0) return false;
  return scheduler.framesPresented() == 1;
}

// Named case: explicit request composes with dirty-driven frames — a dirty
// pump consumes both, then the loop is idle (no leftover reschedule).
static bool check_request_with_dirty_pump_then_idle() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  tree.markDirty(2);
  scheduler.requestFrame(9);
  if (scheduler.pumpFrame() != 1) return false;
  if (scheduler.hasPending()) return false;
  for (int i = 0; i < 10; ++i) {
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
  RUN_CASE(check_request_frame_schedules_one_frame);
  RUN_CASE(check_requested_frame_does_not_reschedule);
  RUN_CASE(check_two_owners_request_two_frames_then_idle);
  RUN_CASE(check_release_owner_clears_pending_request);
  RUN_CASE(check_release_unknown_owner_is_noop);
  RUN_CASE(check_release_owner_clears_continuous_driver);
  RUN_CASE(check_request_with_dirty_pump_then_idle);
  if (failures > 0) {
    std::printf("request_frame_test: %d failing\n", failures);
    return 1;
  }
  std::printf("request_frame_test: all passing\n");
  return 0;
}
