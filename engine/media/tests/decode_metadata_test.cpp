// Batch 2 — EXIF / color-profile / target-size correction (FR-MEDIA-001/AC-MEDIA-003, TC-MEDIA-003).
// Binds ONLY to the T3/T4 `Interface:` lines in plan.md; never to implementation.
//
// Hermetic: C++17 + the new engine/media/decode/image_metadata.h only; no
// network, no env/config reads, no filesystem, no live services. All fixtures
// are inline byte arrays / in-test pixel buffers.
//
// Expected state: RED — engine/media/decode/image_metadata.h declares only the
// T1 ImageMetadata stub, so this file fails to compile until the coder
// implements the T3/T4 shapes (missing ExifOrientation/readExifOrientation/
// applyOrientation/ColorTransform/readColorProfile/applyColorAndSize).
//
// Contract assumptions (see unit-tests/T3.md, unit-tests/T4.md):
//   A-NS:  everything in namespace `media`; header `engine/media/decode/image_metadata.h`.
//   A-IMG: `media::DecodedImage` is default-constructible with at least
//          `uint32_t width/height`, `std::vector<uint8_t> pixels` (RGBA8
//          row-major, size == w*h*4), `ColorSpace colorSpace`, and
//          `bool uploaded == false`.
//   A-ORI: `enum class ExifOrientation : uint8_t { kNormal=1..kOrientation8=8 }`
//          with enumerators for all 8 EXIF values.
//   A-ENC: `readExifOrientation` parses at least (a) bare little-endian TIFF
//          (II*\0 + IFD0 with SHORT tag 0x0112), (b) JPEG APP1-wrapped TIFF
//          ("Exif\0\0" + TIFF), (c) minimal PNG with an eXIF chunk.
//   A-CS:  `enum class ColorSpace { kSrgb, kDisplayP3 }`.
//   A-TR:  `struct ColorTransform { ColorSpace src; const uint8_t* data; size_t size; }`
//          (`{kSrgb,nullptr,0}` when absent); `struct TargetSize { uint32_t
//          width, height; float density = 1.0f; }` with output dims
//          round(width*density) x round(height*density).
//   A-REF: P3<->sRGB reference uses the in-test linear matrix below; per-channel delta <= 8.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/media/decode/image_metadata.h"

// --- tiny harness (matches worker_pool_test.cpp convention) -----------------
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

// --- fixture builders --------------------------------------------------------
// Minimal little-endian TIFF with a single IFD0 SHORT entry for tag 0x0112
// (Orientation) = `orientation`. Layout:
//   0-1: "II"  2-3: 42  4-7: IFD offset 8
//   8-9: 1 entry  10-21: tag/type/count/value  22-25: next-IFD 0
static std::vector<uint8_t> MakeTiffOrientation(uint16_t orientation) {
  std::vector<uint8_t> b = {0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,
                            0x01, 0x00, 0x12, 0x01, 0x03, 0x00, 0x01, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00};
  b[16] = static_cast<uint8_t>(orientation & 0xFF);
  b[17] = static_cast<uint8_t>((orientation >> 8) & 0xFF);
  return b;
}

// Minimal JPEG wrapping the TIFF in APP1: SOI + APP1("Exif\0\0" + tiff) + EOI.
static std::vector<uint8_t> MakeJpegApp1(const std::vector<uint8_t>& tiff) {
  std::vector<uint8_t> b = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x00, 0x45, 0x78,
                            0x69, 0x66, 0x00, 0x00};
  b.insert(b.end(), tiff.begin(), tiff.end());
  const uint16_t seg_len = static_cast<uint16_t>(b.size() - 4 + 2);
  b[4] = static_cast<uint8_t>((seg_len >> 8) & 0xFF);
  b[5] = static_cast<uint8_t>(seg_len & 0xFF);
  b.push_back(0xFF);
  b.push_back(0xD9);
  return b;
}

