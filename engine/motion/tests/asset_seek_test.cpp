// T8 contract tests (seek half) — FR-MOTION-010 / AC-MOTION-033.
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: inline byte
// payloads, no wall clock, no randomness.
// Binds to the T7 control surface (plan: T8 Depends on T7) through
// `AssetError AssetPlayer::seekToMarker(const std::string&)` and
// `float AssetPlayer::progress() const`, plus the T8 scale/variant surface in
// `motion/asset_scale.h`.
//
// Seek-vs-sequential property (plan Detail: "seek-to-marker must match
// sequential playback within tolerance 1e-4"): with no separate stepping API
// in the plan `Interface:` line, "sequential" is expressed through the public
// boundary as seeking through intermediate markers before the target versus
// seeking to the target directly — both orders must converge within 1e-4.
// If the coder adds a deterministic advance/step helper, call sites reconcile
// to it (expectations unchanged).
//
// ASSUMPTION (marker catalogue): the plan names no markers; the tests use
// "intro" and "mid" as named markers expecting kOk after load, and the empty
// marker expecting kDecodeFailed (never kOk) per the T7 contract. Named-marker
// expectations reconcile to the coder's documented catalogue if it defines one.
//
// ASSUMPTION (decodable-bytes proxy): inherits T7 — non-empty printable
// payloads load kOk; empty bytes are kUnsupported.
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

bool near(float a, float b) { return std::fabs(a - b) <= kSeekTol; }

// Direct seek converges with sequential seeks through an earlier marker
// within tolerance 1e-4 (AC-MOTION-033 seek half).
bool check_seek_matches_sequential_within_tolerance() {
  try {
    AssetPlayer direct;
    AssetPlayer sequential;
    if (direct.load(AssetFormat::kLottie, payload("lottie-seek-eq")) !=
        AssetError::kOk) {
      return false;
    }
    if (sequential.load(AssetFormat::kLottie, payload("lottie-seek-eq")) !=
        AssetError::kOk) {
      return false;
    }
    if (direct.play() != AssetError::kOk) return false;
    if (sequential.play() != AssetError::kOk) return false;
    if (direct.seekToMarker("mid") != AssetError::kOk) return false;
    if (sequential.seekToMarker("intro") != AssetError::kOk) return false;
    if (sequential.seekToMarker("mid") != AssetError::kOk) return false;
    return near(direct.progress(), sequential.progress());
  } catch (...) {
    return false;
  }
}

// Repeated seek to the same marker is deterministic within tolerance.
bool check_repeated_seek_is_deterministic() {
  try {
    AssetPlayer p;
    if (p.load(AssetFormat::kRive, payload("rive-seek-repeat")) !=
        AssetError::kOk) {
      return false;
    }
    if (p.play() != AssetError::kOk) return false;
    if (p.seekToMarker("mid") != AssetError::kOk) return false;
    float first = p.progress();
    if (p.seekToMarker("mid") != AssetError::kOk) return false;
    return near(first, p.progress());
  } catch (...) {
    return false;
  }
}

// Seek holds under time-scale extremes: scaled seek converges with unscaled
// seek within tolerance (AC-MOTION-031 x AC-MOTION-033 interaction).
bool check_seek_matches_across_time_scales() {
  try {
    AssetPlayer a;
    AssetPlayer b;
    if (a.load(AssetFormat::kAvd, payload("avd-seek-scale")) !=
        AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (b.load(AssetFormat::kAvd, payload("avd-seek-scale")) !=
        AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    motion_setTimeScale(10.0f);
    if (a.play() != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    if (a.seekToMarker("mid") != AssetError::kOk) {
      motion_setTimeScale(1.0f);
      return false;
    }
    motion_setTimeScale(1.0f);
    if (b.play() != AssetError::kOk) return false;
    if (b.seekToMarker("mid") != AssetError::kOk) return false;
    return near(a.progress(), b.progress());
  } catch (...) {
    motion_setTimeScale(1.0f);
    return false;
  }
}

// Seek holds under the reduced-motion variant: same marker converges within
// tolerance in kFull and kReduced (AC-MOTION-033 x AC-MOTION-023).
bool check_seek_matches_across_variants() {
  try {
    AssetPlayer full;
    AssetPlayer reduced;
    if (full.load(AssetFormat::kSvg, payload("svg-seek-variant")) !=
        AssetError::kOk) {
      motion_setReducedMotion(false);
      return false;
    }
    if (reduced.load(AssetFormat::kSvg, payload("svg-seek-variant")) !=
        AssetError::kOk) {
      motion_setReducedMotion(false);
      return false;
    }
    motion_setReducedMotion(false);
    if (full.play() != AssetError::kOk) {
      motion_setReducedMotion(false);
      return false;
    }
    if (full.seekToMarker("mid") != AssetError::kOk) {
      motion_setReducedMotion(false);
      return false;
    }
    if (motion_setReducedMotion(true) != AssetError::kOk) {
      motion_setReducedMotion(false);
      return false;
    }
    if (reduced.play() != AssetError::kOk) {
      motion_setReducedMotion(false);
      return false;
    }
    if (reduced.seekToMarker("mid") != AssetError::kOk) {
      motion_setReducedMotion(false);
      return false;
    }
    bool ok = near(full.progress(), reduced.progress());
    motion_setReducedMotion(false);
    return ok;
  } catch (...) {
    motion_setReducedMotion(false);
    return false;
  }
}

// Empty marker is rejected (kDecodeFailed, never kOk) — boundary case.
bool check_empty_marker_never_ok() {
  try {
    AssetPlayer p;
    if (p.load(AssetFormat::kLottie, payload("lottie-empty-marker")) !=
        AssetError::kOk) {
      return false;
    }
    if (p.play() != AssetError::kOk) return false;
    return p.seekToMarker("") == AssetError::kDecodeFailed;
  } catch (...) {
    return false;
  }
}

// Seek before load is gated (kNotPrepared) — T7 gating through the T8 lens.
bool check_seek_before_load_returns_not_prepared() {
  try {
    AssetPlayer p;
    return p.seekToMarker("mid") == AssetError::kNotPrepared;
  } catch (...) {
    return false;
  }
}

// Post-seek progress always stays in [0,1].
bool check_progress_in_range_after_seek() {
  try {
    AssetPlayer p;
    if (p.load(AssetFormat::kRive, payload("rive-seek-range")) !=
        AssetError::kOk) {
      return false;
    }
    if (p.play() != AssetError::kOk) return false;
    if (p.seekToMarker("intro") != AssetError::kOk) return false;
    float v = p.progress();
    return v >= 0.0f && v <= 1.0f;
  } catch (...) {
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
  RUN_CASE(check_seek_matches_sequential_within_tolerance);
  RUN_CASE(check_repeated_seek_is_deterministic);
  RUN_CASE(check_seek_matches_across_time_scales);
  RUN_CASE(check_seek_matches_across_variants);
  RUN_CASE(check_empty_marker_never_ok);
  RUN_CASE(check_seek_before_load_returns_not_prepared);
  RUN_CASE(check_progress_in_range_after_seek);
  if (failures > 0) {
    std::printf("asset_seek_test: %d failing\n", failures);
    return 1;
  }
  std::printf("asset_seek_test: all passing\n");
  return 0;
}
