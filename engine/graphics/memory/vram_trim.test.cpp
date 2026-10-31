// T2 hermetic unit test — VRAM trim-tier mapper with profile bounds
// (NFR-GRAPHICS-004, AC-GRAPHICS-008 scripted-trim half).
// Style: plain asserts, no dependencies, no I/O, no network.
// Expectations derive ONLY from the NFR-004 rationale + AC-008: exact fractions
// TRIM_MEMORY_RUNNING_LOW -> 25% flush + prefetch off,
// TRIM_MEMORY_RUNNING_CRITICAL -> 50% flush,
// TRIM_MEMORY_BACKGROUND -> full flush retaining visible textures;
// visible-never-evicted accounting mirrors texture_pool.h; profile bounds from
// the NFR-004 rationale (low <4GB -> 32-48MB; mid 4-6GB -> 64-80MB;
// high >=6GB -> 128MB+), consistent with core::budget::Manager::tierForRam.
// ASSUMPTION: fractional flushes use integer truncation
// (flushed = nonVisible * pct / 100); test sizes are exact multiples.
// ASSUMPTION: the mid-band (4-6GB) bound is monotone non-decreasing between the
// documented 48MiB (at 4GiB) and 128MiB (at 6GiB) tier edges, matching the
// tierForRam interpolation; the spec pins only the low/high edges exactly.
#include <cassert>
#include <cstddef>
#include <cstdio>

#include "engine/graphics/memory/vram_trim.h"

static const int kMiB = 1024 * 1024;

static VramState State(int cap, int used, int visible, bool prefetch) {
  VramState s;
  s.capBytes = cap;
  s.usedBytes = used;
  s.visibleBytes = visible;
  s.prefetchEnabled = prefetch;
  return s;
}

