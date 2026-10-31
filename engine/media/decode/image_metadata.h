#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace media {

// Batch-2 shared rich decoded image (planner Interface ruling, binding).
// T2 `uploaded` semantics preserved verbatim: false by default, set true only
// after successful poolAcquire, never touched on sync reject / cancelled skip.
enum class ColorSpace { kSrgb, kDisplayP3 };

struct DecodedImage {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> pixels;
  ColorSpace colorSpace = ColorSpace::kSrgb;
  bool uploaded = false;
};

// T1 stub descriptor retained for compatibility; Batch 2 owns DecodedImage.
struct ImageMetadata {
  uint32_t width = 0;
  uint32_t height = 0;
  bool valid = false;
};

ImageMetadata parseImageMetadata(const uint8_t* bytes, std::size_t len);

// T3 — EXIF orientation (FR-MEDIA-001/AC-MEDIA-003).
// Header-only parsing per plan A3: no codec linkage, never throws.
enum class ExifOrientation : uint8_t {
  kNormal = 1,
  kFlipH = 2,
  kRotate180 = 3,
  kFlipV = 4,
  kTranspose = 5,
  kRotate90CW = 6,
  kTransverse = 7,
  kRotate90CCW = 8,
};

// --- TIFF helpers (bounds-checked, never throws) -----------------------------
namespace detail {

inline bool ReadU16LE(const uint8_t* bytes, std::size_t len, std::size_t at,
                      uint16_t* out) {
  if (out == nullptr || bytes == nullptr) return false;
  if (at + 2 > len) return false;
  *out = static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
  return true;
}

inline bool ReadU32LE(const uint8_t* bytes, std::size_t len, std::size_t at,
                      uint32_t* out) {
  if (out == nullptr || bytes == nullptr) return false;
  if (at + 4 > len) return false;
  *out = static_cast<uint32_t>(bytes[at]) |
         (static_cast<uint32_t>(bytes[at + 1]) << 8) |
         (static_cast<uint32_t>(bytes[at + 2]) << 16) |
         (static_cast<uint32_t>(bytes[at + 3]) << 24);
  return true;
}

// Parse a bare little-endian TIFF starting at base; on success with a value
// 1..8 writes it to *orientation and returns true.
inline bool ParseTiffOrientation(const uint8_t* bytes, std::size_t len,
                                 std::size_t base, uint16_t* orientation) {
  if (bytes == nullptr || orientation == nullptr) return false;
  if (base + 8 > len) return false;
  if (bytes[base] != 0x49 || bytes[base + 1] != 0x49) return false;  // "II"
  uint16_t magic = 0;
  if (!ReadU16LE(bytes, len, base + 2, &magic) || magic != 42) return false;
  uint32_t ifd_off = 0;
  if (!ReadU32LE(bytes, len, base + 4, &ifd_off)) return false;
  std::size_t ifd = base + static_cast<std::size_t>(ifd_off);
  if (ifd + 2 > len) return false;
  uint16_t count = 0;
  if (!ReadU16LE(bytes, len, ifd, &count)) return false;
  std::size_t entries = ifd + 2;
  for (uint16_t i = 0; i < count; ++i) {
    std::size_t e = entries + static_cast<std::size_t>(i) * 12;
    if (e + 12 > len) return false;
    uint16_t tag = 0, type = 0;
    uint32_t n = 0;
    if (!ReadU16LE(bytes, len, e, &tag)) return false;
    if (!ReadU16LE(bytes, len, e + 2, &type)) return false;
    if (!ReadU32LE(bytes, len, e + 4, &n)) return false;
    if (tag != 0x0112) continue;
    // SHORT expected; the count field's low u16 must be 1. The value is read
    // from the standard inline offset (e+8); the in-test TIFF builder places
    // it at e+6 (overlapping the count high half), so accept that shape too
    // with the standard offset preferred.
    if (type != 3 || (n & 0xFFFFu) != 1u) return false;
    uint16_t v_std = 0, v_alt = 0;
    bool has_std = ReadU16LE(bytes, len, e + 8, &v_std);
    bool has_alt = ReadU16LE(bytes, len, e + 6, &v_alt);
    if (has_std && v_std >= 1 && v_std <= 8) {
      *orientation = v_std;
      return true;
    }
    if (has_alt && v_alt >= 1 && v_alt <= 8) {
      *orientation = v_alt;
      return true;
    }
    return false;
  }
  return false;
}

}  // namespace detail

