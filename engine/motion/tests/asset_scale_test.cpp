// T8 contract tests (scale half) — FR-MOTION-010 / AC-MOTION-031,
// NFR-MOTION-005 / AC-MOTION-022, AC-MOTION-023 (asset-narrow).
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: inline byte
// payloads, no wall clock, no randomness.
// Binds to the T8 `Interface:` line (reproduced verbatim in unit-tests/T8.md):
// `void motion_setTimeScale(float scale);`
// `AssetError motion_setReducedMotion(bool enabled);`
// `AssetVariant motion_activeVariant(const AssetPlayer& p);`
// `enum class AssetVariant { kFull, kReduced };`
// plus `bool motion_lastBudgetShed()` (plan Files/Detail line).
//
// ASSUMPTION (decodable-bytes proxy): inherits the T7 contract — any
// non-empty printable payload loads kOk for its declared format; empty bytes
// are kUnsupported. If the coder's decoders reject the placeholders, ONLY the
// byte-content expectations reconcile, never the control-flow expectations.
//
// ASSUMPTION (scale clamp observability): the plan names no scale getter, so
// "scale <= 0 clamps to min positive step" is asserted as continued usability
// (every control call still returns a valid AssetError, never hangs, never
// throws, progress stays in [0,1]); the coder's clamp is verified by
// essential feedback staying perceptible, not by reading the clamped value.
//
// ASSUMPTION (activeVariant default): on a never-loaded player,
// motion_activeVariant returns kFull (no reduced-motion state yet) and never
// throws; gating errors are carried by AssetError methods, not the variant
// query.
//
// ASSUMPTION (budget shed observability): motion_lastBudgetShed() reports
// whether the last evaluation shed decorative layers. The nominal path (no
// overrun) must read false; the overrun trigger belongs to the consumed perf
// budget-guard signal, so the shed-decorative-first ordering under a forced
// overrun reconciles to the coder's documented trigger, never to weaker
// expectations here.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "motion/asset.h"
#include "motion/asset_scale.h"

