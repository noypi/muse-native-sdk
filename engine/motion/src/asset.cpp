#include "motion/asset.h"

namespace {

// Placeholder decodability proxy: the plan names no fixture bytes, so a
// payload counts as decodable when every byte is plain text (printable
// ASCII plus tab/LF/CR). Binary garbage such as 0xFF bytes is rejected with
// kDecodeFailed instead of loading as kOk. No allocation, no I/O;
// cannot throw.
bool isDecodableByte(uint8_t b) {
  return b == 0x09 || b == 0x0A || b == 0x0D || (b >= 0x20 && b <= 0x7E);
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
  return AssetError::kOk;
}

AssetError AssetPlayer::setSpeed(float s) {
  if (!prepared_) return AssetError::kNotPrepared;
  speed_ = s;
  return AssetError::kOk;
}

float AssetPlayer::progress() const { return progress_; }