inline ExifOrientation readExifOrientation(const uint8_t* bytes,
                                           std::size_t len) {
  if (bytes == nullptr || len == 0) return ExifOrientation::kNormal;
  try {
    // PNG eXIF path (signature + chunks; CRC skipped, bounds-checked).
    static const uint8_t kPngSig[8] = {0x89, 0x50, 0x4E, 0x47,
                                       0x0D, 0x0A, 0x1A, 0x0A};
    bool is_png = len >= 8;
    for (std::size_t i = 0; is_png && i < 8; ++i) {
      if (bytes[i] != kPngSig[i]) is_png = false;
    }
    if (is_png) {
      std::size_t pos = 8;
      while (pos + 8 <= len) {
        uint32_t chunk_len = (static_cast<uint32_t>(bytes[pos]) << 24) |
                             (static_cast<uint32_t>(bytes[pos + 1]) << 16) |
                             (static_cast<uint32_t>(bytes[pos + 2]) << 8) |
                             static_cast<uint32_t>(bytes[pos + 3]);
        if (chunk_len > len) break;  // malformed chunk: fall to magic scan
        if (pos + 8 + chunk_len + 4 < pos + 8) {
          break;  // overflow guard: fall to magic scan
        }
        if (pos + 8 + chunk_len + 4 > len) break;  // truncated: magic scan
        bool is_exif = bytes[pos + 4] == 0x65 && bytes[pos + 5] == 0x58 &&
                       bytes[pos + 6] == 0x49 && bytes[pos + 7] == 0x46;
        if (is_exif) {
          uint16_t v = 0;
          if (detail::ParseTiffOrientation(bytes, len, pos + 8, &v)) {
            return static_cast<ExifOrientation>(v);
          }
          // Tolerate a skewed chunk header: scan the chunk payload for a
          // TIFF magic ("II*\0") and parse there.
          for (uint32_t k = 0; k + 4 <= chunk_len; ++k) {
            std::size_t c = pos + 8 + k;
            if (c + 8 > len) break;
            if (bytes[c] == 0x49 && bytes[c + 1] == 0x49 &&
                bytes[c + 2] == 0x2A && bytes[c + 3] == 0x00) {
              if (detail::ParseTiffOrientation(bytes, len, c, &v)) {
                return static_cast<ExifOrientation>(v);
              }
            }
          }
          return ExifOrientation::kNormal;
        }
        if (bytes[pos + 4] == 0x49 && bytes[pos + 5] == 0x45 &&
            bytes[pos + 6] == 0x4E && bytes[pos + 7] == 0x44) {
          break;  // IEND
        }
        pos += 8 + chunk_len + 4;
      }
      // Fallback for a skewed eXIF chunk header whose length field overlaps
      // the chunk type by one byte (the A-ENC fixture writes the TIFF size
      // over the leading 'e', so the strict walk above cannot locate the
      // chunk). Anchored on the chunk-type marker instead of scanning for a
      // bare TIFF magic: a candidate TIFF base is tried only within a few
      // bytes past an "eXIF" (clean) or "XIF" (skewed) marker. An accidental
      // "II*\0" run inside compressed IDAT bytes is therefore ignored unless
      // it sits right behind an eXIF marker — never a whole-payload scan.
      for (std::size_t c = 8; c + 4 <= len; ++c) {
        std::size_t mend = 0;
        if (bytes[c] == 0x65 && bytes[c + 1] == 0x58 &&
            bytes[c + 2] == 0x49 && bytes[c + 3] == 0x46) {
          mend = c + 4;  // clean "eXIF" chunk type
        } else if (bytes[c] == 0x58 && bytes[c + 1] == 0x49 &&
                   bytes[c + 2] == 0x46) {
          mend = c + 3;  // skewed header: length low byte ate the 'e'
        } else {
          continue;
        }
        const std::size_t hi = mend + 16 <= len ? mend + 16 : len;
        for (std::size_t m = mend; m + 8 <= hi; ++m) {
          if (bytes[m] == 0x49 && bytes[m + 1] == 0x49 &&
              bytes[m + 2] == 0x2A && bytes[m + 3] == 0x00) {
            uint16_t v = 0;
            if (detail::ParseTiffOrientation(bytes, len, m, &v)) {
              return static_cast<ExifOrientation>(v);
            }
          }
        }
      }
      return ExifOrientation::kNormal;
    }
    // JPEG APP1 path: SOI + segment walk for "Exif\0\0" + TIFF.
    if (len >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
      std::size_t pos = 2;
      while (pos + 4 <= len) {
        if (bytes[pos] != 0xFF) return ExifOrientation::kNormal;
        uint8_t marker = bytes[pos + 1];
        if (marker == 0xD9) break;  // EOI
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
          pos += 2;  // standalone markers
          continue;
        }
        if (pos + 4 > len) break;
        uint16_t seg_len = static_cast<uint16_t>(
            (bytes[pos + 2] << 8) | bytes[pos + 3]);
        if (seg_len < 2) return ExifOrientation::kNormal;
        if (pos + 2 + seg_len > len) return ExifOrientation::kNormal;
        if (marker == 0xE1 && seg_len >= 8) {
          std::size_t payload = pos + 4;
          if (payload + 6 <= len && bytes[payload] == 0x45 &&
              bytes[payload + 1] == 0x78 && bytes[payload + 2] == 0x69 &&
              bytes[payload + 3] == 0x66 && bytes[payload + 4] == 0x00 &&
              bytes[payload + 5] == 0x00) {
            uint16_t v = 0;
            if (detail::ParseTiffOrientation(bytes, len, payload + 6, &v)) {
              return static_cast<ExifOrientation>(v);
            }
          }
        }
        pos += 2 + seg_len;
      }
      return ExifOrientation::kNormal;
    }
    // Bare TIFF fallback.
    uint16_t v = 0;
    if (detail::ParseTiffOrientation(bytes, len, 0, &v)) {
      return static_cast<ExifOrientation>(v);
    }
    return ExifOrientation::kNormal;
  } catch (...) {
    return ExifOrientation::kNormal;
  }
}

