// VRAM trim-tier mapper (NFR-GRAPHICS-004, AC-GRAPHICS-008 scripted-trim half).
// Pure accounting model, no driver calls: flushes are charged to non-visible
// bytes only, so visible textures are never evicted.
#ifndef ENGINE_GRAPHICS_MEMORY_VRAM_TRIM_H_
#define ENGINE_GRAPHICS_MEMORY_VRAM_TRIM_H_

#include <cstddef>

#include "engine/graphics/graphics_status.h"

enum class TrimLevel { kRunningLow, kRunningCritical, kBackground };

struct VramState {
  int capBytes = 0;
  int usedBytes = 0;
  int visibleBytes = 0;
  bool prefetchEnabled = false;
};

struct TrimOutcome {
  int flushedBytes = 0;
  bool prefetchEnabled = false;
  Status status = Status::kOk;
};

// Applies one trim tier to the current VRAM usage.
// kRunningLow flushes 25% of non-visible + disables prefetch;
// kRunningCritical flushes 50% of non-visible; kBackground flushes all
// non-visible and retains visible. Fractional flushes use integer truncation
// (flushed = nonVisible * pct / 100). Invalid state (usedBytes < 0,
// visibleBytes < 0, visibleBytes > usedBytes) -> kInvalidArg, no mutation.
TrimOutcome applyTrim(VramState& state, TrimLevel level) noexcept;

// Profile bound: true when usedBytes fits the RAM-class budget.
// Below 4GiB -> 48MiB; at/above 6GiB -> 128MiB; between, the bound
// interpolates linearly (monotone non-decreasing) from 48MiB at 4GiB to
// 128MiB at 6GiB. Negative usage never fits.
bool withinProfileBound(int usedBytes, size_t ramBytes) noexcept;

#endif  // ENGINE_GRAPHICS_MEMORY_VRAM_TRIM_H_
