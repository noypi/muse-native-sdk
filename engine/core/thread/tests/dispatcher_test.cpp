// T5 contract tests — NFR-CORE-010 / AC-CORE-024 (TC-CORE-024).
// Batched frame-aligned dispatcher: non-immediate posts batch until
// drainFrame() (FR-RUNTIME-001 boundary); immediate=true bypasses.
//
// Binds ONLY to the T5 `Interface:` line; never to implementation.
// Hermetic: C++17 + planned headers <core/thread/dispatcher.h> and
// <core/thread/main_checker.h> only; no network, no env/config reads,
// no filesystem, no live services, no JNI.
//
// Expected state: RED — <core/thread/dispatcher.h> does not exist yet,
// so this file fails to compile until the coder implements T5.
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include "core/thread/dispatcher.h"
#include "core/thread/main_checker.h"

// --- tiny harness (matches engine/runtime/tests convention) ----------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

// --- cases ----------------------------------------------------------------

// Batched posts do not run before drainFrame; pending() counts them.
static void check_posts_batch_until_drain() {
  core::thread::Dispatcher d;
  int ran = 0;
  for (int i = 0; i < 10; ++i) {
    d.post([&ran] { ++ran; });
  }
  CHECK("ten posts pending before drain", d.pending() == 10);
  CHECK("nothing ran before drain", ran == 0);
  d.drainFrame();
  CHECK("all ten ran at drain", ran == 10);
  CHECK("nothing pending after drain", d.pending() == 0);
}

// 1000 batched posts drain in exactly one frame boundary (counter proof).
static void check_thousand_posts_drain_in_one_batch() {
  core::thread::Dispatcher d;
  std::atomic<int> ran{0};
  for (int i = 0; i < 1000; ++i) {
    d.post([&ran] { ++ran; });
  }
  CHECK("1000 posts pending", d.pending() == 1000);
  d.drainFrame();
  CHECK("1000 callbacks ran after one drainFrame", ran.load() == 1000);
  CHECK("queue empty after single drain", d.pending() == 0);
}

// immediate=true bypasses batching: executes without drainFrame.
static void check_immediate_bypasses_batching() {
  core::thread::Dispatcher d;
  int ran = 0;
  d.post([&ran] { ++ran; }, true);
  CHECK("immediate post ran without drain", ran == 1);
  CHECK("immediate post leaves nothing pending", d.pending() == 0);
}

// Mixed: immediate runs now, batched waits for the frame boundary.
static void check_mixed_immediate_and_batched() {
  core::thread::Dispatcher d;
  std::vector<int> order;
  d.post([&order] { order.push_back(1); });
  d.post([&order] { order.push_back(99); }, true);
  CHECK("immediate ran inline", order.size() == 1 && order[0] == 99);
  CHECK("batched still pending", d.pending() == 1);
  d.drainFrame();
  CHECK("batched ran at drain",
        order.size() == 2 && order[0] == 99 && order[1] == 1);
  CHECK("queue empty", d.pending() == 0);
}

// FIFO order within a batch (plan is silent; most defensible reading).
static void check_batch_preserves_fifo_order() {
  core::thread::Dispatcher d;
  std::vector<int> order;
  for (int i = 0; i < 8; ++i) {
    d.post([&order, i] { order.push_back(i); });
  }
  d.drainFrame();
  bool fifo = order.size() == 8;
  for (int i = 0; fifo && i < 8; ++i) fifo = (order[static_cast<size_t>(i)] == i);
  CHECK("batch preserves FIFO order", fifo);
}

// Boundary: drainFrame on an empty queue is safe and stays empty.
static void check_empty_drain_safe() {
  core::thread::Dispatcher d;
  CHECK("fresh dispatcher has nothing pending", d.pending() == 0);
  d.drainFrame();
  CHECK("empty drain stays empty", d.pending() == 0);
  d.drainFrame();
  CHECK("double empty drain stays empty", d.pending() == 0);
}

// Each callback runs wrapped in a LocalRefScope (Push/Pop analogue):
// depth returns to zero after the drain, no leak across callbacks.
static void check_callbacks_wrapped_in_local_ref_scope() {
  core::thread::Dispatcher d;
  int saw_nonzero = 0;
  for (int i = 0; i < 4; ++i) {
    d.post([&saw_nonzero] {
      if (core::thread::LocalRefScope::depth() > 0) ++saw_nonzero;
    });
  }
  d.drainFrame();
  CHECK("callbacks ran inside a live LocalRefScope", saw_nonzero == 4);
  CHECK("depth balanced after drain",
        core::thread::LocalRefScope::depth() == 0);
}

// Posts from a worker thread are visible to the main-thread drain.
static void check_post_from_worker_thread() {
  core::thread::Dispatcher d;
  int ran = 0;
  std::thread worker([&d, &ran] {
    for (int i = 0; i < 5; ++i) d.post([&ran] { ++ran; });
  });
  worker.join();
  CHECK("worker posts pending", d.pending() == 5);
  CHECK("worker posts not run before drain", ran == 0);
  d.drainFrame();
  CHECK("worker posts drained on frame boundary", ran == 5);
}

int main() {
  check_posts_batch_until_drain();
  check_thousand_posts_drain_in_one_batch();
  check_immediate_bypasses_batching();
  check_mixed_immediate_and_batched();
  check_batch_preserves_fifo_order();
  check_empty_drain_safe();
  check_callbacks_wrapped_in_local_ref_scope();
  check_post_from_worker_thread();
  if (s_failures == 0) {
    std::printf("ALL GREEN dispatcher_test\n");
    return 0;
  }
  std::printf("%d FAILURES dispatcher_test\n", s_failures);
  return 1;
}