// Minimal PNG (signature + IHDR + eXIF chunk holding the TIFF + IEND), CRCs zeroed.
static std::vector<uint8_t> MakePngExif(const std::vector<uint8_t>& tiff) {
  std::vector<uint8_t> b = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
                            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
                            0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02,
                            0x08, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x65, 0x58, 0x49, 0x46};
  const uint32_t n = static_cast<uint32_t>(tiff.size());
  b[33] = static_cast<uint8_t>((n >> 24) & 0xFF);
  b[34] = static_cast<uint8_t>((n >> 16) & 0xFF);
  b[35] = static_cast<uint8_t>((n >> 8) & 0xFF);
  b[36] = static_cast<uint8_t>(n & 0xFF);
  b.insert(b.end(), tiff.begin(), tiff.end());
  b.insert(b.end(), {0x00, 0x00, 0x00, 0x00});
  b.insert(b.end(),
           {0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
            0x00, 0x00, 0x00, 0x00});
  return b;
}

// 3x2 RGBA fixture with six distinct pixels (row-major):
//   row0: R G B | row1: C M Y  (C=cyan, M=magenta, Y=yellow)
static media::DecodedImage Make3x2() {
  media::DecodedImage img;
  img.width = 3;
  img.height = 2;
  img.colorSpace = media::ColorSpace::kSrgb;
  img.pixels = {255, 0,   0,   255,  // R
                0,   255, 0,   255,  // G
                0,   0,   255, 255,  // B
                0,   255, 255, 255,  // C
                255, 0,   255, 255,  // M
                255, 255, 0,   255};  // Y
  return img;
}

static bool PixelsEqual(const std::vector<uint8_t>& a,
                        const std::vector<uint8_t>& b) {
  return a == b;
}

// In-test P3->sRGB reference (A-REF): linearize with gamma 2.2, apply the
// P3-to-sRGB matrix, re-encode. Coder implements its own conversion; values
// must land within per-channel delta 8 of this reference.
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

// --- T3 cases -----------------------------------------------------------------
static void check_exif_all_orientations_tiff() {
  for (uint16_t v = 1; v <= 8; ++v) {
    const std::vector<uint8_t> tiff = MakeTiffOrientation(v);
    media::ExifOrientation o = media::readExifOrientation(tiff.data(), tiff.size());
    char name[64];
    std::snprintf(name, sizeof(name), "tiff orientation %u detected", v);
    CHECK(name, static_cast<uint16_t>(o) == v);
  }
}

static void check_exif_all_orientations_jpeg_app1() {
  for (uint16_t v = 1; v <= 8; ++v) {
    const std::vector<uint8_t> jpeg = MakeJpegApp1(MakeTiffOrientation(v));
    media::ExifOrientation o = media::readExifOrientation(jpeg.data(), jpeg.size());
    char name[64];
    std::snprintf(name, sizeof(name), "jpeg app1 orientation %u detected", v);
    CHECK(name, static_cast<uint16_t>(o) == v);
  }
}

static void check_exif_png_exif_chunk() {
  const std::vector<uint8_t> png = MakePngExif(MakeTiffOrientation(6));
  media::ExifOrientation o = media::readExifOrientation(png.data(), png.size());
  CHECK("png eXIF orientation 6 detected", static_cast<uint16_t>(o) == 6);
}

// MINOR PNG fallback regression (review 2026-10-31): an IDAT payload that
// happens to contain a TIFF magic ("II*\0") + plausible IFD0 with tag
// 0x0112, but with NO eXIF chunk anywhere, must yield kNormal — the fallback
// is anchored on eXIF chunk-type markers and never scans the whole payload.
static void check_exif_png_idat_magic_ignored() {
  const std::vector<uint8_t> tiff = MakeTiffOrientation(6);
  std::vector<uint8_t> idat = {0x00, 0x01, 0x02, 0x03};
  idat.insert(idat.end(), tiff.begin(), tiff.end());
  idat.insert(idat.end(), {0x04, 0x05});
  // PNG: signature + IHDR + IDAT (holding the TIFF look-alike) + IEND.
  // No eXIF chunk present.
  std::vector<uint8_t> b = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
                            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
                            0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02,
                            0x08, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54};
  const uint32_t n = static_cast<uint32_t>(idat.size());
  b[33] = static_cast<uint8_t>((n >> 24) & 0xFF);
  b[34] = static_cast<uint8_t>((n >> 16) & 0xFF);
  b[35] = static_cast<uint8_t>((n >> 8) & 0xFF);
  b[36] = static_cast<uint8_t>(n & 0xFF);
  b.insert(b.end(), idat.begin(), idat.end());
  b.insert(b.end(), {0x00, 0x00, 0x00, 0x00});
  b.insert(b.end(),
           {0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
            0x00, 0x00, 0x00, 0x00});
  bool threw = false;
  media::ExifOrientation o = media::ExifOrientation::kNormal;
  try {
    o = media::readExifOrientation(b.data(), b.size());
  } catch (...) {
    threw = true;
  }
  CHECK("png IDAT magic without eXIF never throws", !threw);
  CHECK("png IDAT magic without eXIF yields kNormal",
        o == media::ExifOrientation::kNormal);
}

