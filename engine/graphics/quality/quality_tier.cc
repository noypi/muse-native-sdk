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