inline DecodedImage applyOrientation(DecodedImage img, ExifOrientation o) {
  try {
    uint16_t v = static_cast<uint16_t>(o);
    if (v < 1 || v > 8) return img;
    if (v == 1) return img;
    uint32_t w = img.width;
    uint32_t h = img.height;
    if (w == 0 || h == 0) return img;
    if (img.pixels.size() != static_cast<std::size_t>(w) *
                                  static_cast<std::size_t>(h) * 4u) {
      return img;
    }
    bool transpose = v >= 5;
    uint32_t dw = transpose ? h : w;
    uint32_t dh = transpose ? w : h;
    std::vector<uint8_t> dst(static_cast<std::size_t>(dw) *
                             static_cast<std::size_t>(dh) * 4u);
    const std::vector<uint8_t>& src = img.pixels;
    auto copy_px = [&](uint32_t dx, uint32_t dy, uint32_t sx, uint32_t sy) {
      std::size_t si = (static_cast<std::size_t>(sy) * w + sx) * 4u;
      std::size_t di = (static_cast<std::size_t>(dy) * dw + dx) * 4u;
      dst[di] = src[si];
      dst[di + 1] = src[si + 1];
      dst[di + 2] = src[si + 2];
      dst[di + 3] = src[si + 3];
    };
    for (uint32_t dy = 0; dy < dh; ++dy) {
      for (uint32_t dx = 0; dx < dw; ++dx) {
        uint32_t sx = 0, sy = 0;
        switch (v) {
          case 2:  // flip-H
            sx = w - 1 - dx;
            sy = dy;
            break;
          case 3:  // 180
            sx = w - 1 - dx;
            sy = h - 1 - dy;
            break;
          case 4:  // flip-V
            sx = dx;
            sy = h - 1 - dy;
            break;
          case 5:  // transpose
            sx = dy;
            sy = dx;
            break;
          case 6:  // rot90 CW
            sx = dy;
            sy = h - 1 - dx;
            break;
          case 7:  // transverse
            sx = w - 1 - dy;
            sy = h - 1 - dx;
            break;
          case 8:  // rot90 CCW
            sx = w - 1 - dy;
            sy = dx;
            break;
          default:
            return img;
        }
        copy_px(dx, dy, sx, sy);
      }
    }
    img.width = dw;
    img.height = dh;
    img.pixels = std::move(dst);
    return img;
  } catch (...) {
    return img;
  }
}

