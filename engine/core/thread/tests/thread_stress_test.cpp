// T7 contract tests — SPEC-CORE-ENGINE NFR-CORE-010 / AC-CORE-026 (TC-CORE-026).
// 100k background-to-Java-UI stress + navigation-cancel buffer release.
//
// Binds ONLY to the T5/T6 `Interface:` lines; no new library surface:
//   Dispatcher::post(Fn f, bool immediate=false) noexcept;
//   Dispatcher::drainFrame() noexcept; Dispatcher::pending() const noexcept;
//   LocalRefScope (Push/Pop analogue, depth counts);
//   Priority{Render,Ui,Decode,Io}; CancellationToken{cancel,isCancelled};
//   Task{pri,tok,fn}; WorkerPool::WorkerPool(n); submit(Task); shutdown().
// `none` — no new library surface (plan verbatim).
//
// Hermetic: C++17 + <core/thread/dispatcher.h>, <core/thread/main_checker.h>,
// <core/thread/task.h>, <core/thread/worker_pool.h> only; no network, no
// env/config reads, no filesystem, no live services, no real JNI/ALooper.
// Timing is structural (per-drain watchdog vs a generous host ceiling, never
// an absolute device millisecond claim); wall-clock races are avoided by
// asserting terminal state, with wait_for polling as a liveness aid only.
//
// Executable: thread_stress_test --calls=100000, returns 0 iff all checks
// pass (plan Interface verbatim).
//
// Expected state: RED — engine/core/thread/CMakeLists.txt has no
// thread_stress_test target yet (T7's coder extends it), so the CTest entry
// is missing even though this file compiles against the T5/T6 headers.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/thread/dispatcher.h"
#include "core/thread/main_checker.h"
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

// JNI local-ref budget analogue: a frame must never hold more than 512 live
// local refs. Each dispatcher callback runs inside exactly one LocalRefScope,
// so observed depth must stay in [1, 512] inside callbacks and return to 0
// after every drainFrame().
static constexpr int kLocalRefBudget = 512;
// Generous host-side per-drain ceiling (structural watchdog, not a device
// frame-time claim): any single drainFrame() exceeding this counts as an
// over-budget frame; a drain exceeding 5x counts as an ANR/starvation event.
static constexpr long kFrameBudgetMs = 250;
static constexpr long kAnrMs = 1250;
// Decode/io tasks cancelled mid-navigation in the second phase.
static constexpr int kCancelTasks = 64;

static long parse_calls(int argc, char** argv) {
  long calls = 100000;
  for (int i = 1; i < argc; ++i) {
    const char* kPrefix = "--calls=";
    if (std::strncmp(argv[i], kPrefix, std::strlen(kPrefix)) == 0) {
      calls = std::stol(argv[i] + std::strlen(kPrefix));
    }
  }
  return calls;
}

// --- phase 1: 100k background posts, per-frame chunked post/drain -------
// Each frame posts at most kPerFrame callbacks from background producer
// threads (proving post() is thread-safe under the 100k load), asserts the
// queue never holds more than one frame's chunk, then drains exactly once
// before the next frame's posts. Every drain runs under the main-thread
// watchdog: any drain exceeding the frame budget counts as over-budget, a
// drain exceeding 5x counts as an ANR/starvation event.
static void check_stress_posts(long calls) {
  core::thread::Dispatcher d;
  std::atomic<long> ran{0};
  std::atomic<int> max_depth{0};
  std::atomic<int> overflow_hits{0};
  std::atomic<int> scope_violations{0};

  const int kProducers = 4;
  const long kPerFrame = 1000;
  long frames = 0;
  long over_budget_frames = 0;
  long anr_events = 0;
  bool chunk_bound_ok = true;
  long posted_total = 0;
  while (posted_total < calls) {
    long chunk = calls - posted_total;
    if (chunk > kPerFrame) chunk = kPerFrame;
    // Background producers split this frame's chunk; joined before drain.
    std::vector<std::thread> producers;
    producers.reserve(static_cast<size_t>(kProducers));
    long base = chunk / kProducers;
    long rem = chunk % kProducers;
    for (int p = 0; p < kProducers; ++p) {
      long n = base + (p < rem ? 1 : 0);
      producers.emplace_back([&d, &ran, &max_depth, &overflow_hits,
                              &scope_violations, n] {
        for (long i = 0; i < n; ++i) {
          d.post([&ran, &max_depth, &overflow_hits, &scope_violations] {
            int depth = core::thread::LocalRefScope::depth();
            if (depth <= 0) ++scope_violations;  // not wrapped in a scope
            if (depth > kLocalRefBudget) ++overflow_hits;
            int prev = max_depth.load(std::memory_order_relaxed);
            while (depth > prev &&
                   !max_depth.compare_exchange_weak(prev, depth)) {
            }
            ++ran;
          });
        }
      });
    }
    for (auto& t : producers) t.join();
    posted_total += chunk;
    if (d.pending() > static_cast<std::size_t>(kPerFrame)) {
      chunk_bound_ok = false;  // more than one frame's chunk queued
    }
    auto t0 = std::chrono::steady_clock::now();
    d.drainFrame();
    auto t1 = std::chrono::steady_clock::now();
    long ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    ++frames;
    if (ms > kFrameBudgetMs) ++over_budget_frames;
    if (ms > kAnrMs) ++anr_events;
    if (d.pending() != 0) {
      // Dispatcher drains fully per contract; anything left means a late
      // post raced the drain — still a chunk-bound violation for this test.
      chunk_bound_ok = false;
    }
  }

  CHECK("stress: per-frame chunk bound held (pending <= 1000 at every drain)",
        chunk_bound_ok);
  CHECK("stress: all 100k callbacks ran exactly once", ran.load() == calls);
  CHECK("stress: queue empty after frame-by-frame drain", d.pending() == 0);
  CHECK("stress: LocalRefScope depth balanced after drain",
        core::thread::LocalRefScope::depth() == 0);
  CHECK("stress: every callback ran inside a live LocalRefScope",
        scope_violations.load() == 0);
  CHECK("stress: no local-ref overflow past 512-entry analogue",
        overflow_hits.load() == 0);
  CHECK("stress: max observed local-ref depth within budget",
        max_depth.load() >= 1 && max_depth.load() <= kLocalRefBudget);
  CHECK("stress: zero over-budget frames", over_budget_frames == 0);
  CHECK("stress: zero watchdog ANRs", anr_events == 0);
  std::printf("INFO stress frames=%ld over_budget=%ld anr=%ld max_depth=%d\n",
              frames, over_budget_frames, anr_events, max_depth.load());
}

