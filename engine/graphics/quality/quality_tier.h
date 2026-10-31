// Quality tiers: seamless effect-set-only transitions + telemetry +
// app override (NFR-GRAPHICS-005, AC-GRAPHICS-026/027).
// Transitions change only the TierEffectSet (no relayout signal exists);
// every accepted selection emits a TierTelemetry record observed via
// lastTierTelemetry(). Post-override thermal/capability requests for a
// different tier are denied with no telemetry; unknown tiers are denied.
// The namespaced quality_tier::setTier/configFor contract (T6, AC-026)
// coexists in this header: effect-set swap only via FrameContext, seamless
// contract relayoutCount==0, droppedFrames==0, flickerFlag==false, invalid
// tier -> Status::kInvalidTier, no-VRS fallback row, invalid ordinals clamp
// to the safe LOW row (T9 fail-closed).
#ifndef ENGINE_GRAPHICS_QUALITY_QUALITY_TIER_H_
#define ENGINE_GRAPHICS_QUALITY_QUALITY_TIER_H_

#include <cstddef>
#include <initializer_list>

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

namespace quality_tier {

enum class QualityTier { kLow, kMedium, kHigh, kUltra };

enum class Status { kOk, kInvalidTier };

struct TierConfig {
  int blurPasses = 0;
  int shadowQuality = 0;
  int shadingRate = 0;
  int msaaSamples = 0;
  bool vrsApplied = false;
};

struct FrameContext {
  QualityTier currentTier = QualityTier::kLow;
  int geometryVersion = 0;
  bool vrsSupported = false;
  int capsTierOrdinal = 0;
  std::size_t memoryBoundBytes = 0;
  float thermalHeadroom = 0.0f;
  TierConfig applied;
};

struct TransitionResult {
  Status status = Status::kOk;
  int relayoutCount = 0;
  int droppedFrames = 0;
  bool flickerFlag = false;
};

TierConfig configFor(QualityTier tier, bool vrsSupported);

TransitionResult setTier(QualityTier next, FrameContext& ctx);

}  // namespace quality_tier

#endif  // ENGINE_GRAPHICS_QUALITY_QUALITY_TIER_H_
