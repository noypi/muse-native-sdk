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
#include <climits>
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
  // 11. F1 soak repro: CRITICAL at soak scale flushes exactly 50% of
  //     non-visible with 64-bit arithmetic (no 32-bit overflow).
  {
    VramState s = State(60000000, 50129232, 2322432, true);
    TrimOutcome o = applyTrim(s, TrimLevel::kRunningCritical);
    assert(o.status == Status::kOk);
    assert(o.flushedBytes == 23903400);  // 50% of (50129232 - 2322432).
    assert(s.usedBytes == 26225832);
    assert(s.visibleBytes == 2322432);
    printf("PASS f1-critical-soak-scale-50pct\n");
  }
  // 12. F1 soak repro: BACKGROUND at soak scale flushes all non-visible,
  //     retains visible.
  {
    VramState s = State(40000000, 30000000, 1000000, true);
    TrimOutcome o = applyTrim(s, TrimLevel::kBackground);
    assert(o.status == Status::kOk);
    assert(o.flushedBytes == 29000000);  // all of (30000000 - 1000000).
    assert(s.usedBytes == 1000000);
    assert(s.visibleBytes == 1000000);
    printf("PASS f1-background-soak-scale-full-flush\n");
  }
  // 13. CRITICAL overflow boundary: nonVisible straddling the old 32-bit
  //     overflow threshold (~42.9M for pct=50: 42949672*50 < 2^31,
  //     42949674*50 > 2^31-1). Exact 50% fractions both sides.
  {
    VramState s = State(60000000, 43949672, 1000000, true);
    TrimOutcome o = applyTrim(s, TrimLevel::kRunningCritical);
    assert(o.status == Status::kOk);
    assert(o.flushedBytes == 21474836);  // 50% of 42949672.
    assert(s.usedBytes == 22474836);
    assert(s.visibleBytes == 1000000);
    VramState t = State(60000000, 43949674, 1000000, true);
    TrimOutcome p = applyTrim(t, TrimLevel::kRunningCritical);
    assert(p.status == Status::kOk);
    assert(p.flushedBytes == 21474837);  // 50% of 42949674.
    assert(t.usedBytes == 22474837);
    assert(t.visibleBytes == 1000000);
    printf("PASS critical-overflow-boundary-42_9M\n");
  }
  // 14. BACKGROUND overflow boundary: nonVisible straddling the old 32-bit
  //     overflow threshold (~21.5M for pct=100: 21474836*100 < 2^31,
  //     21474837*100 > 2^31-1). Full flush both sides, visible retained.
  {
    VramState s = State(40000000, 22474836, 1000000, true);
    TrimOutcome o = applyTrim(s, TrimLevel::kBackground);
    assert(o.status == Status::kOk);
    assert(o.flushedBytes == 21474836);  // all of 21474836.
    assert(s.usedBytes == 1000000);
    assert(s.visibleBytes == 1000000);
    VramState t = State(40000000, 22474837, 1000000, true);
    TrimOutcome p = applyTrim(t, TrimLevel::kBackground);
    assert(p.status == Status::kOk);
    assert(p.flushedBytes == 21474837);  // all of 21474837.
    assert(t.usedBytes == 1000000);
    assert(t.visibleBytes == 1000000);
    printf("PASS background-overflow-boundary-21_5M\n");
  }
  // 15. Hardening (a): every invalid shape (usedBytes<0, visibleBytes<0,
  //     visibleBytes>usedBytes) is rejected with kInvalidArg and zero state
  //     mutation, on every trim tier. Expectations from the header contract
  //     (invalid state -> kInvalidArg, no mutation), never from the impl.
  {
    const TrimLevel kTiers[] = {TrimLevel::kRunningLow,
                               TrimLevel::kRunningCritical,
                               TrimLevel::kBackground};
    VramState bad[3] = {
        State(2000, -100, 0, true),    // usedBytes < 0
        State(2000, 500, -10, true),    // visibleBytes < 0
        State(2000, 300, 400, false),   // visibleBytes > usedBytes
    };
    for (int i = 0; i < 3; ++i) {
      for (int t = 0; t < 3; ++t) {
        VramState s = bad[i];
        TrimOutcome o = applyTrim(s, kTiers[t]);
        assert(o.status == Status::kInvalidArg);
        assert(o.flushedBytes == 0);
        assert(s.capBytes == bad[i].capBytes);
        assert(s.usedBytes == bad[i].usedBytes);
        assert(s.visibleBytes == bad[i].visibleBytes);
        assert(s.prefetchEnabled == bad[i].prefetchEnabled);
      }
    }
    printf("PASS invalid-shapes-rejected-zero-mutation-all-tiers\n");
  }
  // 16. Hardening (b): INT_MAX-scale nonVisible clamp — for all three tiers,
  //     flush stays within [0, nonVisible] and used never goes negative.
  //     Spec: flushes are charged to non-visible only (visible-never-evicted).
  {
    const TrimLevel kTiers[] = {TrimLevel::kRunningLow,
                               TrimLevel::kRunningCritical,
                               TrimLevel::kBackground};
    for (int t = 0; t < 3; ++t) {
      VramState s = State(INT_MAX, INT_MAX, 0, true);
      const int nonVisible = s.usedBytes - s.visibleBytes;
      TrimOutcome o = applyTrim(s, kTiers[t]);
      assert(o.status == Status::kOk);
      assert(o.flushedBytes >= 0);
      assert(o.flushedBytes <= nonVisible);
      assert(s.usedBytes >= 0);
      assert(s.usedBytes == nonVisible - o.flushedBytes);
      assert(s.visibleBytes == 0);
    }
    // With a visible residue, BACKGROUND must retain exactly visible.
    {
      VramState s = State(INT_MAX, INT_MAX, 1000, true);
      TrimOutcome o = applyTrim(s, TrimLevel::kBackground);
      assert(o.status == Status::kOk);
      assert(o.flushedBytes == INT_MAX - 1000);
      assert(s.usedBytes == 1000);
      assert(s.visibleBytes == 1000);
    }
    printf("PASS intmax-scale-clamp-all-tiers\n");
  }
  // 17. Hardening (c): negative usage never fits, and the 4GiB->6GiB
  //     interpolation is monotone non-decreasing (48MiB at 4GiB .. 128MiB
  //     at 6GiB per the header contract).
  {
    const size_t kG = 1024ULL * 1024ULL * 1024ULL;
    assert(!withinProfileBound(-1, 3 * kG));
    assert(!withinProfileBound(-1, 5 * kG));
    assert(!withinProfileBound(-1, 8 * kG));
    assert(!withinProfileBound(INT_MIN, 8 * kG));
    const size_t kRams[] = {4 * kG, 9 * kG / 2, 5 * kG, 11 * kG / 2, 6 * kG};
    int prevCap = 0;
    for (int r = 0; r < 5; ++r) {
      // Cap = largest m (MiB) admitted; [48,128] bracket per header edges.
      int cap = 0;
      for (int m = 48; m <= 128; ++m) {
        if (withinProfileBound(m * kMiB, kRams[r])) cap = m;
      }
      assert(cap >= 48 && cap <= 128);
      assert(cap >= prevCap);  // monotone non-decreasing in RAM class.
      prevCap = cap;
    }
    printf("PASS negative-usage-denied-bound-monotone\n");
  }
  printf("ALL VRAM_TRIM TESTS PASSED\n");
  return 0;
}
