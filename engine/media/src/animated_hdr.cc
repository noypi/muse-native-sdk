// T4 — cadence/jank, EXIF-upright, HDR/P3-reference animated verification.
// See header for contract. Never throws; all rejects are kInvalidArg.

#include "engine/media/include/media/animated_hdr.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace media {
namespace {

// Gamma-2.2 linearize / re-encode (T2 A-REF family).
float ToLinear(uint8_t c) {
  return std::pow(static_cast<float>(c) / 255.0f, 2.2f);
}

uint8_t FromLinear(float v) {
  if (!(v > 0.0f)) return 0;
  if (v >= 1.0f) return 255;
  return static_cast<uint8_t>(std::round(255.0f * std::pow(v, 1.0f / 2.2f)));
}

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

// Oriented dimensions for an EXIF value (transpose iff value >= 5).
void OrientedDims(uint32_t w, uint32_t h, uint16_t v, uint32_t* dw,
                  uint32_t* dh) {
  if (v >= 5) {
    *dw = h;
    *dh = w;
  } else {
    *dw = w;
    *dh = h;
  }
}

// Map an upright output pixel (dx,dy) back to its stored source pixel.
// Matches image_metadata.h::applyOrientation sense (seam-correct, EXIF
// standard CW): case 6 (rotate-90-CW) is sx = dy; sy = h - 1 - dx, and
// case 8 (rotate-90-CCW) is sx = w - 1 - dy; sy = dx.
void MapSource(uint32_t w, uint32_t h, uint16_t v, uint32_t dx, uint32_t dy,
               uint32_t* sx, uint32_t* sy) {
  switch (v) {
    case 2:  // flip-H
      *sx = w - 1 - dx;
      *sy = dy;
      break;
    case 3:  // 180
      *sx = w - 1 - dx;
      *sy = h - 1 - dy;
      break;
    case 4:  // flip-V
      *sx = dx;
      *sy = h - 1 - dy;
      break;
    case 5:  // transpose
      *sx = dy;
      *sy = dx;
      break;
    case 6:  // rotate-90-CW (applyOrientation sense)
      *sx = dy;
      *sy = h - 1 - dx;
      break;
    case 7:  // transverse
      *sx = w - 1 - dy;
      *sy = h - 1 - dx;
      break;
    case 8:  // rotate-90-CCW (applyOrientation sense)
      *sx = w - 1 - dy;
      *sy = dx;
      break;
    default:
      *sx = dx;
      *sy = dy;
      break;
  }
}

}  // namespace

HdrCheck verifyAnimatedHdr(AnimSequence seq, ReferenceFrames ref) {
  try {
    HdrCheck out;
    out.maxDelta = 0.0;
    out.cadenceOk = true;
    out.upright = true;
    out.status = Status::kOk;

    if (seq.frames.empty()) {
      out.maxDelta = 0.0;
      out.cadenceOk = false;
      out.upright = false;
      out.status = Status::kInvalidArg;
      return out;
    }

    // Cadence: each frame ready before its authored deadline
    // (cumulative delayMs, saturating).
    uint64_t deadline = 0;
    for (const AnimFrame& f : seq.frames) {
      if (deadline > UINT64_MAX - f.delayMs) {
        deadline = UINT64_MAX;
      } else {
        deadline += f.delayMs;
      }
      if (f.readyMs > deadline) {
        out.cadenceOk = false;
      }
    }

    // Validate payloads first: any truncated frame is a corrupt reject.
    for (const AnimFrame& f : seq.frames) {
      if (f.width == 0 || f.height == 0) {
        out.maxDelta = kHdrDelta + 1.0;
        out.upright = false;
        out.status = Status::kInvalidArg;
        return out;
      }
      const std::size_t expect = static_cast<std::size_t>(f.width) *
                                 static_cast<std::size_t>(f.height) * 4u;
      if (f.pixels.size() != expect) {
        out.maxDelta = kHdrDelta + 1.0;
        out.upright = false;
        out.status = Status::kInvalidArg;
        return out;
      }
    }

    // Decode each frame: fold EXIF orientation into the decode transform,
    // then convert P3 -> sRGB at upload (T2 matrix). Single pass, no extra
    // copy beyond the one upright output buffer.
    double max_delta = 0.0;
    bool upright = true;
    const bool ref_count_ok = ref.pixels.size() == seq.frames.size();
    if (!ref_count_ok) upright = false;
    for (std::size_t i = 0; i < seq.frames.size(); ++i) {
      const AnimFrame& f = seq.frames[i];
      uint16_t v = static_cast<uint16_t>(f.orientation);
      if (v < 1 || v > 8) v = 1;
      uint32_t dw = f.width;
      uint32_t dh = f.height;
      OrientedDims(f.width, f.height, v, &dw, &dh);
      if (dw != ref.width || dh != ref.height) upright = false;

      std::vector<uint8_t> decoded(static_cast<std::size_t>(dw) *
                                   static_cast<std::size_t>(dh) * 4u);
      for (uint32_t dy = 0; dy < dh; ++dy) {
        for (uint32_t dx = 0; dx < dw; ++dx) {
          uint32_t sx = 0, sy = 0;
          MapSource(f.width, f.height, v, dx, dy, &sx, &sy);
          const std::size_t si =
              (static_cast<std::size_t>(sy) * f.width + sx) * 4u;
          const std::size_t di =
              (static_cast<std::size_t>(dy) * dw + dx) * 4u;
          decoded[di] = f.pixels[si];
          decoded[di + 1] = f.pixels[si + 1];
          decoded[di + 2] = f.pixels[si + 2];
          decoded[di + 3] = f.pixels[si + 3];
        }
      }
      if (f.color == ColorSpace::kDisplayP3) {
        for (std::size_t k = 0; k + 4 <= decoded.size(); k += 4) {
          ConvertPixelP3ToSrgb(&decoded[k], &decoded[k + 1], &decoded[k + 2]);
        }
      }

      if (ref_count_ok) {
        const std::vector<uint8_t>& want = ref.pixels[i];
        if (want.size() != decoded.size()) {
          max_delta = 255.0;
          upright = false;
        } else {
          for (std::size_t k = 0; k < decoded.size(); ++k) {
            const double d = std::fabs(static_cast<double>(decoded[k]) -
                                       static_cast<double>(want[k]));
            if (d > max_delta) max_delta = d;
          }
        }
      } else {
        max_delta = 255.0;
      }
    }

    out.maxDelta = max_delta;
    out.upright = upright;
    out.status = Status::kOk;
    return out;
  } catch (...) {
    HdrCheck out;
    out.maxDelta = kHdrDelta + 1.0;
    out.cadenceOk = false;
    out.upright = false;
    out.status = Status::kInvalidArg;
    return out;
  }
}

}  // namespace media