static void check_exif_missing_and_corrupt_identity() {
  static const uint8_t kNoExif[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10,
                                    0x4A, 0x46, 0x49, 0x46, 0x00, 0x01};
  CHECK("missing EXIF yields kNormal",
        media::readExifOrientation(kNoExif, sizeof(kNoExif)) ==
            media::ExifOrientation::kNormal);
  static const uint8_t kGarbage[] = {0x00, 0xFF, 0x41, 0x42, 0x7F,
                                     0x11, 0x22, 0x33};
  bool threw = false;
  media::ExifOrientation o = media::ExifOrientation::kNormal;
  try {
    o = media::readExifOrientation(kGarbage, sizeof(kGarbage));
  } catch (...) {
    threw = true;
  }
  CHECK("corrupt EXIF never throws", !threw);
  CHECK("corrupt EXIF yields kNormal", o == media::ExifOrientation::kNormal);
  bool threw_null = false;
  media::ExifOrientation on = media::ExifOrientation::kNormal;
  try {
    on = media::readExifOrientation(nullptr, 0);
  } catch (...) {
    threw_null = true;
  }
  CHECK("null/empty input never throws", !threw_null);
  CHECK("null/empty input yields kNormal",
        on == media::ExifOrientation::kNormal);
}

static void check_orientation_identity_normal() {
  media::DecodedImage img = Make3x2();
  const std::vector<uint8_t> before = img.pixels;
  media::DecodedImage out =
      media::applyOrientation(img, media::ExifOrientation::kNormal);
  CHECK("kNormal keeps width", out.width == 3);
  CHECK("kNormal keeps height", out.height == 2);
  CHECK("kNormal keeps pixels", PixelsEqual(out.pixels, before));
}

// Orientations 2-8 remap upright with correct (possibly transposed) dims.
// Expected pixels for the 3x2 R G B / C M Y fixture (upright = orientation 1):
//   1: R G B / C M Y        3x2   (identity)
//   2: G R B? no — flip-H:  B G R / Y M C   3x2
//   3: 180:                 Y M C / B G R   3x2
//   4: flip-V:              C M Y / R G B   3x2
//   5: transpose:            R C / G M / B Y  2x3
//   6: rot90 CW:             C R / M G / Y B  2x3
//   7: transverse:           Y B / M G / C R  2x3
//   8: rot90 CCW:            B Y / G M / R C  2x3
static void check_orientation_remaps() {
  struct Expect {
    uint16_t ori;
    uint32_t w;
    uint32_t h;
    std::vector<uint8_t> px;
  };
  const std::vector<uint8_t> R = {255, 0, 0, 255}, G = {0, 255, 0, 255},
                             B = {0, 0, 255, 255}, C = {0, 255, 255, 255},
                             M = {255, 0, 255, 255}, Y = {255, 255, 0, 255};
  auto flat = [](std::vector<std::vector<uint8_t>> ps) {
    std::vector<uint8_t> out;
    for (auto& p : ps) out.insert(out.end(), p.begin(), p.end());
    return out;
  };
  const std::vector<Expect> kCases = {
      {2, 3, 2, flat({B, G, R, Y, M, C})},
      {3, 3, 2, flat({Y, M, C, B, G, R})},
      {4, 3, 2, flat({C, M, Y, R, G, B})},
      {5, 2, 3, flat({R, C, G, M, B, Y})},
      {6, 2, 3, flat({C, R, M, G, Y, B})},
      {7, 2, 3, flat({Y, B, M, G, C, R})},
      {8, 2, 3, flat({B, Y, G, M, R, C})},
  };
  for (const Expect& e : kCases) {
    media::DecodedImage out = media::applyOrientation(
        Make3x2(), static_cast<media::ExifOrientation>(e.ori));
    char nd[64], pd[64];
    std::snprintf(nd, sizeof(nd), "orientation %u dims %ux%u", e.ori, e.w, e.h);
    std::snprintf(pd, sizeof(pd), "orientation %u upright pixels", e.ori);
    CHECK(nd, out.width == e.w && out.height == e.h);
    CHECK(pd, PixelsEqual(out.pixels, e.px));
  }
}

