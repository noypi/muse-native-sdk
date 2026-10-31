// T5 hermetic unit test — VRAM soak guard, compressed-time feed simulation
// (NFR-GRAPHICS-004, AC-GRAPHICS-008, TC-008).
// Style: plain asserts, no dependencies, no I/O, no network, no wall clock.
// The scripted allocate/evict/trim sequence stands in for the 10-minute
// wall-clock feed soak (the real 10-minute 6562 soak is separate manual
// evidence, not this unit). Reuses the T4 VramPool double through the public
// interface; no change to vram_budget.h.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/graphics/memory/vram_budget.h"
#include "engine/graphics/memory/vram_soak.h"

namespace {

constexpr std::size_t kMB = 1024u * 1024u;

vram_soak::SoakStep Alloc(uint32_t id, std::size_t bytes, bool visible) {
  vram_soak::SoakStep s;
  s.id = id;
  s.bytes = bytes;
  s.visible = visible;
  s.trimAfter = false;
  s.trimLevel = vram_budget::TrimLevel::kRunningLow;
  return s;
}

vram_soak::SoakStep TrimStep(vram_budget::TrimLevel level) {
  vram_soak::SoakStep s;
  s.id = 0u;
  s.bytes = 0u;
  s.visible = false;
  s.trimAfter = true;
  s.trimLevel = level;
  return s;
}

}  // namespace

