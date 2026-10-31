// T3 — animated decode implementation (FR-MEDIA-007/AC-MEDIA-010).
//
// Pure `nowMs`-since-load state machine; pauses hold the last shown frame
// without shifting the timeline. Never throws (all paths noexcept + guarded).

#include "engine/media/include/media/animated_decode.h"

namespace media {
namespace {

// Builds the stub 1x1 RGBA8 frame for `index` with its authored delay.
Frame MakeFrame(const AnimPlayer& p, uint32_t index) noexcept {
  Frame f;
  try {
    f.index = index;
    f.width = 1;
    f.height = 1;
    f.pixels.assign(4, 0);
    if (index < p.delays.size()) f.delayMs = p.delays[index];
  } catch (...) {
    // Never throws: fall back to a bare frame on allocation failure.
    f = Frame{};
    f.index = index;
  }
  return f;
}

// Live frame index for `nowMs` (caller guarantees prepared, non-empty).
// Saturating uint64 accumulation + division avoids any overflow, so hostile
// clocks and large tables stay exact without wide arithmetic.
uint32_t LiveIndex(const AnimPlayer& p, uint64_t nowMs) noexcept {
  const std::size_t n = p.delays.size();
  uint64_t total = 0;
  for (uint64_t d : p.delays) {
    if (d > UINT64_MAX - total) {
      total = UINT64_MAX;
      break;
    }
    total += d;
  }
  if (total == 0) return 0;
  if (p.loopCount != 0) {
    if (nowMs / total >= p.loopCount) {
      return static_cast<uint32_t>(n - 1);
    }
  }
  uint64_t t = nowMs % total;
  for (std::size_t i = 0; i < n; ++i) {
    if (t < p.delays[i]) return static_cast<uint32_t>(i);
    t -= p.delays[i];
  }
  return static_cast<uint32_t>(n - 1);
}

}  // namespace

AssetError animLoad(AnimPlayer& p, const std::vector<uint64_t>& delaysMs,
                    uint32_t loopCount) noexcept {
  try {
    if (delaysMs.empty()) {
      try {
        p.prepared = false;
      } catch (...) {
      }
      return AssetError::kInvalidArg;
    }
    p.delays = delaysMs;
    p.loopCount = loopCount;
    p.heldIndex = 0;
    p.prepared = true;
    return AssetError::kOk;
  } catch (...) {
    return AssetError::kInvalidArg;
  }
}

AnimStatus animAdvance(AnimPlayer& p, uint64_t nowMs, bool onScreen) noexcept {
  try {
    if (!p.prepared || p.delays.empty()) {
      AnimStatus s;
      s.paused = true;
      s.err = AssetError::kNotPrepared;
      return s;
    }
    bool paused = false;
    try {
      paused = !onScreen || motionReduced();
    } catch (...) {
      paused = !onScreen;
    }
    if (paused) {
      uint32_t held = p.heldIndex < p.delays.size()
                          ? p.heldIndex
                          : static_cast<uint32_t>(p.delays.size() - 1);
      AnimStatus s;
      s.f = MakeFrame(p, held);
      s.paused = true;
      s.err = AssetError::kOk;
      return s;
    }
    uint32_t idx = LiveIndex(p, nowMs);
    p.heldIndex = idx;
    AnimStatus s;
    s.f = MakeFrame(p, idx);
    s.paused = false;
    s.err = AssetError::kOk;
    return s;
  } catch (...) {
    AnimStatus s;
    s.paused = true;
    s.err = AssetError::kNotPrepared;
    return s;
  }
}

}  // namespace media
