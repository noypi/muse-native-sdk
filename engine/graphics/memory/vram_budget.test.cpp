// T4 hermetic unit test — adaptive VRAM budget + onTrimMemory flush tiers
// (NFR-GRAPHICS-004, AC-GRAPHICS-008, TC-008).
// Style: plain asserts, no dependencies, no I/O, no network, compressed time
// (the 10-minute wall-clock soak is T5, not this file).
// The pool is an in-process double injected through the public interface;
// the profile bound comes from the injected RAM class (mocked
// DeviceFingerprint.ram_gb / Memory Budget Manager bound query).
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "engine/graphics/memory/vram_budget.h"

namespace {

constexpr std::size_t kMB = 1024u * 1024u;

// Seed a pool at ~80% of its bound: half the bytes visible, half evictable.
void SeedPool(vram_budget::VramPool& pool) {
  std::size_t target = (pool.boundBytes() * 8u) / 10u;
  std::size_t half = target / 2u;
  assert(pool.allocate(1u, half, /*visible=*/true) ==
         vram_budget::Status::kOk);
  assert(pool.allocate(2u, target - half, /*visible=*/false) ==
         vram_budget::Status::kOk);
}

std::size_t VisibleBefore(const vram_budget::VramPool& pool) {
  return pool.visibleCount();
}

}  // namespace

int main() {
  using vram_budget::Status;
  using vram_budget::TrimLevel;
  // 1. RUNNING_CRITICAL frees 50% immediately, keeps every visible texture.
  {
    vram_budget::VramPool pool(64u * kMB);
    SeedPool(pool);
    std::size_t usedBefore = pool.usedBytes();
    std::size_t visibleBefore = VisibleBefore(pool);
    vram_budget::FlushResult r =
        vram_budget::applyTrim(TrimLevel::kRunningCritical, pool);
    assert(pool.usedBytes() <= usedBefore / 2u + kMB);
    assert(r.retainedVisibleCount == visibleBefore);
    assert(pool.visibleCount() == visibleBefore);
    printf("PASS critical-flushes-50pct-retains-visible\n");
  }
  // 2. RUNNING_LOW frees 25% and disables prefetch, keeps visible set.
  {
    vram_budget::VramPool pool(64u * kMB);
    SeedPool(pool);
    std::size_t usedBefore = pool.usedBytes();
    std::size_t visibleBefore = VisibleBefore(pool);
    assert(pool.prefetchEnabled());
    vram_budget::FlushResult r =
        vram_budget::applyTrim(TrimLevel::kRunningLow, pool);
    assert(pool.usedBytes() <= (usedBefore * 3u) / 4u + kMB);
    assert(!pool.prefetchEnabled());
    assert(r.retainedVisibleCount == visibleBefore);
    assert(pool.visibleCount() == visibleBefore);
    printf("PASS low-flushes-25pct-disables-prefetch\n");
  }
  // 3. BACKGROUND flushes everything except visible textures.
  {
    vram_budget::VramPool pool(64u * kMB);
    SeedPool(pool);
    std::size_t visibleBefore = VisibleBefore(pool);
    vram_budget::FlushResult r =
        vram_budget::applyTrim(TrimLevel::kBackground, pool);
    assert(pool.usedBytes() == pool.visibleBytes());
    assert(r.retainedVisibleCount == visibleBefore);
    assert(pool.visibleCount() == visibleBefore);
    printf("PASS background-retains-only-visible\n");
  }
  // 4. Over-profile-bound allocation fails closed: kBudgetExceeded, pool unchanged.
  {
    vram_budget::VramPool pool(64u * kMB);
    SeedPool(pool);
    std::size_t usedBefore = pool.usedBytes();
    std::size_t visibleBefore = VisibleBefore(pool);
    Status s = pool.allocate(99u, pool.boundBytes() + 1u, /*visible=*/false);
    assert(s == Status::kBudgetExceeded);
    assert(pool.usedBytes() == usedBefore);
    assert(pool.visibleCount() == visibleBefore);
    printf("PASS over-bound-allocation-fail-closed\n");
  }
  // 5. Per-RAM-class bounds: low-end profile is 32-48MB.
  {
    for (std::size_t bound : {32u * kMB, 48u * kMB}) {
      vram_budget::VramPool pool(bound);
      SeedPool(pool);
      std::size_t visibleBefore = VisibleBefore(pool);
      vram_budget::applyTrim(TrimLevel::kRunningCritical, pool);
      assert(pool.usedBytes() <= bound);
      assert(pool.visibleCount() == visibleBefore);
    }
    printf("PASS low-end-bounds-32-48MB\n");
  }
  // 6. Per-RAM-class bounds: mid profile is 64-80MB.
  {
    for (std::size_t bound : {64u * kMB, 80u * kMB}) {
      vram_budget::VramPool pool(bound);
      SeedPool(pool);
      std::size_t visibleBefore = VisibleBefore(pool);
      vram_budget::applyTrim(TrimLevel::kRunningLow, pool);
      assert(pool.usedBytes() <= bound);
      assert(pool.visibleCount() == visibleBefore);
    }
    printf("PASS mid-bounds-64-80MB\n");
  }
  // 7. Per-RAM-class bounds: high profile is 128MB+.
  {
    for (std::size_t bound : {128u * kMB, 256u * kMB}) {
      vram_budget::VramPool pool(bound);
      SeedPool(pool);
      std::size_t visibleBefore = VisibleBefore(pool);
      vram_budget::applyTrim(TrimLevel::kBackground, pool);
      assert(pool.usedBytes() <= bound);
      assert(pool.visibleCount() == visibleBefore);
    }
    printf("PASS high-bounds-128MB-plus\n");
  }
  // 8. Visible-texture retention invariant holds across every tier.
  {
    for (TrimLevel level :
         {TrimLevel::kRunningCritical, TrimLevel::kRunningLow,
          TrimLevel::kBackground}) {
      vram_budget::VramPool pool(80u * kMB);
      SeedPool(pool);
      std::size_t visibleBefore = VisibleBefore(pool);
      vram_budget::FlushResult r = vram_budget::applyTrim(level, pool);
      assert(r.retainedVisibleCount == visibleBefore);
      assert(pool.visibleCount() == visibleBefore);
    }
    printf("PASS visible-retention-invariant-all-tiers\n");
  }
  printf("ALL VRAM BUDGET TESTS PASSED\n");
  return 0;
}