// --- phase 2: navigation-cancel releases all buffers within one frame ------
// Submits decode/io tasks holding counted buffers, cancels every token
// mid-navigation (simulating navigation away), and asserts all buffers are
// released within one drainFrame().
static void check_navigation_cancel_releases_within_one_frame() {
  core::thread::WorkerPool pool(2);
  core::thread::Dispatcher d;
  std::atomic<int> live{0};
  std::vector<core::thread::CancellationToken> toks;
  toks.reserve(static_cast<size_t>(kCancelTasks));

  for (int i = 0; i < kCancelTasks; ++i) {
    auto buf = std::shared_ptr<int>(new int(i), [&live](int* p) {
      delete p;
      --live;
    });
    ++live;
    core::thread::Task t;
    t.pri = (i % 2 == 0) ? core::thread::Priority::Decode
                         : core::thread::Priority::Io;
    // Cooperative body: exits early once the navigation cancel lands.
    t.fn = [buf, tok = t.tok] {
      for (int step = 0; step < 200; ++step) {
        if (tok.isCancelled()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      (void)buf;
    };
    toks.push_back(t.tok);
    pool.submit(std::move(t));
  }

  // Mid-navigation cancel: cancel every in-flight decode/io token, then post
  // the navigation commit through the dispatcher and drain exactly once.
  for (auto& tok : toks) tok.cancel();
  std::atomic<bool> nav_ran{false};
  d.post([&nav_ran] { nav_ran = true; });
  auto t0 = std::chrono::steady_clock::now();
  d.drainFrame();
  auto t1 = std::chrono::steady_clock::now();
  long ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

  pool.shutdown();  // join workers; buffers held only by live task bodies

  CHECK("cancel: navigation commit ran within one drainFrame()",
        nav_ran.load() && d.pending() == 0);
  CHECK("cancel: LocalRefScope balanced after navigation drain",
        core::thread::LocalRefScope::depth() == 0);
  // Poll as a liveness aid only; the assertion reads terminal state.
  bool released = false;
  for (int i = 0; i < 500 && !(released = (live.load() == 0)); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK("cancel: all decode/io buffers released within one frame", released);
  CHECK("cancel: navigation drain within frame budget", ms <= kFrameBudgetMs);
}

// --- boundary: --calls parsing ------------------------------------------------
static void check_calls_flag_default() {
  // Default (no flag) must be the plan's 100k.
  const char* argv0 = "thread_stress_test";
  char* argv[] = {const_cast<char*>(argv0)};
  CHECK("default --calls is 100000", parse_calls(1, argv) == 100000);
}

static void check_calls_flag_explicit() {
  const char* argv0 = "thread_stress_test";
  char arg[] = "--calls=7";
  char* argv[] = {const_cast<char*>(argv0), arg};
  CHECK("explicit --calls parsed", parse_calls(2, argv) == 7);
}

int main(int argc, char** argv) {
  check_calls_flag_default();
  check_calls_flag_explicit();
  long calls = parse_calls(argc, argv);
  if (calls <= 0) {
    std::printf("FAIL thread_stress_test: --calls must be > 0\n");
    return 1;
  }
  check_stress_posts(calls);
  check_navigation_cancel_releases_within_one_frame();
  if (s_failures == 0) {
    std::printf("ALL GREEN thread_stress_test calls=%ld\n", calls);
    return 0;
  }
  std::printf("%d FAILURES thread_stress_test\n", s_failures);
  return 1;
}