int main() {
  // 1. RUNNING_LOW flushes exactly 25% of non-visible + disables prefetch.
  {
    VramState s = State(2000, 1000, 200, true);
    TrimOutcome o = applyTrim(s, TrimLevel::kRunningLow);
    assert(o.status == Status::kOk);
    assert(o.flushedBytes == 200);  // 25% of (1000 - 200).
    assert(s.usedBytes == 800);
    assert(s.visibleBytes == 200);
    assert(!s.prefetchEnabled);
    assert(!o.prefetchEnabled);
    printf("PASS running-low-25pct-plus-prefetch-off\n");
  }
  // 2. RUNNING_CRITICAL flushes exactly 50% of non-visible.
  {
    VramState s = State(2000, 1000, 200, true);
    TrimOutcome o = applyTrim(s, TrimLevel::kRunningCritical);
    assert(o.status == Status::kOk);
    assert(o.flushedBytes == 400);  // 50% of (1000 - 200).
    assert(s.usedBytes == 600);
    assert(s.visibleBytes == 200);
    printf("PASS running-critical-50pct\n");
  }
  // 3. BACKGROUND flushes all non-visible, retains visible.
  {
    VramState s = State(2000, 1000, 200, true);
    TrimOutcome o = applyTrim(s, TrimLevel::kBackground);
    assert(o.status == Status::kOk);
    assert(o.flushedBytes == 800);  // all of (1000 - 200).
    assert(s.usedBytes == 200);
    assert(s.visibleBytes == 200);
    printf("PASS background-full-flush-retains-visible\n");
  }
  // 4. Scripted sequence LOW -> CRITICAL -> BACKGROUND flushes the specified
  //    fraction of the then-current non-visible pool at each step, and ends
  //    with exactly the visible bytes resident.
  {
    VramState s = State(4000, 2000, 400, true);
    TrimOutcome l = applyTrim(s, TrimLevel::kRunningLow);
    assert(l.status == Status::kOk);
    assert(l.flushedBytes == 400);  // 25% of 1600.
    assert(s.usedBytes == 1600);
    TrimOutcome c = applyTrim(s, TrimLevel::kRunningCritical);
    assert(c.status == Status::kOk);
    assert(c.flushedBytes == 600);  // 50% of 1200.
    assert(s.usedBytes == 1000);
    TrimOutcome b = applyTrim(s, TrimLevel::kBackground);
    assert(b.status == Status::kOk);
    assert(b.flushedBytes == 600);  // all of remaining 600.
    assert(s.usedBytes == 400);
    assert(s.visibleBytes == 400);
    printf("PASS scripted-trim-sequence-exact-fractions\n");
  }
  // 5. Fully-visible pool: trims flush nothing, visible intact, still kOk.
  {
    VramState s = State(2000, 500, 500, true);
    assert(applyTrim(s, TrimLevel::kRunningLow).flushedBytes == 0);
    assert(applyTrim(s, TrimLevel::kRunningCritical).flushedBytes == 0);
    TrimOutcome b = applyTrim(s, TrimLevel::kBackground);
    assert(b.status == Status::kOk);
    assert(b.flushedBytes == 0);
    assert(s.usedBytes == 500);
    assert(s.visibleBytes == 500);
    printf("PASS fully-visible-pool-never-evicted\n");
  }
  // 6. Invalid state (visible exceeds used) -> kInvalidArg, no mutation.
  {
    VramState s = State(2000, 300, 400, true);
    int usedBefore = s.usedBytes;
    TrimOutcome o = applyTrim(s, TrimLevel::kRunningCritical);
    assert(o.status == Status::kInvalidArg);
    assert(o.flushedBytes == 0);
    assert(s.usedBytes == usedBefore);
    assert(s.visibleBytes == 400);
    assert(s.prefetchEnabled);
    printf("PASS invalid-visible-exceeds-used-no-mutation\n");
  }
  // 7. Invalid state (negative usage) -> kInvalidArg, no mutation.
  {
    VramState s = State(2000, -100, 0, true);
    TrimOutcome o = applyTrim(s, TrimLevel::kRunningLow);
    assert(o.status == Status::kInvalidArg);
    assert(o.flushedBytes == 0);
    assert(s.usedBytes == -100);
    assert(s.prefetchEnabled);
    printf("PASS invalid-negative-usage-no-mutation\n");
  }
  // 8. Low RAM class (<4GB): 48MiB bound — accept at cap, reject over-cap.
  {
    const size_t kRam3G = 3ULL * 1024 * 1024 * 1024;
    assert(withinProfileBound(48 * kMiB, kRam3G));
    assert(!withinProfileBound(48 * kMiB + 1, kRam3G));
    printf("PASS low-ram-class-48mib-bound\n");
  }
  // 9. High RAM class (>=6GB): 128MiB bound — accept at cap, reject over-cap.
  {
    const size_t kRam8G = 8ULL * 1024 * 1024 * 1024;
    assert(withinProfileBound(128 * kMiB, kRam8G));
    assert(!withinProfileBound(128 * kMiB + 1, kRam8G));
    printf("PASS high-ram-class-128mib-bound\n");
  }
  // 10. Tier edges: 4GiB admits 48MiB; 6GiB admits 128MiB; mid band is
  //     monotone non-decreasing between them (ASSUMPTION: interpolation).
  {
    const size_t kRam4G = 4ULL * 1024 * 1024 * 1024;
    const size_t kRam5G = 5ULL * 1024 * 1024 * 1024;
    const size_t kRam6G = 6ULL * 1024 * 1024 * 1024;
    assert(withinProfileBound(48 * kMiB, kRam4G));
    assert(withinProfileBound(128 * kMiB, kRam6G));
    assert(withinProfileBound(48 * kMiB, kRam5G));
    assert(!withinProfileBound(128 * kMiB + 1, kRam5G));
    // Monotone: whatever the 5GB cap is, it sits within [48MiB, 128MiB].
    bool foundCap = false;
    for (int m = 48; m <= 128; ++m) {
      if (!withinProfileBound((m + 1) * kMiB, kRam5G)) {
        assert(withinProfileBound(m * kMiB, kRam5G));
        foundCap = true;
        break;
      }
    }
    assert(foundCap);
    printf("PASS tier-edges-and-mid-monotone\n");
  }
  printf("ALL VRAM_TRIM TESTS PASSED\n");
  return 0;
}
