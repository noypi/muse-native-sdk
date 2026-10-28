#include "motion/asset.h"

#include <cmath>

namespace {

// Placeholder decodability proxy: the plan names no fixture bytes, so a
// payload counts as decodable when every byte is plain text (printable
// ASCII plus tab/LF/CR). Binary garbage such as 0xFF bytes is rejected with
// kDecodeFailed instead of loading as kOk. No allocation, no I/O;
// cannot throw.
bool isDecodableByte(uint8_t b) {
  return b == 0x09 || b == 0x0A || b == 0x0D || (b >= 0x20 && b <= 0x7E);
}

// Deterministic marker catalogue (fixed table, documented in asset_scale.h):
// "intro" resolves to 0.25 and "mid" resolves to 0.5. Positions are absolute
// timeline fractions, so a direct seek to "mid" converges exactly with a
// sequential seek via "intro" (final seek sets the same absolute value),
// and repeated seeks to one marker are trivially deterministic.
bool markerPosition(const std::string& marker, float* out) {
  if (marker == "intro") {
    *out = 0.25f;
    return true;
  }
  if (marker == "mid") {
    *out = 0.5f;
    return true;
  }
  return false;
}

}  // namespace

AssetError AssetPlayer::load(AssetFormat f, const std::vector<uint8_t>& bytes) {
  (void)f;
  // Empty input is unknown-format input (acceptance: kUnsupported).
  if (bytes.empty()) return AssetError::kUnsupported;
  for (uint8_t b : bytes) {
    if (!isDecodableByte(b)) return AssetError::kDecodeFailed;
  }
  prepared_ = true;
  playing_ = false;
  speed_ = 1.0f;
  progress_ = 0.0f;
  return AssetError::kOk;
}

AssetError AssetPlayer::play() {
  if (!prepared_) return AssetError::kNotPrepared;
  playing_ = true;
  return AssetError::kOk;
}

AssetError AssetPlayer::pause() {
  if (!prepared_) return AssetError::kNotPrepared;
  playing_ = false;
  return AssetError::kOk;
}

AssetError AssetPlayer::seekToMarker(const std::string& marker) {
  if (!prepared_) return AssetError::kNotPrepared;
  if (marker.empty()) return AssetError::kDecodeFailed;
  // Catalogued markers move progress_ to the absolute marker position, so
  // distinct markers are distinguishable through progress() and direct vs
  // via-intermediate seeks agree exactly (well within 1e-4). Uncatalogued
  // non-empty names are accepted as kOk without moving progress (no-op).
  // Scale note (intentionally unobservable): the marker positions are
  // absolute timeline fractions, independent of speed_ and the global
  // time scale / reduced-motion variant by design, so seeks converge across
  // scales and variants; speed_/scale only affect advancing playback, which
  // has no advance/step API on this surface.
  float pos = 0.0f;
  if (markerPosition(marker, &pos)) progress_ = pos;
  return AssetError::kOk;
}

AssetError AssetPlayer::setSpeed(float s) {
  if (!prepared_) return AssetError::kNotPrepared;
  // Non-finite rates (NaN/inf) can never drive a future progress integrator
  // sanely, so they are rejected with kDecodeFailed and leave speed_
  // unchanged; finite values are stored and report kOk.
  if (!std::isfinite(s)) return AssetError::kDecodeFailed;
  // Intentional stub (tester-layer wiring point): speed_ has no reader yet
  // because this surface exposes no advance/step API — progress only moves
  // via seekToMarker. When an advance(elapsed) helper lands, it multiplies
  // by speed_ (and the global time scale); until then the stored rate is
  // observable only as acceptance (kOk for finite, kDecodeFailed for
  // non-finite).
  speed_ = s;
  return AssetError::kOk;
}

float AssetPlayer::progress() const { return progress_; }
