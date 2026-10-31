#pragma once

// T4 — cadence/jank, EXIF-upright, HDR/P3-reference animated verification
// path (FR-MEDIA-007/AC-MEDIA-012, TC-MEDIA-012). Builds on T2 color
// conversion + T3 timing; EXIF orientation is folded into the decode
// transform (extends AC-MEDIA-003 handling).
//
// Hermetic: C++17, pure in-memory, never throws, no network/env/filesystem.

#include <cstdint>
#include <vector>

#include "engine/media/decode/image_metadata.h"
#include "engine/media/include/media/image_cache.h"

namespace media {

inline constexpr double kHdrDelta = 8.0;

struct AnimFrame {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> pixels;
  uint64_t delayMs = 0;
  uint64_t readyMs = 0;
  ExifOrientation orientation = ExifOrientation::kNormal;
  ColorSpace color = ColorSpace::kSrgb;
};

struct AnimSequence {
  std::vector<AnimFrame> frames;
  uint32_t loopCount = 1;
};

struct ReferenceFrames {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<std::vector<uint8_t>> pixels;
};

struct HdrCheck {
  double maxDelta = 0.0;
  bool cadenceOk = false;
  bool upright = false;
  Status status = Status::kOk;
};

// Verifies an animated sequence against upright sRGB reference frames.
//  - empty sequence -> {0, false, false, kInvalidArg}
//  - cadenceOk = every frame readyMs <= cumulative delayMs deadline
//  - upright = every frame valid and oriented dims match the reference dims
//  - maxDelta = per-channel max absolute delta of oriented + P3->sRGB
//    converted output vs the reference bytes (255 on shape mismatch)
//  - truncated frame payload (size != w*h*4) -> {delta, cadence, false,
//    kInvalidArg}; divergent content stays kOk with large maxDelta.
// Never throws.
HdrCheck verifyAnimatedHdr(AnimSequence seq, ReferenceFrames ref);

}  // namespace media
