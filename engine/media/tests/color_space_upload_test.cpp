// Batch 1 — color-space-carrying decode/upload (FR-MEDIA-007/AC-MEDIA-011,
// TC-MEDIA-011). Binds ONLY to the T2 `Interface:` line in plan.md; never to
// implementation.
//
// Hermetic: C++17 + the new engine/media/include/media/color_upload.h, the
// shared `engine/media/decode/image_metadata.h` DecodedImage, and the
// `engine/graphics/color/color_space.h` destination negotiation only; no
// network, no env/config reads, no filesystem, no live services.
//
// Expected state: RED — engine/media/include/media/color_upload.h does not
// exist yet, so this file fails to compile until the coder implements T2
// (there is deliberately no naive-memcpy path to test against).
//
// Contract assumptions (see unit-tests/T2.md):
//   A-NS:   everything in namespace `media`; header
//           `engine/media/include/media/color_upload.h`.
//   A-IMG:  `media::DecodedImage` is the shared rich type from
//           `engine/media/decode/image_metadata.h` (width/height,
//           RGBA8 `pixels`, `ColorSpace colorSpace`).
//   A-OUT:  `struct UploadResult { std::vector<uint8_t> pixels; Gamut src;
//           Transfer transfer; Status status; };`
//           `UploadResult convertForUpload(DecodedImage img,
//           NegotiatedSurface dst);` with `Gamut`/`Transfer`/`NegotiatedSurface`
//           from `engine/graphics/color/color_space.h` (global scope),
//           `Status::{kOk,kOverBudget,kInvalidArg}` shared with T1.
//   A-REF:  P3->sRGB reference is the in-test linear matrix below;
//           per-channel delta <= 8. A naive channel copy must fail the
//           "not a naive copy" assertions by construction.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/graphics/color/color_space.h"
#include "engine/media/decode/image_metadata.h"
#include "engine/media/include/media/color_upload.h"

// --- tiny harness (matches engine/media/tests convention) -------------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

// In-test P3->sRGB reference (A-REF): linearize with gamma 2.2, apply the
// P3-to-sRGB matrix, re-encode. The coder implements its own conversion;
// values must land within per-channel delta 8 of this reference.
static uint8_t RefP3ToSrgb(uint8_t p3, int row) {
  static const float kM[3][3] = {{1.2249f, -0.2247f, -0.0002f},
                                 {-0.0420f, 1.0419f, 0.0001f},
                                 {-0.0197f, -0.0786f, 1.0983f}};
  const float c = std::pow(p3 / 255.0f, 2.2f);
  float v = kM[row][0] * c + kM[row][1] * c + kM[row][2] * c;
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  return static_cast<uint8_t>(std::round(255.0f * std::pow(v, 1.0f / 2.2f)));
}

static media::DecodedImage MakeP3Teal() {
  media::DecodedImage img;
  img.width = 1;
  img.height = 1;
  img.colorSpace = media::ColorSpace::kDisplayP3;
  img.pixels = {64, 180, 200, 255};
  return img;
}

static NegotiatedSurface MakeSrgbDst() {
  NegotiatedSurface dst;
  dst.gamut = Gamut::kSrgb;
  dst.transfer = Transfer::kSrgbGamma;
  dst.fallback = false;
  return dst;
}

// --- cases -----------------------------------------------------------------

// Empty pixels are a synchronous kInvalidArg reject that never throws.
static void check_empty_pixels_rejected() {
  media::DecodedImage img;
  img.width = 0;
  img.height = 0;
  img.colorSpace = media::ColorSpace::kSrgb;
  img.pixels.clear();
  bool threw = false;
  media::UploadResult r{{0}, Gamut::kSrgb, Transfer::kSrgbGamma,
                         media::Status::kOk};
  try {
    r = media::convertForUpload(img, MakeSrgbDst());
  } catch (...) {
    threw = true;
  }
  CHECK("empty pixels never throws", !threw);
  CHECK("empty pixels status kInvalidArg",
        r.status == media::Status::kInvalidArg);
}

