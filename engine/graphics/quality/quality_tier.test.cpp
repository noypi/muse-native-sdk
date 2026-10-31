// T4 hermetic unit test — quality tiers: seamless transition + telemetry +
// app override (NFR-GRAPHICS-005, AC-GRAPHICS-026/027).
// Style: plain asserts, no dependencies, no I/O, no network.
// Expectations derive ONLY from AC-026/027 and the plan interface:
// transitions change only the TierEffectSet (no relayout — geometry/layout is
// never part of the effect set, and a repeated request is idempotent);
// every selection emits a telemetry record {tier, source, frameTimeOk};
// app-override source wins over thermal/capability; unknown tier -> false,
// tier unchanged. Thermal step-down (AC-028) is NOT in this batch.
// ASSUMPTION (telemetry observation point): the plan Interface names the
// emitted record but no accessor; tests observe it via lastTierTelemetry(),
// which the coder provides without changing the specified behavior.
#include <cassert>
#include <cstdio>

#include "engine/graphics/quality/quality_tier.h"

int main() {
  // 1. Effect sets differ across tiers: stepping up changes effect knobs only.
  {
    TierEffectSet low = effectSetFor(QualityTier::kLow);
    TierEffectSet ultra = effectSetFor(QualityTier::kUltra);
    assert(low.blurPasses != ultra.blurPasses ||
           low.shadowQuality != ultra.shadowQuality ||
           low.shadingRate != ultra.shadingRate ||
           low.msaaSamples != ultra.msaaSamples);
    printf("PASS effect-sets-differ-across-tiers\n");
  }
  // 2. Tier order is monotone: higher tiers never select weaker effects.
  {
    TierEffectSet low = effectSetFor(QualityTier::kLow);
    TierEffectSet medium = effectSetFor(QualityTier::kMedium);
    TierEffectSet high = effectSetFor(QualityTier::kHigh);
    TierEffectSet ultra = effectSetFor(QualityTier::kUltra);
    assert(low.blurPasses <= medium.blurPasses);
    assert(medium.blurPasses <= high.blurPasses);
    assert(high.blurPasses <= ultra.blurPasses);
    assert(low.shadowQuality <= medium.shadowQuality);
    assert(medium.shadowQuality <= high.shadowQuality);
    assert(high.shadowQuality <= ultra.shadowQuality);
    printf("PASS tier-order-monotone\n");
  }
  // 3. Seamless transition: requestTier changes only the effect set — the
  //    post-transition set equals effectSetFor(new tier), and repeating the
  //    identical request is idempotent (no churn, no relayout trigger).
  {
    assert(requestTier(QualityTier::kHigh, TierSource::kCapability));
    assert(currentTier() == QualityTier::kHigh);
    TierEffectSet after = effectSetFor(currentTier());
    assert(requestTier(QualityTier::kHigh, TierSource::kCapability));
    TierEffectSet repeated = effectSetFor(currentTier());
    assert(after.blurPasses == repeated.blurPasses);
    assert(after.shadowQuality == repeated.shadowQuality);
    assert(after.shadingRate == repeated.shadingRate);
    assert(after.msaaSamples == repeated.msaaSamples);
    printf("PASS transition-changes-only-effect-set\n");
  }
  // 4. Step down then back up restores the exact effect set (no pop residue).
  {
    assert(requestTier(QualityTier::kUltra, TierSource::kCapability));
    TierEffectSet ultra = effectSetFor(currentTier());
    assert(requestTier(QualityTier::kLow, TierSource::kThermal));
    assert(currentTier() == QualityTier::kLow);
    assert(requestTier(QualityTier::kUltra, TierSource::kCapability));
    TierEffectSet restored = effectSetFor(currentTier());
    assert(restored.blurPasses == ultra.blurPasses);
    assert(restored.shadowQuality == ultra.shadowQuality);
    assert(restored.shadingRate == ultra.shadingRate);
    assert(restored.msaaSamples == ultra.msaaSamples);
    printf("PASS step-down-up-restores-effect-set\n");
  }
  // 5. Every successful selection emits a telemetry record {tier, source}.
  {
    assert(requestTier(QualityTier::kMedium, TierSource::kThermal));
    TierTelemetry t = lastTierTelemetry();
    assert(t.tier == QualityTier::kMedium);
    assert(t.source == TierSource::kThermal);
    printf("PASS selection-emits-telemetry-record\n");
  }
  // 6. Transition telemetry carries no dropped-frame signal (host proxy for
  //    AC-026 "no dropped frame"; on-device proof is T5 evidence, not here).
  {
    assert(requestTier(QualityTier::kHigh, TierSource::kCapability));
    TierTelemetry t = lastTierTelemetry();
    assert(t.tier == QualityTier::kHigh);
    assert(t.frameTimeOk);
    printf("PASS transition-telemetry-frame-time-ok\n");
  }
  // 7. App override wins over thermal/capability: after an override, thermal
  //    and capability requests are denied and the tier is unchanged; a newer
  //    app override still takes effect.
  {
    assert(requestTier(QualityTier::kLow, TierSource::kThermal));
    assert(requestTier(QualityTier::kUltra, TierSource::kAppOverride));
    assert(currentTier() == QualityTier::kUltra);
    assert(!requestTier(QualityTier::kLow, TierSource::kThermal));
    assert(currentTier() == QualityTier::kUltra);
    assert(!requestTier(QualityTier::kMedium, TierSource::kCapability));
    assert(currentTier() == QualityTier::kUltra);
    TierTelemetry t = lastTierTelemetry();
    assert(t.tier == QualityTier::kUltra);
    assert(t.source == TierSource::kAppOverride);
    assert(requestTier(QualityTier::kHigh, TierSource::kAppOverride));
    assert(currentTier() == QualityTier::kHigh);
    printf("PASS app-override-wins-over-thermal\n");
  }
  // 8. Denial: unknown tier -> false, tier unchanged, no telemetry emitted.
  {
    assert(requestTier(QualityTier::kHigh, TierSource::kCapability));
    TierTelemetry before = lastTierTelemetry();
    assert(!requestTier(static_cast<QualityTier>(999), TierSource::kThermal));
    assert(currentTier() == QualityTier::kHigh);
    TierTelemetry after = lastTierTelemetry();
    assert(after.tier == before.tier);
    assert(after.source == before.source);
    printf("PASS unknown-tier-denied-unchanged\n");
  }
  // 9. Hardening (d): unknown tiers denied with no telemetry write — every
  //    out-of-range value x every source returns false, tier unchanged, and
  //    the last telemetry record (tier, source, frameTimeOk) untouched.
  //    Spec: unknown tiers are denied.
  //    NOTE: an earlier case latches the app-override lock, so the known
  //    baseline is (re-)established via kAppOverride (always accepted);
  //    the denial assertions themselves are source-independent.
  {
    assert(requestTier(QualityTier::kMedium, TierSource::kAppOverride));
    const QualityTier kBad[] = {
        static_cast<QualityTier>(999), static_cast<QualityTier>(-1),
        static_cast<QualityTier>(4), static_cast<QualityTier>(-100)};
    const TierSource kSources[] = {TierSource::kCapability, TierSource::kThermal,
                                  TierSource::kAppOverride};
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 3; ++j) {
        TierTelemetry before = lastTierTelemetry();
        assert(!requestTier(kBad[i], kSources[j]));
        assert(currentTier() == QualityTier::kMedium);
        TierTelemetry after = lastTierTelemetry();
        assert(after.tier == before.tier);
        assert(after.source == before.source);
        assert(after.frameTimeOk == before.frameTimeOk);
      }
    }
    printf("PASS unknown-tiers-denied-no-telemetry-write\n");
  }
  // 10. Hardening (e): effectSetFor returns the safe Low set for an
  //     out-of-range tier value. Spec: the Low set is the minimum-effect
  //     fallback, so clamping unknown input there can never over-commit.
  {
    TierEffectSet low = effectSetFor(QualityTier::kLow);
    const QualityTier kBad[] = {static_cast<QualityTier>(999),
                               static_cast<QualityTier>(-1),
                               static_cast<QualityTier>(4)};
    for (int i = 0; i < 3; ++i) {
      TierEffectSet got = effectSetFor(kBad[i]);
      assert(got.blurPasses == low.blurPasses);
      assert(got.shadowQuality == low.shadowQuality);
      assert(got.shadingRate == low.shadingRate);
      assert(got.msaaSamples == low.msaaSamples);
    }
    printf("PASS effect-set-out-of-range-falls-back-to-low\n");
  }
  printf("ALL QUALITY_TIER TESTS PASSED\n");
  return 0;
}
