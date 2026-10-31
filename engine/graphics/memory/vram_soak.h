#ifndef ENGINE_GRAPHICS_MEMORY_VRAM_SOAK_H_
#define ENGINE_GRAPHICS_MEMORY_VRAM_SOAK_H_

// Hermetic compressed-time VRAM soak guard (NFR-GRAPHICS-004,
// AC-GRAPHICS-008, TC-GRAPHICS-008). Replays a scripted allocate/trim
// sequence over the T4 VramPool double (vram_budget.h, unchanged).
// Stdlib only, no wall clock, no I/O.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/graphics/memory/vram_budget.h"

namespace vram_soak {

struct SoakStep {
  uint32_t id;
  std::size_t bytes;
  bool visible;
  bool trimAfter;
  vram_budget::TrimLevel trimLevel;
};

struct SoakScript {
  std::vector<SoakStep> steps;
};

struct SoakVerdict {
  std::size_t stepsRun;
  std::size_t peakBytes;
  std::size_t boundBytes;
  bool overBound;
  bool passed;
};

// Replays script steps in order against pool. Steps with bytes > 0 allocate
// through VramPool::allocate (fail-closed: a rejected over-bound alloc sets
// overBound and leaves the pool unchanged). Steps with trimAfter set call
// applyTrim with the step's trimLevel (after the alloc, if any). peakBytes
// is the high-water usedBytes; passed == !overBound.
SoakVerdict runSoak(const SoakScript &script, vram_budget::VramPool &pool);

}  // namespace vram_soak

#endif  // ENGINE_GRAPHICS_MEMORY_VRAM_SOAK_H_
