// T1 contract tests — NFR-CORE-009 / AC-CORE-019 (TC-CORE-019).
// Render path allocates exclusively from the per-frame arena and
// pre-sized pools; a test build fails on any global-heap allocation
// during frame execution.
//
// Binds ONLY to the T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + planned headers <core/mem/arena.h>, <core/mem/pool.h>
// only; no network, no env/config reads, no filesystem, no live services.
//
// Expected state: RED — the planned headers do not exist yet, so this
// file fails to compile until the coder implements T1.
#include <cstddef>
#include <cstdio>
#include <new>

#include "core/mem/arena.h"
#include "core/mem/pool.h"

// --- tiny harness (matches engine/runtime/tests convention) ----------------
static int s_failures = 0;

#define CHECK(name, expr)                                     \
  do {                                                        \
    if (!(expr)) {                                            \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                           \
    } else {                                                  \
      std::printf("PASS %s\n", name);                         \
    }                                                         \
  } while (0)

// --- cases ----------------------------------------------------------------

// Render-path allocation from the arena passes under the guard.
static void check_arena_alloc_passes_under_guard() {
  unsigned char buf[1024];
  core::mem::FrameArena arena(buf, sizeof(buf));
  {
    core::mem::RenderGuard guard(true);
    void* p = arena.allocate(128, 8);
    CHECK("arena alloc under guard succeeds", p != nullptr);
    CHECK("no global heap used by arena alloc",
          !core::mem::RenderGuard::globalHeapUsed());
  }
}

// Render-path allocation from a pre-sized pool passes under the guard.
static void check_pool_alloc_passes_under_guard() {
  core::mem::Pool<int, 4> pool;
  {
    core::mem::RenderGuard guard(true);
    int* a = pool.acquire();
    CHECK("pool acquire under guard succeeds", a != nullptr);
    CHECK("no global heap used by pool acquire",
          !core::mem::RenderGuard::globalHeapUsed());
    pool.release(a);
  }
}

// A global-heap allocation while the guard is on trips the failure.
static void check_global_new_trips_guard() {
  {
    core::mem::RenderGuard guard(true);
    int* leak = new (std::nothrow) int(42);  // intentional trip wire
    CHECK("global new trips guard",
          core::mem::RenderGuard::globalHeapUsed());
    delete leak;
  }
}

// Guard off: no trip is recorded for ordinary allocation.
static void check_guard_off_records_nothing() {
  {
    core::mem::RenderGuard guard(false);
    int* p = new (std::nothrow) int(7);
    CHECK("guard off does not flag", !core::mem::RenderGuard::globalHeapUsed());
    delete p;
  }
}

// Pool exhaustion returns nullptr instead of touching the heap.
static void check_pool_exhaustion_returns_nullptr() {
  core::mem::Pool<int, 2> pool;
  int* a = pool.acquire();
  int* b = pool.acquire();
  CHECK("pool fills to capacity", a != nullptr && b != nullptr);
  {
    core::mem::RenderGuard guard(true);
    int* c = pool.acquire();
    CHECK("exhausted pool returns nullptr", c == nullptr);
    CHECK("exhausted pool does not touch heap",
          !core::mem::RenderGuard::globalHeapUsed());
  }
  pool.release(a);
  pool.release(b);
  CHECK("pool reusable after release", pool.acquire() != nullptr);
}

// Frame sequence: guard per frame, arena reset between frames.
static void check_frame_sequence_arena_guard_reset() {
  unsigned char buf[512];
  core::mem::FrameArena arena(buf, sizeof(buf));
  for (int frame = 0; frame < 3; ++frame) {
    core::mem::RenderGuard guard(true);
    void* p = arena.allocate(256, 8);
    CHECK("frame alloc succeeds", p != nullptr);
    CHECK("frame alloc heap-free",
          !core::mem::RenderGuard::globalHeapUsed());
    arena.reset();
    CHECK("used is zero after frame reset", arena.used() == 0);
  }
}

int main() {
  check_arena_alloc_passes_under_guard();
  check_pool_alloc_passes_under_guard();
  check_global_new_trips_guard();
  check_guard_off_records_nothing();
  check_pool_exhaustion_returns_nullptr();
  check_frame_sequence_arena_guard_reset();
  if (s_failures == 0) {
    std::printf("ALL GREEN allocator_guard_test\n");
    return 0;
  }
  std::printf("%d FAILURES allocator_guard_test\n", s_failures);
  return 1;
}
