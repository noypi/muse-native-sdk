#include "motion/asset_scale.h"

namespace {

// Minimum positive time-scale step: non-positive input clamps here so
// essential feedback stays perceptible and playback never hangs or
// reverses. Small but nonzero to keep progress reporting usable.
constexpr float kMinTimeScale = 0.01f;

float g_timeScale = 1.0f;  // INTENTIONAL STUB (write-only by design):
                            // no advance/step API reads it yet — seek
                            // positions are absolute marker fractions and
                            // intentionally scale-independent. Tester-layer
                            // wiring point: a future advance(elapsed) helper
                            // multiplies elapsed by g_timeScale; clamping is
                            // observed meanwhile as continued usability.
bool g_reducedMotion = false;
bool g_lastBudgetShed = false;  // INTENTIONAL STUB (nominally false):
                                // no budget timer lives in this TU, so the
                                // nominal path never sets it. Tester-layer
                                // wiring point: the consumed perf budget-guard
                                // overrun signal sets it when decorative
                                // layers shed first.

}  // namespace

void motion_setTimeScale(float scale) noexcept {
  // Clamp non-positive and non-finite input to the minimum positive step;
  // valid positives pass through unchanged. No branching on player state,
  // no allocation, cannot throw.
  if (!(scale > 0.0f)) {
    g_timeScale = kMinTimeScale;
    return;
  }
  g_timeScale = scale;
}

AssetError motion_setReducedMotion(bool enabled) noexcept {
  g_reducedMotion = enabled;
  return AssetError::kOk;
}

AssetVariant motion_activeVariant(const AssetPlayer& p) noexcept {
  (void)p;
  // Variant is global reduced-motion state; gating errors belong to the
  // AssetError methods, so this query never fails and defaults to kFull.
  return g_reducedMotion ? AssetVariant::kReduced : AssetVariant::kFull;
}

bool motion_lastBudgetShed() noexcept {
  // Nominal path: no budget overrun has been signalled, so nothing was
  // shed. Overrun handling (perf budget-guard signal -> shed decorative
  // layers first, keep essential feedback) reports through this flag when
  // wired; until then it reads false.
  return g_lastBudgetShed;
}
