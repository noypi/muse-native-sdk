#ifndef MOTION_ASSET_H_
#define MOTION_ASSET_H_

#include <cstdint>
#include <string>
#include <vector>

// Unified playback/control surface for all asset formats
// (FR-MOTION-010 / AC-MOTION-030).
//
// One AssetPlayer imports a single Lottie, Rive, AVD, or SVG payload and
// drives it through the same control API. Format parsers feed the T1
// retarget graph; raster output goes to the consumed graphics sink
// interface (no graphics implementation here).
//
// Gating: every method except load() requires a successful load() first and
// returns AssetError::kNotPrepared otherwise. progress() returns float so it
// cannot carry kNotPrepared; it returns exactly 0.0f before load. Gating
// state is per-player-instance (no shared mutable load state).
//
// Decoding proxy: any non-empty payload of printable bytes loads as kOk for
// its declared format; empty bytes are unknown-format input and return
// kUnsupported; payloads containing non-text bytes are rejected with
// kDecodeFailed. Nothing here throws; malformed or oversized input is
// reported via AssetError, never via an exception.

enum class AssetFormat { kLottie, kRive, kAvd, kSvg };

enum class AssetError { kOk, kUnsupported, kDecodeFailed, kNotPrepared };

class AssetPlayer {
 public:
  AssetPlayer() = default;

  // Imports one asset of the declared format. Empty input returns
  // kUnsupported; undecodable bytes return kDecodeFailed; a decodable
  // payload returns kOk and arms the player. Failed loads leave the
  // previous gating state unchanged. Never throws.
  AssetError load(AssetFormat f, const std::vector<uint8_t>& bytes);

  // Starts playback. kNotPrepared before a successful load; kOk after
  // (re-entrant: playing twice stays kOk). Never throws.
  AssetError play();

  // Pauses playback. kNotPrepared before a successful load; kOk after.
  // Never throws.
  AssetError pause();

  // Seeks to a named marker. kNotPrepared before a successful load;
  // kDecodeFailed for the empty marker (invalid marker, never kOk);
  // kOk for a named marker. Never throws.
  AssetError seekToMarker(const std::string& marker);

  // Sets the playback rate. kNotPrepared before a successful load; kOk
  // for finite values after load (T8 owns clamping semantics).
  // Never throws.
  AssetError setSpeed(float s);

  // Playback position in [0, 1]; exactly 0.0f before load. Never throws.
  float progress() const;

 private:
  bool prepared_ = false;
  bool playing_ = false;
  float speed_ = 1.0f;
  float progress_ = 0.0f;
};

#endif  // MOTION_ASSET_H_
