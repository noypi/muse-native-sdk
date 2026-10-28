// T6 contract tests — NFR-CORE-010 / AC-CORE-025 (TC-CORE-025).
// Worker tasks carry priority + cancellation token; cancellation is
// observable and releases resources (RAII, no leak).
//
// Binds ONLY to the T6 `Interface:` line; never to implementation.
// Hermetic: C++17 + planned headers <core/thread/task.h> and
// <core/thread/worker_pool.h> only; no network, no env/config reads,
// no filesystem, no live services.
//
// Expected state: RED — <core/thread/task.h> / <core/thread/worker_pool.h>
// do not exist yet, so this file fails to compile until the coder
// implements T6.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/thread/task.h"
#include "core/thread/worker_pool.h"

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

// Wait up to ~5s for a predicate (poll-based, no wall-clock dependence
// beyond a generous timeout; never asserts exact timing).
template <typename Pred>
static bool wait_for(Pred pred) {
  for (int i = 0; i < 500 && !pred(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return pred();
}

// --- cases ----------------------------------------------------------------

// A submitted task runs to completion.
static void check_task_runs() {
  core::thread::WorkerPool pool(2);
  std::atomic<bool> ran{false};
  core::thread::Task t;
  t.pri = core::thread::Priority::Ui;
  t.fn = [&ran] { ran = true; };
  pool.submit(std::move(t));
  CHECK("task ran", wait_for([&ran] { return ran.load(); }));
  pool.shutdown();
}

// CancellationToken: cancel() is observable via isCancelled().
static void check_token_cancel_observable() {
  core::thread::CancellationToken tok;
  CHECK("token starts uncancelled", !tok.isCancelled());
  tok.cancel();
  CHECK("cancel is observable", tok.isCancelled());
}

// Token copies share state (cancelling one view cancels all holders).
static void check_token_copies_share_state() {
  core::thread::CancellationToken a;
  core::thread::CancellationToken b = a;
  CHECK("copy starts uncancelled", !b.isCancelled());
  a.cancel();
  CHECK("cancel visible through copy", b.isCancelled());
}

// A cancelled task observes cancellation and skips its work.
static void check_cancelled_task_observes_token() {
  core::thread::WorkerPool pool(1);
  // Occupy the single worker so the second task stays queued.
  std::promise<void> gate;
  auto gate_future = gate.get_future();
  core::thread::Task blocker;
  blocker.pri = core::thread::Priority::Render;
  blocker.fn = [&gate_future] { gate_future.wait(); };
  pool.submit(std::move(blocker));

  std::atomic<bool> did_work{false};
  std::atomic<bool> saw_cancel{false};
  core::thread::Task t;
  t.pri = core::thread::Priority::Decode;
  t.fn = [&did_work, &saw_cancel, tok = t.tok] {
    if (tok.isCancelled()) {
      saw_cancel = true;
      return;
    }
    did_work = true;
  };
  // Cancel before it can run.
  t.tok.cancel();
  pool.submit(std::move(t));
  gate.set_value();
  CHECK("cancelled task observed cancellation",
        wait_for([&saw_cancel] { return saw_cancel.load(); }));
  // Give the worker a chance to (incorrectly) run the body.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK("cancelled task skipped its work", !did_work.load());
  pool.shutdown();
}

// Priority: render/ui run ahead of decode/io under contention.
// Single worker + gate: queue one task per priority while the worker is
// blocked, then verify high-priority tasks complete first.
static void check_priority_order_under_contention() {
  bool ordered = false;
  for (int round = 0; round < 5 && !ordered; ++round) {
    core::thread::WorkerPool pool(1);
    std::promise<void> gate;
    auto gate_future = gate.get_future();
    core::thread::Task blocker;
    blocker.pri = core::thread::Priority::Render;
    blocker.fn = [&gate_future] { gate_future.wait(); };
    pool.submit(std::move(blocker));

    std::mutex mu;
    std::vector<core::thread::Priority> order;
    auto make = [&](core::thread::Priority p) {
      core::thread::Task t;
      t.pri = p;
      t.fn = [&mu, &order, p] {
        std::lock_guard<std::mutex> lock(mu);
        order.push_back(p);
      };
      return t;
    };
    // Submit low priorities first so any ordering effect is the queue's.
    pool.submit(make(core::thread::Priority::Io));
    pool.submit(make(core::thread::Priority::Decode));
    pool.submit(make(core::thread::Priority::Ui));
    pool.submit(make(core::thread::Priority::Render));
    gate.set_value();
    bool done = wait_for([&mu, &order] {
      std::lock_guard<std::mutex> lock(mu);
      return order.size() == 4;
    });
    if (done) {
      std::lock_guard<std::mutex> lock(mu);
      // Render/Ui (first two) must both precede Decode/Io (last two).
      const bool hi_first =
          (order[0] == core::thread::Priority::Render ||
           order[0] == core::thread::Priority::Ui) &&
          (order[1] == core::thread::Priority::Render ||
           order[1] == core::thread::Priority::Ui) &&
          order[0] != order[1];
      const bool lo_last =
          (order[2] == core::thread::Priority::Decode ||
           order[2] == core::thread::Priority::Io) &&
          (order[3] == core::thread::Priority::Decode ||
           order[3] == core::thread::Priority::Io) &&
          order[2] != order[3];
      ordered = hi_first && lo_last;
    }
    pool.shutdown();
  }
  CHECK("render/ui run ahead of decode/io under contention", ordered);
}

// RAII: cancelling an in-flight decode/io task releases its buffer —
// the destructor count proves no leak after cancel + shutdown.
static void check_cancel_releases_buffer() {
  std::atomic<int> live{0};
  {
    core::thread::WorkerPool pool(2);
    for (int i = 0; i < 8; ++i) {
      auto buf = std::shared_ptr<int>(
          new int(42), [&live](int* p) {
            delete p;
            --live;
          });
      ++live;
      core::thread::Task t;
      t.pri = core::thread::Priority::Decode;
      t.fn = [buf, tok = t.tok] {
        for (int step = 0; step < 100; ++step) {
          if (tok.isCancelled()) return;  // cooperative exit
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        (void)buf;
      };
      if (i % 2 == 0) t.tok.cancel();
      pool.submit(std::move(t));
    }
    pool.shutdown();
  }
  CHECK("all task buffers released after cancel+shutdown",
        wait_for([&live] { return live.load() == 0; }));
}

// Boundary: submit after shutdown is safe (no crash, no hang).
static void check_submit_after_shutdown_safe() {
  core::thread::WorkerPool pool(1);
  pool.shutdown();
  core::thread::Task t;
  t.pri = core::thread::Priority::Io;
  t.fn = [] {};
  pool.submit(std::move(t));  // must not crash or hang
  CHECK("submit after shutdown is safe", true);
  pool.shutdown();  // double shutdown is safe
  CHECK("double shutdown is safe", true);
}

int main() {
  check_task_runs();
  check_token_cancel_observable();
  check_token_copies_share_state();
  check_cancelled_task_observes_token();
  check_priority_order_under_contention();
  check_cancel_releases_buffer();
  check_submit_after_shutdown_safe();
  if (s_failures == 0) {
    std::printf("ALL GREEN task_cancellation_test\n");
    return 0;
  }
  std::printf("%d FAILURES task_cancellation_test\n", s_failures);
  return 1;
}