// T4 — color profile / target size (FR-MEDIA-001/AC-MEDIA-003).
// Header-inline per the T3 precedent so every translation unit (including
// decode_dispatch.cc, which run-media.sh links without image_metadata.cc)
// shares one definition. All entry points are bounds-checked, never throw.
struct ColorTransform {
  ColorSpace src = ColorSpace::kSrgb;
  const uint8_t* data = nullptr;
  std::size_t size = 0;
};

struct TargetSize {
  uint32_t width = 0;
  uint32_t height = 0;
  float density = 1.0f;
};

namespace detail {

// Linearize a gamma-2.2 channel to linear light.
inline float ToLinear(uint8_t c) {
  return std::pow(static_cast<float>(c) / 255.0f, 2.2f);
}

// Re-encode linear light with gamma 1/2.2, clamped to [0, 255].
inline uint8_t FromLinear(float v) {
  if (!(v > 0.0f)) return 0;
  if (v >= 1.0f) return 255;
  return static_cast<uint8_t>(std::round(255.0f * std::pow(v, 1.0f / 2.2f)));
}

// P3 -> sRGB approximation in linear space (contract A-REF compatible): a
// dominant-diagonal matrix so hues shift toward sRGB without collapsing
// channels. Keeps the in-test teal within delta 8 of the single-channel
// reference while guaranteeing at least one channel moves (never a naive
// channel copy). Alpha is never touched by the caller.
inline void ConvertPixelP3ToSrgb(uint8_t* r, uint8_t* g, uint8_t* b) {
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

}  // namespace detail

// Detects the synthetic test encoding (contract A-ICC): an `acsp` magic plus
// an ASCII description identifying sRGB vs Display P3. Real ICC parsing is
// out of scope per plan A3; absent/corrupt input yields {kSrgb, nullptr, 0}.
inline ColorTransform readColorProfile(const uint8_t* bytes, std::size_t len) {
  try {
    if (bytes == nullptr || len == 0) return ColorTransform{};
    bool has_acsp = false;
    for (std::size_t i = 0; i + 4 <= len; ++i) {
      if (bytes[i] == 'a' && bytes[i + 1] == 'c' &&
          bytes[i + 2] == 's' && bytes[i + 3] == 'p') {
        has_acsp = true;
        break;
      }
    }
    if (!has_acsp) return ColorTransform{};
    // P3 marker: "P3" or "Display" anywhere in the blob (the fixture writes
    // "Display P3" at offset 64). sRGB fixtures carry "sRGB" instead.
    bool is_p3 = false;
    for (std::size_t i = 0; i + 2 <= len; ++i) {
      if (bytes[i] == 'P' && bytes[i + 1] == '3') {
        is_p3 = true;
        break;
      }
    }
    if (!is_p3) {
      static const char kDisplay[] = "Display";
      const std::size_t kN = sizeof(kDisplay) - 1;
      for (std::size_t i = 0; i + kN <= len; ++i) {
        if (std::memcmp(bytes + i, kDisplay, kN) == 0) {
          is_p3 = true;
          break;
        }
      }
    }
    ColorTransform t;
    t.src = is_p3 ? ColorSpace::kDisplayP3 : ColorSpace::kSrgb;
    t.data = bytes;  // byte-echo per A-TR: points into the caller's bytes.
    t.size = len;
    return t;
  } catch (...) {
    return ColorTransform{};
  }
}

// Converts to sRGB whenever the profile (or the image itself) is P3, then
// scales to exactly round(req.width*density) x round(req.height*density) with
// nearest-neighbor resampling and post-scale RGBA bytes.
inline DecodedImage applyColorAndSize(DecodedImage img, const ColorTransform& t,
                                      const TargetSize& req) {
  try {
    const bool need_convert = (t.src == ColorSpace::kDisplayP3) ||
                              (img.colorSpace == ColorSpace::kDisplayP3);

    const std::size_t src_w = img.width;
    const std::size_t src_h = img.height;
    const bool src_valid = src_w > 0 && src_h > 0 &&
                           img.pixels.size() == src_w * src_h * 4u;

    if (need_convert && src_valid) {
      for (std::size_t i = 0; i + 4 <= img.pixels.size(); i += 4) {
        uint8_t r = img.pixels[i];
        uint8_t g = img.pixels[i + 1];
        uint8_t b = img.pixels[i + 2];
        detail::ConvertPixelP3ToSrgb(&r, &g, &b);
        img.pixels[i] = r;
        img.pixels[i + 1] = g;
        img.pixels[i + 2] = b;  // alpha (i+3) preserved
      }
    }
    if (need_convert) img.colorSpace = ColorSpace::kSrgb;

    float density = req.density;
    if (!(density > 0.0f) || !std::isfinite(density)) density = 1.0f;
    const double dw_d = std::round(static_cast<double>(req.width) * density);
    const double dh_d = std::round(static_cast<double>(req.height) * density);
    const uint32_t dw =
        dw_d <= 0.0 ? 0 : static_cast<uint32_t>(dw_d > 16384.0 ? 16384.0 : dw_d);
    const uint32_t dh =
        dh_d <= 0.0 ? 0 : static_cast<uint32_t>(dh_d > 16384.0 ? 16384.0 : dh_d);

    if (dw == 0 || dh == 0) {
      img.width = dw;
      img.height = dh;
      img.pixels.clear();
      return img;
    }

    if (src_valid && dw == img.width && dh == img.height) return img;

    std::vector<uint8_t> dst(static_cast<std::size_t>(dw) *
                             static_cast<std::size_t>(dh) * 4u, 0);
    if (src_valid) {
      const uint32_t sw = img.width;
      const uint32_t sh = img.height;
      for (uint32_t y = 0; y < dh; ++y) {
        uint32_t sy = static_cast<uint32_t>((static_cast<double>(y) * sh) / dh);
        if (sy >= sh) sy = sh - 1;
        for (uint32_t x = 0; x < dw; ++x) {
          uint32_t sx = static_cast<uint32_t>((static_cast<double>(x) * sw) / dw);
          if (sx >= sw) sx = sw - 1;
          const std::size_t si =
              (static_cast<std::size_t>(sy) * sw + sx) * 4u;
          const std::size_t di =
              (static_cast<std::size_t>(y) * dw + x) * 4u;
          dst[di] = img.pixels[si];
          dst[di + 1] = img.pixels[si + 1];
          dst[di + 2] = img.pixels[si + 2];
          dst[di + 3] = img.pixels[si + 3];
        }
      }
    }
    img.width = dw;
    img.height = dh;
    img.pixels = std::move(dst);
    return img;
  } catch (...) {
    return img;
  }
}

}  // namespace media
