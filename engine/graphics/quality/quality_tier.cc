#include "engine/graphics/quality/quality_tier.h"

namespace {

bool IsKnownTier(QualityTier tier) noexcept {
  return tier == QualityTier::kLow || tier == QualityTier::kMedium ||
         tier == QualityTier::kHigh || tier == QualityTier::kUltra;
}

QualityTier g_currentTier = QualityTier::kHigh;
bool g_overrideActive = false;
TierTelemetry g_lastTelemetry{QualityTier::kHigh, TierSource::kCapability,
                              true};

}  // namespace

QualityTier currentTier() noexcept { return g_currentTier; }

bool requestTier(QualityTier tier, TierSource source) noexcept {
  if (!IsKnownTier(tier)) return false;
  if (tier == g_currentTier) {
    // Idempotent repeat: no state change, still an accepted selection.
    g_lastTelemetry = TierTelemetry{tier, source, true};
    return true;
  }
  if (g_overrideActive && source != TierSource::kAppOverride) return false;
  if (source == TierSource::kAppOverride) g_overrideActive = true;
  g_currentTier = tier;
  g_lastTelemetry = TierTelemetry{tier, source, true};
  return true;
}

TierEffectSet effectSetFor(QualityTier tier) noexcept {
  switch (tier) {
    case QualityTier::kLow:
      return TierEffectSet{0, 0, 1, 1};
    case QualityTier::kMedium:
      return TierEffectSet{1, 1, 2, 2};
    case QualityTier::kHigh:
      return TierEffectSet{2, 2, 3, 4};
    case QualityTier::kUltra:
      return TierEffectSet{4, 3, 4, 8};
  }
  return TierEffectSet{0, 0, 1, 1};
}

TierTelemetry lastTierTelemetry() noexcept { return g_lastTelemetry; }

namespace quality_tier {

namespace {

bool IsValidTier(QualityTier tier) {
  switch (tier) {
    case QualityTier::kLow:
    case QualityTier::kMedium:
    case QualityTier::kHigh:
    case QualityTier::kUltra:
      return true;
  }
  return false;
}

}  // namespace

TierConfig configFor(QualityTier tier, bool vrsSupported) {
  // T9 fail-closed: invalid ordinals map to the safe LOW row, never zeros.
  if (!IsValidTier(tier)) tier = QualityTier::kLow;
  TierConfig cfg;
  if (vrsSupported) {
    // VRS row: VRS applied only when the device probes capable.
    cfg.vrsApplied = true;
    switch (tier) {
      case QualityTier::kLow:
        cfg.blurPasses = 1;
        cfg.shadowQuality = 1;
        cfg.shadingRate = 1;
        cfg.msaaSamples = 2;
        break;
      case QualityTier::kMedium:
        cfg.blurPasses = 2;
        cfg.shadowQuality = 2;
        cfg.shadingRate = 1;
        cfg.msaaSamples = 4;
        break;
      case QualityTier::kHigh:
        cfg.blurPasses = 3;
        cfg.shadowQuality = 3;
        cfg.shadingRate = 2;
        cfg.msaaSamples = 4;
        break;
      case QualityTier::kUltra:
        cfg.blurPasses = 4;
        cfg.shadowQuality = 4;
        cfg.shadingRate = 2;
        cfg.msaaSamples = 8;
        break;
    }
  } else {
    // No-VRS fallback row: effects degrade first, VRS stays off, and the
    // shading-rate knob uses a fixed-rate path distinct from the VRS row.
    cfg.vrsApplied = false;
    switch (tier) {
      case QualityTier::kLow:
        cfg.blurPasses = 1;
        cfg.shadowQuality = 1;
        cfg.shadingRate = 0;
        cfg.msaaSamples = 2;
        break;
      case QualityTier::kMedium:
        cfg.blurPasses = 2;
        cfg.shadowQuality = 2;
        cfg.shadingRate = 0;
        cfg.msaaSamples = 4;
        break;
      case QualityTier::kHigh:
        cfg.blurPasses = 2;
        cfg.shadowQuality = 2;
        cfg.shadingRate = 0;
        cfg.msaaSamples = 4;
        break;
      case QualityTier::kUltra:
        cfg.blurPasses = 3;
        cfg.shadowQuality = 3;
        cfg.shadingRate = 0;
        cfg.msaaSamples = 8;
        break;
    }
  }
  return cfg;
}

TransitionResult setTier(QualityTier next, FrameContext& ctx) {
  if (!IsValidTier(next)) {
    // Fail closed: leave every field of ctx untouched.
    return TransitionResult{Status::kInvalidTier, 0, 0, false};
  }
  // Effect-set swap only: re-resolve the tier table against the probed VRS
  // flag recorded in ctx. Geometry, layout, caps, memory, and thermal inputs
  // are plain data and are never modified here.
  ctx.currentTier = next;
  ctx.applied = configFor(next, ctx.vrsSupported);
  return TransitionResult{Status::kOk, 0, 0, false};
}

}  // namespace quality_tier
