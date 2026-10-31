#pragma once

// T2 — color-space-carrying decode/upload (FR-MEDIA-007/AC-MEDIA-011,
// TC-MEDIA-011). Converts at upload against the negotiated destination from
// `engine/graphics/color/color_space.h`; there is no naive channel-copy path.
//
// Hermetic: C++17, pure in-memory, never throws, no network/env/filesystem.

#include <cstdint>
#include <vector>

#include "engine/graphics/color/color_space.h"
#include "engine/media/decode/image_metadata.h"
#include "engine/media/include/media/image_cache.h"

namespace media {

struct UploadResult {
  std::vector<uint8_t> pixels;
  Gamut src = Gamut::kSrgb;
  Transfer transfer = Transfer::kSrgbGamma;
  Status status = Status::kOk;
};

// Converts `img` for upload to `dst`.
//  - empty pixels, size != w*h*4, unknown source color space, or unknown
//    destination gamut/transfer -> {src best-effort, kInvalidArg}
//  - P3 source to sRGB destination -> gamma-2.2 + linear-matrix conversion
//    (A-REF compatible, within per-channel delta 8, never a naive copy)
//  - otherwise -> passthrough copy with src gamut + negotiated transfer.
// Never throws.
UploadResult convertForUpload(DecodedImage img, NegotiatedSurface dst);

}  // namespace media
