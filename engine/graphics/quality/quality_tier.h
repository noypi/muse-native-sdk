// Quality tiers: seamless effect-set-only transitions + telemetry +
// app override (NFR-GRAPHICS-005, AC-GRAPHICS-026/027).
// Transitions change only the TierEffectSet (no relayout signal exists);
// every accepted selection emits a TierTelemetry record observed via
// lastTierTelemetry(). Post-override thermal/capability requests for a
// different tier are denied with no telemetry; unknown tiers are denied.
#ifndef ENGINE_GRAPHICS_QUALITY_QUALITY_TIER_H_
#define ENGINE_GRAPHICS_QUALITY_QUALITY_TIER_H_

enum class QualityTier {
  kLow,
  kMedium,
  kHigh,
  kUltra,
};

enum class TierSource {
  kCapability,
  kThermal,
  kAppOverride,
};

// Effect knobs only — no geometry/layout members, so a tier step can never
// trigger relayout.
struct TierEffectSet {
  int blurPasses;
  int shadowQuality;
  int shadingRate;
  int msaaSamples;
};

struct TierTelemetry {
  QualityTier tier;
  TierSource source;
  bool frameTimeOk;
};

QualityTier currentTier() noexcept;
bool requestTier(QualityTier tier, TierSource source) noexcept;
TierEffectSet effectSetFor(QualityTier tier) noexcept;
TierTelemetry lastTierTelemetry() noexcept;

#endif  // ENGINE_GRAPHICS_QUALITY_QUALITY_TIER_H_
