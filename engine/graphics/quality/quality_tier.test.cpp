// Merged hermetic unit test — quality tiers (NFR-GRAPHICS-005,
// AC-GRAPHICS-026/027, TC-GRAPHICS-026/027).
// Style: plain asserts, no dependencies, no I/O, no network.
//
// Block A (main side, T4): global API — seamless transition + telemetry +
// app override. Expectations derive ONLY from AC-026/027 and the plan
// interface: transitions change only the TierEffectSet (no relayout —
// geometry/layout is never part of the effect set, and a repeated request is
// idempotent); every selection emits a telemetry record {tier, source,
// frameTimeOk}; app-override source wins over thermal/capability; unknown
// tier -> false, tier unchanged. Thermal step-down (AC-028) is NOT in this
// batch.
// ASSUMPTION (telemetry observation point): the plan Interface names the
// emitted record but no accessor; tests observe it via lastTierTelemetry(),
// which the coder provides without changing the specified behavior.
//
// Block B (2e1a157 side, T6): quality_tier:: namespaced contract — seamless
// effect-set-only transition ({relayoutCount,droppedFrames,flickerFlag} =
// {0,0,false}), no-VRS fallback row, invalid -> Status::kInvalidTier,
// configFor-invalid-ordinal -> safe LOW row (T9 fail-closed, pinned).
// Tier selection inputs (CapsTier shape, memory bound, thermal headroom
// float) arrive as plain data inside FrameContext (mocked, not implemented);
// the tier table carries a no-VRS fallback row per Q-GRAPHICS-005 A5.
//
// The two blocks share one process but not state: the global API drives
// process-wide tier state while the namespaced contract touches only its
// FrameContext, so either block may run first.
#include <cassert>
#include <cstddef>
#include <cstdio>

#include "engine/graphics/quality/quality_tier.h"

namespace {

quality_tier::FrameContext MakeCtx(quality_tier::QualityTier current,
                                   bool vrsSupported) {
  quality_tier::FrameContext ctx;
  ctx.currentTier = current;
  ctx.geometryVersion = 7;
  ctx.vrsSupported = vrsSupported;
  ctx.capsTierOrdinal = 1;  // mocked CapsTier::MID shape
  ctx.memoryBoundBytes = 64u * 1024u * 1024u;
  ctx.thermalHeadroom = 0.5f;
  ctx.applied = quality_tier::configFor(current, vrsSupported);
  return ctx;
}

void AssertSeamless(const quality_tier::TransitionResult& r) {
  assert(r.status == quality_tier::Status::kOk);
  assert(r.relayoutCount == 0);
  assert(r.droppedFrames == 0);
  assert(!r.flickerFlag);
}

}  // namespace

