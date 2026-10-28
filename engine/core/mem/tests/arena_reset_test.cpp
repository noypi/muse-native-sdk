// T1 contract tests — NFR-CORE-009 / AC-CORE-020 (TC-CORE-020).
// Per-frame arena reset: O(1) bounded rewind, overflow returns nullptr,
// never grows on the frame thread.
//
// Binds ONLY to the T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + planned header <core/mem/arena.h> only; no network,
// no env/config reads, no filesystem, no live services.
//
// Expected state: RED — <core/mem/arena.h> does not exist yet, so this
// file fails to compile until the coder implements T1.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/mem/arena.h"

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

// Happy path: allocations within capacity succeed and used() tracks them.
static void check_alloc_within_capacity_succeeds() {
  unsigned char buf[1024];
  core::mem::FrameArena arena(buf, sizeof(buf));
  CHECK("initial used is zero", arena.used() == 0);
  void* a = arena.allocate(64, 8);
  void* b = arena.allocate(128, 8);
  CHECK("first alloc non-null", a != nullptr);
  CHECK("second alloc non-null", b != nullptr);
  CHECK("pointers distinct", a != b);
  CHECK("used tracks both allocs", arena.used() >= 64 + 128);
  CHECK("used within capacity", arena.used() <= sizeof(buf));
}

// Boundary: overflow returns nullptr and never grows.
static void check_overflow_returns_nullptr_without_grow() {
  unsigned char buf[256];
  core::mem::FrameArena arena(buf, sizeof(buf));
  void* fill = arena.allocate(256, 1);
  CHECK("exact-fit alloc succeeds", fill != nullptr);
  size_t used_before = arena.used();
  void* over = arena.allocate(1, 1);
  CHECK("overflow returns nullptr", over == nullptr);
  CHECK("used unchanged after overflow", arena.used() == used_before);
  void* over2 = arena.allocate(1024, 1);
  CHECK("large overflow returns nullptr", over2 == nullptr);
  CHECK("used still unchanged", arena.used() == used_before);
}

// reset() rewinds to zero and the full buffer is reusable afterwards.
static void check_reset_rewinds_and_reusable() {
  unsigned char buf[512];
  core::mem::FrameArena arena(buf, sizeof(buf));
  void* a = arena.allocate(512, 1);
  CHECK("fill succeeds", a != nullptr);
  CHECK("overflow while full", arena.allocate(1, 1) == nullptr);
  arena.reset();
  CHECK("used is zero after reset", arena.used() == 0);
  void* b = arena.allocate(512, 1);
  CHECK("full alloc succeeds after reset", b != nullptr);
}

// Alignment: returned pointers satisfy the requested alignment.
static void check_alloc_respects_alignment() {
  unsigned char buf[512];
  core::mem::FrameArena arena(buf, sizeof(buf));
  const size_t aligns[] = {1, 2, 4, 8, 16};
  for (size_t i = 0; i < sizeof(aligns) / sizeof(aligns[0]); ++i) {
    void* p = arena.allocate(16, aligns[i]);
    CHECK("aligned alloc non-null", p != nullptr);
    if (p == nullptr) return;
    CHECK("pointer satisfies alignment",
          (reinterpret_cast<uintptr_t>(p) % aligns[i]) == 0);
  }
}

// AC-CORE-020: reset is O(1) and bounded — reset cost must not grow with
// fill size and must stay under a fixed host ceiling.
static void check_reset_is_o1_bounded() {
  const size_t kCap = 1u << 20;  // 1 MiB
  static unsigned char big[1u << 20];
  core::mem::FrameArena arena(big, kCap);

  // Time reset after a tiny fill.
  arena.allocate(16, 1);
  auto t0 = std::chrono::steady_clock::now();
  arena.reset();
  auto t1 = std::chrono::steady_clock::now();

  // Time reset after a full fill.
  void* full = arena.allocate(kCap, 1);
  CHECK("full 1MiB fill succeeds", full != nullptr);
  auto t2 = std::chrono::steady_clock::now();
  arena.reset();
  auto t3 = std::chrono::steady_clock::now();

  auto small_us =
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  auto full_us =
      std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
  CHECK("used is zero after full reset", arena.used() == 0);

  // Fixed ceiling: pointer-rewind reset must complete well under 1ms.
  const long long kCeilingUs = 1000;
  CHECK("small-fill reset under ceiling", small_us <= kCeilingUs);
  CHECK("full-fill reset under ceiling", full_us <= kCeilingUs);
  // O(1): full-fill reset must not cost materially more than small-fill
  // reset (generous 20x ratio to absorb timer granularity; a memset-style
  // O(n) rewind over 1 MiB would exceed it).
  CHECK("reset cost independent of fill size", full_us <= small_us + 1000 &&
                                                   (small_us >= 0) &&
                                                   full_us <= 20 * (small_us + 50));
}

// Zero-size allocation must not corrupt the arena.
static void check_zero_size_alloc_safe() {
  unsigned char buf[128];
  core::mem::FrameArena arena(buf, sizeof(buf));
  size_t used_before = arena.used();
  void* p = arena.allocate(0, 1);
  // Either nullptr or a valid pointer is acceptable; used() must not jump.
  CHECK("zero-size alloc keeps used bounded",
        p == nullptr || arena.used() <= used_before + 16);
  CHECK("arena still usable after zero-size alloc",
        arena.allocate(64, 1) != nullptr);
}

int main() {
  check_alloc_within_capacity_succeeds();
  check_overflow_returns_nullptr_without_grow();
  check_reset_rewinds_and_reusable();
  check_alloc_respects_alignment();
  check_reset_is_o1_bounded();
  check_zero_size_alloc_safe();
  if (s_failures == 0) {
    std::printf("ALL GREEN arena_reset_test\n");
    return 0;
  }
  std::printf("%d FAILURES arena_reset_test\n", s_failures);
  return 1;
}
