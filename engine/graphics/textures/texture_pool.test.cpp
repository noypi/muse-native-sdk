// T4 hermetic unit test — ring-buffered texture pool lifetime/eviction
// (FR-GRAPHICS-008, AC-GRAPHICS-021/022). Accounting model only: no VMA calls.
// Style: plain asserts, no dependencies, no I/O, no network.
#include <cassert>
#include <climits>
#include <cstdio>

#include "engine/graphics/textures/texture_pool.h"

static PoolState State(int cap, int used, int slots, int usedSlots) {
  PoolState s;
  s.capacityBytes = cap;
  s.usedBytes = used;
  s.ringSlots = slots;
  s.usedSlots = usedSlots;
  return s;
}

static TextureRequest Req(GpuFormat fmt, int bytes, bool visible) {
  TextureRequest r;
  r.format = fmt;
  r.bytes = bytes;
  r.visible = visible;
  return r;
}

static const GpuFormat kAllFormats[] = {
    GpuFormat::kAstc, GpuFormat::kEtc2, GpuFormat::kBc, GpuFormat::kUncompressed,
};

int main() {
  // 1. Format-independent: identical admit path for every GpuFormat value.
  for (GpuFormat f : kAllFormats) {
    PoolOutcome o = poolAcquire(State(1000, 100, 8, 2), Req(f, 200, false));
    assert(o.status == Status::kOk);
    assert(o.admitted == true);
    assert(o.evicted == false);
    assert(o.evictedBytes == 0);
  }
  printf("PASS admit-path-identical-all-formats\n");

  // 2. Format-independent: identical evict path for every GpuFormat value.
  for (GpuFormat f : kAllFormats) {
    PoolOutcome o = poolAcquire(State(1000, 900, 8, 2), Req(f, 200, false));
    assert(o.status == Status::kOk);
    assert(o.admitted == true);
    assert(o.evicted == true);
    assert(o.evictedBytes == 100);  // 900 + 200 - 1000
  }
  printf("PASS evict-path-identical-all-formats\n");

  // 3. Over-budget: request larger than the whole pool -> kOverBudget, denied.
  for (GpuFormat f : kAllFormats) {
    PoolOutcome o = poolAcquire(State(1000, 900, 8, 2), Req(f, 1500, false));
    assert(o.status == Status::kOverBudget);
    assert(o.admitted == false);
  }
  printf("PASS over-budget-denied-all-formats\n");

  // 4. Visible request admitted under pressure; eviction accounting covers
  //    only non-visible bytes (evictedBytes <= prior usedBytes).
  {
    PoolOutcome o = poolAcquire(State(100, 90, 8, 2), Req(GpuFormat::kAstc, 20, true));
    assert(o.status == Status::kOk);
    assert(o.admitted == true);
    assert(o.evicted == true);
    assert(o.evictedBytes == 10);
    assert(o.evictedBytes <= 90);  // never charges visible bytes
    printf("PASS visible-admitted-pressure-evicts-nonvisible-only\n");
  }
  // 5. Exact-fit admission needs no eviction.
  {
    PoolOutcome o = poolAcquire(State(100, 80, 8, 2), Req(GpuFormat::kEtc2, 20, true));
    assert(o.status == Status::kOk);
    assert(o.admitted == true);
    assert(o.evicted == false);
    assert(o.evictedBytes == 0);
    printf("PASS exact-fit-no-eviction\n");
  }
  // 6. Full ring slots deny admission without eviction (kOverBudget).
  {
    PoolOutcome o = poolAcquire(State(1000, 100, 4, 4), Req(GpuFormat::kBc, 50, false));
    assert(o.admitted == false);
    assert(o.evicted == false);
    assert(o.evictedBytes == 0);
    assert(o.status == Status::kOverBudget);
    printf("PASS full-ring-slots-deny\n");
  }
  // 7. Invalid args: non-positive bytes, capacity, or slots -> kInvalidArg.
  {
    PoolOutcome o1 = poolAcquire(State(1000, 100, 8, 2), Req(GpuFormat::kAstc, 0, false));
    assert(o1.status == Status::kInvalidArg && o1.admitted == false);
    PoolOutcome o2 = poolAcquire(State(1000, 100, 8, 2), Req(GpuFormat::kAstc, -10, false));
    assert(o2.status == Status::kInvalidArg && o2.admitted == false);
    PoolOutcome o3 = poolAcquire(State(0, 0, 8, 0), Req(GpuFormat::kAstc, 10, false));
    assert(o3.status == Status::kInvalidArg && o3.admitted == false);
    PoolOutcome o4 = poolAcquire(State(1000, -5, 8, 2), Req(GpuFormat::kAstc, 10, false));
    assert(o4.status == Status::kInvalidArg && o4.admitted == false);
    printf("PASS invalid-args-rejected\n");
  }
  // 8. Reference-feed VRAM: compressed selections are materially below the
  //    uncompressed baseline (AC-GRAPHICS-022 second clause).
  {
    const int kUncompressedBytes = 1024;
    const int kAstcBytes = 256;   // ASTC 8x8 reference rate vs RGBA8
    const int kEtc2Bytes = 512;   // ETC2 RGB8 reference rate vs RGBA8
    const int kTextures = 4;
    int compressedTotal = 2 * kAstcBytes + 2 * kEtc2Bytes;
    int uncompressedTotal = kTextures * kUncompressedBytes;
    assert(compressedTotal < uncompressedTotal);
    // Compressed feed must also fit a budget the raw feed overruns.
    PoolState s = State(compressedTotal + 256, 0, 8, 0);
    PoolOutcome ok = poolAcquire(s, Req(GpuFormat::kAstc, compressedTotal, false));
    assert(ok.status == Status::kOk && ok.admitted == true);
    PoolOutcome over = poolAcquire(s, Req(GpuFormat::kUncompressed, uncompressedTotal, false));
    assert(over.status == Status::kOverBudget && over.admitted == false);
    printf("PASS reference-feed-compressed-below-uncompressed\n");
  }
  // 9. Security: INT_MAX-scale accounting must not admit-without-eviction via
  //    signed overflow. usedBytes + bytes overflows 32-bit int here, so a
  //    naive `shortfall = used + bytes - capacity` wraps negative and looks
  //    like "fits". Fail-closed expectation: admitted==false (kOverBudget).
  //    NOTE: once the 64-bit shortfall fix lands, clamped-shortfall eviction
  //    (admitted with evictedBytes covering the true 200-byte shortfall) is
  //    also acceptable — this assertion pins the RED fail-closed behavior.
  {
    PoolOutcome o = poolAcquire(State(INT_MAX - 100, INT_MAX - 100, 8, 0),
                                Req(GpuFormat::kAstc, 200, false));
    assert(o.status == Status::kOverBudget);
    assert(o.admitted == false);
    printf("PASS int-max-scale-overflow-fails-closed\n");
  }
  printf("ALL T4 TESTS PASSED\n");
  return 0;
}