static void RunGlobalApiTests() {
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
    const TierSource kSources[] = {TierSource::kCapability,
                                   TierSource::kThermal,
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
  printf("ALL QUALITY_TIER GLOBAL-API TESTS PASSED\n");
}

static void RunNamespacedContractTests() {
  using quality_tier::QualityTier;
  using quality_tier::Status;
  // 1. High -> low transition is seamless: 0 relayouts, 0 dropped, no flicker.
  {
    quality_tier::FrameContext ctx = MakeCtx(QualityTier::kHigh, true);
    quality_tier::TransitionResult r =
        quality_tier::setTier(QualityTier::kLow, ctx);
    AssertSeamless(r);
    assert(ctx.currentTier == QualityTier::kLow);
    printf("PASS high-to-low-seamless\n");
  }
  // 2. Low -> high transition is seamless.
  {
    quality_tier::FrameContext ctx = MakeCtx(QualityTier::kLow, true);
    quality_tier::TransitionResult r =
        quality_tier::setTier(QualityTier::kHigh, ctx);
    AssertSeamless(r);
    assert(ctx.currentTier == QualityTier::kHigh);
    printf("PASS low-to-high-seamless\n");
  }
  // 3. Every ordered tier pair transitions seamlessly.
  {
    const QualityTier tiers[] = {QualityTier::kLow, QualityTier::kMedium,
                                 QualityTier::kHigh, QualityTier::kUltra};
    for (QualityTier from : tiers) {
      for (QualityTier to : tiers) {
        quality_tier::FrameContext ctx = MakeCtx(from, true);
        quality_tier::TransitionResult r = quality_tier::setTier(to, ctx);
        AssertSeamless(r);
        assert(ctx.currentTier == to);
      }
    }
    printf("PASS all-ordered-pairs-seamless\n");
  }
  // 4. Invalid tier is rejected fail-closed: kInvalidTier, tier unchanged.
  {
    quality_tier::FrameContext ctx = MakeCtx(QualityTier::kMedium, true);
    quality_tier::TransitionResult r = quality_tier::setTier(
        static_cast<QualityTier>(99), ctx);
    assert(r.status == Status::kInvalidTier);
    assert(ctx.currentTier == QualityTier::kMedium);
    assert(r.relayoutCount == 0);
    assert(r.droppedFrames == 0);
    assert(!r.flickerFlag);
    printf("PASS invalid-tier-rejected-fail-closed\n");
  }
  // 5. No-VRS device gets the fallback row: effects degrade, VRS off,
  //    geometry/layout untouched.
  {
    quality_tier::FrameContext ctx = MakeCtx(QualityTier::kLow, false);
    quality_tier::TransitionResult r =
        quality_tier::setTier(QualityTier::kHigh, ctx);
    AssertSeamless(r);
    quality_tier::TierConfig applied = ctx.applied;
    assert(!applied.vrsApplied);
    quality_tier::TierConfig vrsRow =
        quality_tier::configFor(QualityTier::kHigh, true);
    assert(applied.blurPasses <= vrsRow.blurPasses);
    assert(applied.shadowQuality <= vrsRow.shadowQuality);
    assert(ctx.geometryVersion == 7);
    printf("PASS no-vrs-device-fallback-row\n");
  }
  // 6. VRS row is applied only when the device probes VRS-capable.
  {
    quality_tier::TierConfig on =
        quality_tier::configFor(QualityTier::kUltra, true);
    quality_tier::TierConfig off =
        quality_tier::configFor(QualityTier::kUltra, false);
    assert(on.vrsApplied);
    assert(!off.vrsApplied);
    assert(on.shadingRate != off.shadingRate);
    printf("PASS vrs-row-only-when-probed\n");
  }
  // 7. Geometry/layout version is untouched by every transition.
  {
    const QualityTier tiers[] = {QualityTier::kLow, QualityTier::kMedium,
                                 QualityTier::kHigh, QualityTier::kUltra};
    for (QualityTier from : tiers) {
      for (QualityTier to : tiers) {
        for (bool vrs : {false, true}) {
          quality_tier::FrameContext ctx = MakeCtx(from, vrs);
          quality_tier::TransitionResult r = quality_tier::setTier(to, ctx);
          AssertSeamless(r);
          assert(ctx.geometryVersion == 7);
        }
      }
    }
    printf("PASS geometry-layout-untouched\n");
  }
  // 8. Tier table is monotonic: higher tiers never reduce effect quality.
  {
    const QualityTier tiers[] = {QualityTier::kLow, QualityTier::kMedium,
                                 QualityTier::kHigh, QualityTier::kUltra};
    for (bool vrs : {false, true}) {
      for (std::size_t i = 1u; i < 4u; ++i) {
        quality_tier::TierConfig prev =
            quality_tier::configFor(tiers[i - 1u], vrs);
        quality_tier::TierConfig next =
            quality_tier::configFor(tiers[i], vrs);
        assert(next.blurPasses >= prev.blurPasses);
        assert(next.shadowQuality >= prev.shadowQuality);
        assert(next.msaaSamples >= prev.msaaSamples);
      }
    }
    printf("PASS tier-table-monotonic\n");
  }
  // 9. T9 fail-closed pin: configFor with an invalid ordinal clamps to the
  //    safe LOW row (never zeros), for both VRS probe outcomes.
  {
    const QualityTier kBad[] = {static_cast<QualityTier>(99),
                                static_cast<QualityTier>(-1),
                                static_cast<QualityTier>(4)};
    for (QualityTier bad : kBad) {
      for (bool vrs : {false, true}) {
        quality_tier::TierConfig got = quality_tier::configFor(bad, vrs);
        quality_tier::TierConfig low =
            quality_tier::configFor(QualityTier::kLow, vrs);
        assert(got.blurPasses == low.blurPasses);
        assert(got.shadowQuality == low.shadowQuality);
        assert(got.shadingRate == low.shadingRate);
        assert(got.msaaSamples == low.msaaSamples);
        assert(got.vrsApplied == low.vrsApplied);
      }
    }
    printf("PASS config-for-invalid-clamps-to-low-row\n");
  }
  printf("ALL QUALITY TIER NAMESPACED-CONTRACT TESTS PASSED\n");
}

int main() {
  RunGlobalApiTests();
  RunNamespacedContractTests();
  printf("ALL QUALITY_TIER TESTS PASSED\n");
  return 0;
}