static void check_orientation_never_throws() {
  bool threw = false;
  try {
    media::DecodedImage empty;
    media::DecodedImage out =
        media::applyOrientation(empty, media::ExifOrientation::kNormal);
    (void)out;
    media::DecodedImage out2 = media::applyOrientation(
        empty, static_cast<media::ExifOrientation>(99));
    (void)out2;
  } catch (...) {
    threw = true;
  }
  CHECK("applyOrientation never throws (empty/unknown)", !threw);
}

// --- T4 cases -------------------------------------------------------------------
static void check_color_missing_profile_defaults_srgb() {
  static const uint8_t kNoProfile[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10,
                                       0x4A, 0x46, 0x49, 0x46, 0x00, 0x01};
  bool threw = false;
  media::ColorTransform t{media::ColorSpace::kDisplayP3, nullptr, 999};
  try {
    t = media::readColorProfile(kNoProfile, sizeof(kNoProfile));
  } catch (...) {
    threw = true;
  }
  CHECK("missing profile never throws", !threw);
  CHECK("missing profile defaults sRGB", t.src == media::ColorSpace::kSrgb);
  CHECK("missing profile null data", t.data == nullptr && t.size == 0);

  bool threw_null = false;
  media::ColorTransform tn{media::ColorSpace::kDisplayP3, nullptr, 999};
  try {
    tn = media::readColorProfile(nullptr, 0);
  } catch (...) {
    threw_null = true;
  }
  CHECK("null profile input never throws", !threw_null);
  CHECK("null profile input defaults sRGB",
        tn.src == media::ColorSpace::kSrgb && tn.data == nullptr &&
            tn.size == 0);

  static const uint8_t kGarbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00};
  bool threw_g = false;
  media::ColorTransform tg{media::ColorSpace::kDisplayP3, nullptr, 999};
  try {
    tg = media::readColorProfile(kGarbage, sizeof(kGarbage));
  } catch (...) {
    threw_g = true;
  }
  CHECK("corrupt profile never throws", !threw_g);
  CHECK("corrupt profile defaults sRGB", tg.src == media::ColorSpace::kSrgb);
}

static void check_color_srgb_and_p3_profiles() {
  // Minimal synthetic ICC blobs (A-ICC): 128-byte header with 'acsp' magic at
  // offset 36 and a description string identifying the space.
  auto make_icc = [](const char* desc) {
    std::vector<uint8_t> b(140, 0);
    b[36] = 'a';
    b[37] = 'c';
    b[38] = 's';
    b[39] = 'p';
    for (int i = 0; desc[i] && i < 32; ++i) b[64 + i] = static_cast<uint8_t>(desc[i]);
    return b;
  };
  const std::vector<uint8_t> srgb = make_icc("sRGB IEC61966-2.1");
  const std::vector<uint8_t> p3 = make_icc("Display P3");
  media::ColorTransform ts = media::readColorProfile(srgb.data(), srgb.size());
  CHECK("sRGB ICC detected", ts.src == media::ColorSpace::kSrgb);
  CHECK("sRGB ICC echoes bytes", ts.data == srgb.data() && ts.size == srgb.size());
  media::ColorTransform tp = media::readColorProfile(p3.data(), p3.size());
  CHECK("P3 ICC detected", tp.src == media::ColorSpace::kDisplayP3);
  CHECK("P3 ICC echoes bytes", tp.data == p3.data() && tp.size == p3.size());
}