// Unknown destination gamut is a synchronous kInvalidArg reject.
static void check_unknown_gamut_rejected() {
  NegotiatedSurface dst = MakeSrgbDst();
  dst.gamut = static_cast<Gamut>(99);
  bool threw = false;
  media::UploadResult r{{0}, Gamut::kSrgb, Transfer::kSrgbGamma,
                         media::Status::kOk};
  try {
    r = media::convertForUpload(MakeP3Teal(), dst);
  } catch (...) {
    threw = true;
  }
  CHECK("unknown gamut never throws", !threw);
  CHECK("unknown gamut status kInvalidArg",
        r.status == media::Status::kInvalidArg);
}

// sRGB image to sRGB destination converts cleanly (kOk) with bytes intact.
static void check_srgb_to_srgb_ok() {
  media::DecodedImage img;
  img.width = 1;
  img.height = 1;
  img.colorSpace = media::ColorSpace::kSrgb;
  img.pixels = {10, 20, 30, 255};
  media::UploadResult r = media::convertForUpload(img, MakeSrgbDst());
  CHECK("srgb->srgb status kOk", r.status == media::Status::kOk);
  CHECK("srgb->srgb reports src gamut", r.src == Gamut::kSrgb);
  CHECK("srgb->srgb reports transfer", r.transfer == Transfer::kSrgbGamma);
  CHECK("srgb->srgb pixel bytes match dims", r.pixels.size() == 4u);
}

// P3 image converts toward sRGB per A-REF and is never a naive channel copy.
static void check_p3_converts_within_delta() {
  media::UploadResult r = media::convertForUpload(MakeP3Teal(), MakeSrgbDst());
  CHECK("p3->srgb status kOk", r.status == media::Status::kOk);
  CHECK("p3->srgb reports src gamut", r.src == Gamut::kP3);
  CHECK("p3->srgb pixel bytes match dims", r.pixels.size() == 4u);
  const uint8_t expect[3] = {RefP3ToSrgb(64, 0), RefP3ToSrgb(180, 1),
                              RefP3ToSrgb(200, 2)};
  bool within = r.pixels.size() == 4u;
  for (int c = 0; c < 3 && within; ++c) {
    const int got = r.pixels[static_cast<std::size_t>(c)];
    if (std::abs(got - expect[c]) > 8) within = false;
  }
  CHECK("P3->sRGB within per-channel delta 8", within);
  CHECK("alpha preserved", r.pixels.size() == 4u && r.pixels[3] == 255);
  CHECK("conversion is not a naive copy",
        r.pixels[0] != 64 || r.pixels[1] != 180 || r.pixels[2] != 200);
}

// Destination P3 request carries the negotiated transfer through.
static void check_p3_destination_carries_transfer() {
  media::DecodedImage img = MakeP3Teal();
  NegotiatedSurface dst;
  dst.gamut = Gamut::kP3;
  dst.transfer = Transfer::kSrgbGamma;
  dst.fallback = false;
  media::UploadResult r = media::convertForUpload(img, dst);
  CHECK("p3 dst status kOk", r.status == media::Status::kOk);
  CHECK("p3 dst reports transfer", r.transfer == Transfer::kSrgbGamma);
}

// Security: corrupt pixel payloads (size != w*h*4) never throw and reject
// with kInvalidArg instead of reading out of bounds.
static void check_corrupt_pixels_never_throw() {
  media::DecodedImage img;
  img.width = 2;
  img.height = 2;
  img.colorSpace = media::ColorSpace::kDisplayP3;
  img.pixels = {0xDE, 0xAD, 0xBE};  // truncated: claims 16 bytes, holds 3
  bool threw = false;
  media::UploadResult r{{0}, Gamut::kSrgb, Transfer::kSrgbGamma,
                         media::Status::kOk};
  try {
    r = media::convertForUpload(img, MakeSrgbDst());
  } catch (...) {
    threw = true;
  }
  CHECK("corrupt pixels never throws", !threw);
  CHECK("corrupt pixels status kInvalidArg",
        r.status == media::Status::kInvalidArg);
}

int main() {
  check_empty_pixels_rejected();
  check_unknown_gamut_rejected();
  check_srgb_to_srgb_ok();
  check_p3_converts_within_delta();
  check_p3_destination_carries_transfer();
  check_corrupt_pixels_never_throw();
  if (s_failures == 0) {
    std::printf("ALL GREEN color_space_upload_test\n");
    return 0;
  }
  std::printf("%d FAILURES color_space_upload_test\n", s_failures);
  return 1;
}
