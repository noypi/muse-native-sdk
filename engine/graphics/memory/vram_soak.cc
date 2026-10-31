// Hermetic compressed-time VRAM soak guard. See vram_soak.h for the contract.
#include "engine/graphics/memory/vram_soak.h"

namespace vram_soak {

SoakVerdict runSoak(const SoakScript &script, vram_budget::VramPool &pool) {
  SoakVerdict verdict;
  verdict.stepsRun = 0;
  verdict.peakBytes = 0;
  verdict.boundBytes = pool.boundBytes();
  verdict.overBound = false;
  verdict.passed = true;

  for (const SoakStep &step : script.steps) {
    if (step.bytes > 0) {
      const vram_budget::Status status =
          pool.allocate(step.id, step.bytes, step.visible);
      if (status == vram_budget::Status::kBudgetExceeded) {
        verdict.overBound = true;
      }
    }
    if (step.trimAfter) {
      vram_budget::applyTrim(step.trimLevel, pool);
    }
    if (pool.usedBytes() > verdict.peakBytes) {
      verdict.peakBytes = pool.usedBytes();
    }
    ++verdict.stepsRun;
  }

  verdict.passed = !verdict.overBound;
  return verdict;
}

}  // namespace vram_soak
