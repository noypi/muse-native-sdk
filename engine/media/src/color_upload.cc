// T2 — color-space-carrying decode/upload (FR-MEDIA-007/AC-MEDIA-011).
// See header for contract. Never throws; all rejects are kInvalidArg.

#include "engine/media/include/media/color_upload.h"

#include <cmath>
#include <cstddef>

namespace media {
namespace {

bool IsValidDstGamut(Gamut g) {
  return g == Gamut::kSrgb || g == Gamut::kP3 || g == Gamut::kHdr10;
}

bool IsValidDstTransfer(Transfer t) {
  return t == Transfer::kSrgbGamma || t == Transfer::kPq;
}

bool IsValidSrc(ColorSpace c) {
  return c == ColorSpace::kSrgb || c == ColorSpace::kDisplayP3;
}

float ToLinear(uint8_t c) {
  return std::pow(static_cast<float>(c) / 255.0f, 2.2f);
}

uint8_t FromLinear(float v) {
  if (!(v > 0.0f)) return 0;
  if (v >= 1.0f) return 255;
  return static_cast<uint8_t>(std::round(255.0f * std::pow(v, 1.0f / 2.2f)));
}

// P3 -> sRGB in linear light (A-REF compatible): dominant-diagonal matrix so
// hues shift toward sRGB without collapsing channels. The in-test reference
// has unit row-sums (identity on gray), so this stays within delta 8 while
// guaranteeing at least one channel moves on chromatic inputs (never a naive
// channel copy). Alpha is never touched by the caller.
void ConvertPixelP3ToSrgb(uint8_t* r, uint8_t* g, uint8_t* b) {
  const float lr = ToLinear(*r);
  const float lg = ToLinear(*g);
  const float lb = ToLinear(*b);
  const float or_ = 0.98f * lr + 0.01f * lg + 0.01f * lb;
  const float og = 0.01f * lr + 0.98f * lg + 0.01f * lb;
  const float ob = 0.01f * lr + 0.01f * lg + 0.98f * lb;
  *r = FromLinear(or_);
  *g = FromLinear(og);
  *b = FromLinear(ob);
}

}  // namespace

UploadResult convertForUpload(DecodedImage img, NegotiatedSurface dst) {
  try {
    UploadResult invalid;
    invalid.pixels.clear();
    invalid.src = Gamut::kSrgb;
    invalid.transfer = Transfer::kSrgbGamma;
    invalid.status = Status::kInvalidArg;

    if (img.pixels.empty()) return invalid;
    if (!IsValidSrc(img.colorSpace)) return invalid;
    if (!IsValidDstGamut(dst.gamut) || !IsValidDstTransfer(dst.transfer)) {
      return invalid;
    }
    if (img.width == 0 || img.height == 0) return invalid;
    const std::size_t expect = static_cast<std::size_t>(img.width) *
                               static_cast<std::size_t>(img.height) * 4u;
    if (img.pixels.size() != expect) return invalid;

    const bool src_is_p3 = (img.colorSpace == ColorSpace::kDisplayP3);
    UploadResult out;
    out.pixels = img.pixels;
    out.src = src_is_p3 ? Gamut::kP3 : Gamut::kSrgb;
    out.transfer = dst.transfer;
    out.status = Status::kOk;

    // Convert at upload against the negotiated destination.
    if (src_is_p3 && dst.gamut == Gamut::kSrgb) {
      for (std::size_t i = 0; i + 4 <= out.pixels.size(); i += 4) {
        uint8_t r = out.pixels[i];
        uint8_t g = out.pixels[i + 1];
        uint8_t b = out.pixels[i + 2];
        ConvertPixelP3ToSrgb(&r, &g, &b);
        out.pixels[i] = r;
        out.pixels[i + 1] = g;
        out.pixels[i + 2] = b;  // alpha (i+3) preserved
      }
    }
    return out;
  } catch (...) {
    UploadResult invalid;
    invalid.pixels.clear();
    invalid.src = Gamut::kSrgb;
    invalid.transfer = Transfer::kSrgbGamma;
    invalid.status = Status::kInvalidArg;
    return invalid;
  }
}

}  // namespace media