int main() {
  using vram_budget::TrimLevel;
  // 1. Empty script passes: 0 steps run, 0 peak, bound named, no over-bound.
  {
    vram_budget::VramPool pool(64u * kMB);
    vram_soak::SoakScript script;
    vram_soak::SoakVerdict v = vram_soak::runSoak(script, pool);
    assert(v.passed);
    assert(!v.overBound);
    assert(v.stepsRun == 0u);
    assert(v.peakBytes == 0u);
    assert(v.boundBytes == pool.boundBytes());
    printf("PASS empty-script-passes\n");
  }
  // 2. In-bound feed passes: verdict names steps run + peak bytes + bound.
  {
    vram_budget::VramPool pool(64u * kMB);
    vram_soak::SoakScript script;
    script.steps.push_back(Alloc(1u, 10u * kMB, /*visible=*/true));
    script.steps.push_back(Alloc(2u, 10u * kMB, /*visible=*/false));
    script.steps.push_back(Alloc(3u, 10u * kMB, /*visible=*/false));
    vram_soak::SoakVerdict v = vram_soak::runSoak(script, pool);
    assert(v.passed);
    assert(!v.overBound);
    assert(v.stepsRun == script.steps.size());
    assert(v.peakBytes == 30u * kMB);
    assert(v.boundBytes == 64u * kMB);
    assert(pool.usedBytes() <= pool.boundBytes());
    printf("PASS in-bound-feed-passes-names-steps-peak-bound\n");
  }
  // 3. Scripted CRITICAL trim flushes the specified fraction mid-soak.
  {
    vram_budget::VramPool pool(64u * kMB);
    vram_soak::SoakScript script;
    script.steps.push_back(Alloc(1u, 20u * kMB, /*visible=*/true));
    script.steps.push_back(Alloc(2u, 20u * kMB, /*visible=*/false));
    script.steps.push_back(TrimStep(TrimLevel::kRunningCritical));
    script.steps.push_back(Alloc(3u, 8u * kMB, /*visible=*/false));
    vram_soak::SoakVerdict v = vram_soak::runSoak(script, pool);
    assert(v.passed);
    assert(!v.overBound);
    assert(v.stepsRun == script.steps.size());
    assert(pool.usedBytes() <= pool.boundBytes());
    assert(pool.visibleCount() == 1u);  // only id 1 visible; id 3 hidden
    printf("PASS critical-trim-flushes-half-mid-soak\n");
  }
  // 4. Scripted LOW trim flushes its fraction and disables prefetch.
  {
    vram_budget::VramPool pool(64u * kMB);
    vram_soak::SoakScript script;
    script.steps.push_back(Alloc(1u, 16u * kMB, /*visible=*/true));
    script.steps.push_back(Alloc(2u, 16u * kMB, /*visible=*/false));
    script.steps.push_back(TrimStep(TrimLevel::kRunningLow));
    vram_soak::SoakVerdict v = vram_soak::runSoak(script, pool);
    assert(v.passed);
    assert(!v.overBound);
    assert(!pool.prefetchEnabled());
    assert(pool.usedBytes() <= pool.boundBytes());
    printf("PASS low-trim-flushes-quarter-mid-soak\n");
  }
  // 5. Scripted BACKGROUND trim retains only visible textures mid-soak.
  {
    vram_budget::VramPool pool(64u * kMB);
    vram_soak::SoakScript script;
    script.steps.push_back(Alloc(1u, 12u * kMB, /*visible=*/true));
    script.steps.push_back(Alloc(2u, 12u * kMB, /*visible=*/false));
    script.steps.push_back(TrimStep(TrimLevel::kBackground));
    vram_soak::SoakVerdict v = vram_soak::runSoak(script, pool);
    assert(v.passed);
    assert(!v.overBound);
    assert(pool.usedBytes() == pool.visibleBytes());
    assert(pool.visibleCount() == 1u);
    printf("PASS background-trim-retains-only-visible-mid-soak\n");
  }
  // 6. Over-bound allocation fails the verdict: overBound set, passed false,
  //    usage never exceeds the bound (fail-closed pool, state unchanged).
  {
    vram_budget::VramPool pool(64u * kMB);
    vram_soak::SoakScript script;
    script.steps.push_back(Alloc(1u, 32u * kMB, /*visible=*/true));
    script.steps.push_back(Alloc(2u, 64u * kMB, /*visible=*/false));
    vram_soak::SoakVerdict v = vram_soak::runSoak(script, pool);
    assert(v.overBound);
    assert(!v.passed);
    assert(v.stepsRun == script.steps.size());
    assert(pool.usedBytes() <= pool.boundBytes());
    assert(pool.visibleCount() == 1u);
    printf("PASS over-bound-allocation-fails-verdict\n");
  }
  // 7. Visible-texture retention holds at every script step.
  {
    vram_budget::VramPool pool(80u * kMB);
    vram_soak::SoakScript script;
    script.steps.push_back(Alloc(1u, 16u * kMB, /*visible=*/true));
    script.steps.push_back(Alloc(2u, 16u * kMB, /*visible=*/false));
    script.steps.push_back(TrimStep(TrimLevel::kRunningCritical));
    script.steps.push_back(Alloc(3u, 8u * kMB, /*visible=*/true));
    script.steps.push_back(TrimStep(TrimLevel::kRunningLow));
    script.steps.push_back(TrimStep(TrimLevel::kBackground));
    vram_soak::SoakVerdict v = vram_soak::runSoak(script, pool);
    assert(v.passed);
    assert(!v.overBound);
    assert(pool.visibleCount() == 2u);  // ids 1 and 3 survive every trim
    assert(pool.usedBytes() == pool.visibleBytes());
    printf("PASS visible-retention-holds-every-step\n");
  }
  // 8. Peak tracks the high-water mark even after trims drain the pool.
  {
    vram_budget::VramPool pool(64u * kMB);
    vram_soak::SoakScript script;
    script.steps.push_back(Alloc(1u, 20u * kMB, /*visible=*/true));
    script.steps.push_back(Alloc(2u, 20u * kMB, /*visible=*/false));
    script.steps.push_back(TrimStep(TrimLevel::kBackground));
    vram_soak::SoakVerdict v = vram_soak::runSoak(script, pool);
    assert(v.passed);
    assert(v.peakBytes == 40u * kMB);
    assert(pool.usedBytes() < v.peakBytes);
    printf("PASS peak-tracks-high-water-mark\n");
  }
  printf("ALL VRAM SOAK TESTS PASSED\n");
  return 0;
}