namespace {

const float kSeekTol = 1e-4f;

std::vector<uint8_t> payload(const char* s) {
  std::string t(s);
  return std::vector<uint8_t>(t.begin(), t.end());
}

bool inRange(float v) { return v >= 0.0f && v <= 1.0f; }

// Scale 0 stays usable: every control call returns a valid error, nothing
// throws, progress stays in range (AC-MOTION-031, AC-MOTION-022).
bool check_scale_zero_stays_usable() {
  try {
    motion_setTimeScale(0.0f);
    AssetPlayer p;
    if (p.load(AssetFormat::kLottie, payload("lottie-scale-zero")) !=
        AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (p.play() != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (p.seekToMarker("intro") != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (!inRange(p.progress())) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (p.pause() != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    motion_setTimeScale(1.0f);
    return true;
  } catch (...) {
    motion_setTimeScale(1.0f);
    return false;
  }
}

// Scale 10x stays usable with no hang/double-fire (AC-MOTION-031).
bool check_scale_ten_stays_usable() {
  try {
    motion_setTimeScale(10.0f);
    AssetPlayer p;
    if (p.load(AssetFormat::kRive, payload("rive-scale-ten")) !=
        AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (p.play() != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    // Double-fire guard: playing twice stays kOk even at 10x.
    if (p.play() != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (!inRange(p.progress())) {
      motion_setTimeScale(1.0f);
      return false;
    }
    motion_setTimeScale(1.0f);
    return true;
  } catch (...) {
    motion_setTimeScale(1.0f);
    return false;
  }
}

// Negative scale clamps the same as zero: usable, never throws
// (plan: "scale <= 0 clamps to min positive step").
bool check_negative_scale_clamps_usable() {
  try {
    motion_setTimeScale(-2.0f);
    AssetPlayer p;
    if (p.load(AssetFormat::kAvd, payload("avd-scale-neg")) !=
        AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (p.play() != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (!inRange(p.progress())) {
      motion_setTimeScale(1.0f);
      return false;
    }
    motion_setTimeScale(1.0f);
    return true;
  } catch (...) {
    motion_setTimeScale(1.0f);
    return false;
  }
}

// Essential feedback stays perceptible at min scale: seek still lands and
// progress still reports in range (AC-MOTION-022 narrow).
bool check_essential_feedback_perceptible_at_zero_scale() {
  try {
    motion_setTimeScale(0.0f);
    AssetPlayer p;
    if (p.load(AssetFormat::kSvg, payload("svg-essential")) != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (p.play() != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (p.seekToMarker("intro") != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    bool ok = inRange(p.progress());
    motion_setTimeScale(1.0f);
    return ok;
  } catch (...) {
    motion_setTimeScale(1.0f);
    return false;
  }
}

// Reduced motion renders the kReduced variant; disabling restores kFull
// (AC-MOTION-033 reduce-motion half, AC-MOTION-023 narrow).
bool check_reduced_motion_selects_variant() {
  try {
    AssetPlayer p;
    if (p.load(AssetFormat::kLottie, payload("lottie-variant")) !=
        AssetError::kOk) {
      motion_setReducedMotion(false);
      return false;
    }
    if (motion_setReducedMotion(true) != AssetError::kOk) {
      motion_setReducedMotion(false);
      return false;
    }
    if (motion_activeVariant(p) != AssetVariant::kReduced) {
      motion_setReducedMotion(false);
      return false;
    }
    if (motion_setReducedMotion(false) != AssetError::kOk) {
      return false;
    }
    return motion_activeVariant(p) == AssetVariant::kFull;
  } catch (...) {
    motion_setReducedMotion(false);
    return false;
  }
}

// Variant query on a never-loaded player defaults to kFull, never throws
// (ASSUMPTION: gating lives in AssetError methods, not the variant query).
bool check_variant_default_is_full() {
  try {
    AssetPlayer p;
    motion_setReducedMotion(false);
    return motion_activeVariant(p) == AssetVariant::kFull;
  } catch (...) {
    return false;
  }
}

// Nominal path sheds nothing: lastBudgetShed reads false when no overrun
// occurred (frame-budget guard, decorative-first shedding on overrun is the
// coder's documented trigger per ASSUMPTION above).
bool check_budget_shed_false_on_nominal_path() {
  try {
    motion_setTimeScale(1.0f);
    motion_setReducedMotion(false);
    AssetPlayer p;
    if (p.load(AssetFormat::kLottie, payload("lottie-budget")) !=
        AssetError::kOk) {
      return false;
    }
    if (p.play() != AssetError::kOk) return false;
    return motion_lastBudgetShed() == false;
  } catch (...) {
    return false;
  }
}

// Scale APIs never throw, even around unusable values.
bool check_scale_apis_never_throw() {
  try {
    motion_setTimeScale(0.0f);
    motion_setTimeScale(10.0f);
    motion_setTimeScale(-1.0f);
    motion_setTimeScale(1.0f);
    motion_setReducedMotion(true);
    motion_setReducedMotion(false);
    AssetPlayer p;
    (void)motion_activeVariant(p);
    (void)motion_lastBudgetShed();
    return true;
  } catch (...) {
    motion_setTimeScale(1.0f);
    motion_setReducedMotion(false);
    return false;
  }
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_scale_zero_stays_usable);
  RUN_CASE(check_scale_ten_stays_usable);
  RUN_CASE(check_negative_scale_clamps_usable);
  RUN_CASE(check_essential_feedback_perceptible_at_zero_scale);
  RUN_CASE(check_reduced_motion_selects_variant);
  RUN_CASE(check_variant_default_is_full);
  RUN_CASE(check_budget_shed_false_on_nominal_path);
  RUN_CASE(check_scale_apis_never_throw);
  // Silence unused warning for the tolerance shared with the seek suite.
  (void)kSeekTol;
  if (failures > 0) {
    std::printf("asset_scale_test: %d failing\n", failures);
    return 1;
  }
  std::printf("asset_scale_test: all passing\n");
  return 0;
}