static void check_color_convert_within_delta() {
  // P3 teal pixel must convert toward sRGB per A-REF (never a naive copy).
  media::DecodedImage img;
  img.width = 1;
  img.height = 1;
  img.colorSpace = media::ColorSpace::kDisplayP3;
  img.pixels = {64, 180, 200, 255};
  media::ColorTransform t;
  t.src = media::ColorSpace::kDisplayP3;
  t.data = img.pixels.data();
  t.size = img.pixels.size();
  media::TargetSize req{1, 1, 1.0f};
  media::DecodedImage out = media::applyColorAndSize(img, t, req);
  CHECK("convert keeps 1x1 dims", out.width == 1 && out.height == 1);
  CHECK("convert lands in sRGB space",
        out.colorSpace == media::ColorSpace::kSrgb);
  bool within = true;
  const uint8_t expect[3] = {RefP3ToSrgb(64, 0), RefP3ToSrgb(180, 1),
                             RefP3ToSrgb(200, 2)};
  for (int c = 0; c < 3; ++c) {
    const int got = out.pixels[c];
    if (std::abs(got - expect[c]) > 8) within = false;
  }
  CHECK("P3->sRGB within per-channel delta 8", within);
  CHECK("alpha preserved", out.pixels[3] == 255);
  // Not a naive channel copy: at least one channel must move.
  CHECK("conversion is not a naive copy",
        out.pixels[0] != 64 || out.pixels[1] != 180 || out.pixels[2] != 200);
}

static void check_target_size_density_exact() {
  media::DecodedImage img = Make3x2();  // 3x2 sRGB
  media::ColorTransform t{media::ColorSpace::kSrgb, nullptr, 0};
  media::TargetSize d1{6, 4, 1.0f};
  media::DecodedImage o1 = media::applyColorAndSize(img, t, d1);
  CHECK("density 1 exact dims", o1.width == 6 && o1.height == 4);
  CHECK("density 1 pixel bytes match dims",
        o1.pixels.size() == 6u * 4u * 4u);
  media::TargetSize d2{3, 2, 2.0f};
  media::DecodedImage o2 = media::applyColorAndSize(img, t, d2);
  CHECK("density 2 exact dims", o2.width == 6 && o2.height == 4);
  media::TargetSize d3{2, 2, 1.5f};
  media::DecodedImage o3 = media::applyColorAndSize(img, t, d3);
  CHECK("density 1.5 rounds exact dims", o3.width == 3 && o3.height == 3);
  media::TargetSize down{1, 1, 1.0f};
  media::DecodedImage o4 = media::applyColorAndSize(img, t, down);
  CHECK("downscale exact dims", o4.width == 1 && o4.height == 1);
  CHECK("downscale single pixel RGBA", o4.pixels.size() == 4u);
}

static void check_color_and_size_never_throws() {
  bool threw = false;
  try {
    media::DecodedImage empty;
    media::ColorTransform t{media::ColorSpace::kSrgb, nullptr, 0};
    media::TargetSize req{0, 0, 1.0f};
    media::DecodedImage out = media::applyColorAndSize(empty, t, req);
    (void)out;
  } catch (...) {
    threw = true;
  }
  CHECK("applyColorAndSize never throws on empty", !threw);
}

int main() {
  check_exif_all_orientations_tiff();
  check_exif_all_orientations_jpeg_app1();
  check_exif_png_exif_chunk();
  check_exif_png_idat_magic_ignored();
  check_exif_missing_and_corrupt_identity();
  check_orientation_identity_normal();
  check_orientation_remaps();
  check_orientation_never_throws();
  check_color_missing_profile_defaults_srgb();
  check_color_srgb_and_p3_profiles();
  check_color_convert_within_delta();
  check_target_size_density_exact();
  check_color_and_size_never_throws();
  if (s_failures == 0) {
    std::printf("ALL GREEN decode_metadata_test\n");
    return 0;
  }
  std::printf("%d FAILURES decode_metadata_test\n", s_failures);
  return 1;
}
