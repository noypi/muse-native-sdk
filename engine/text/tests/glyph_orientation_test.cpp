// T6a/T6b — Mirrored-glyph orientation pins (BUG-TEXT-008 T5 re-proof).
//
// Device fact (t5r3-cold.png / t5r3-warm8s.png / t5r3-swipe.png +
// t5r3-logcat.txt): the shaping chain now resolves — logcat shows
// `FontPathForFace id=65537 -> /system/fonts/Roboto-Regular.ttf` with
// `FT_New_Face ... opened`, gallery labels shape with drawn>0 — but every
// label renders horizontally mirrored ("m3-gallery" reads "mE-gallery").
// Root cause per re-proof triage: CPU-raster glyph-upload X-flip (bitmap
// columns uploaded in reverse order vs the shaped advance direction).
//
// These pins are hermetic and Real-only (P12.6): C++17, no framework, no
// device, no font files, no downloads, no config. They bind to the planned
// coder-owned orientation guard:
//
//   bool muse::text::GlyphUploadPreservesAdvanceDirection(
//       const uint8_t* src_rows, size_t width, size_t height, size_t pitch,
//       float advance_px) noexcept;
//
// which the CPU-raster upload path must satisfy: the leftmost source column
// (x=0, where the pen sits for +x LTR advance) must land at the leftmost
// uploaded texel, never mirrored. The header does not exist yet — the
// RED below is a missing-behavior compile failure, not a malformed test.
// The stub-backed model IS the contract suite on host (no Fake, Real-only).
//
// T6a (this file, check_upload_preserves_column_order): an asymmetric
//   wedge bitmap (opaque at x=0, transparent at x=w-1 — the shape that
//   distinguishes "3" from "E") must upload left-to-right.
// T6b (this file, check_ltr_advance_is_positive_x): the shaped advance for
//   an LTR label ("m3-gallery") is strictly +x, and the upload maps the pen
//   in the same direction (no sign flip at the raster boundary).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "muse/text/glyph_raster_orientation.h"

namespace {

// Asymmetric wedge: column x=0 fully opaque, column x=w-1 fully clear.
// An X-flipped upload turns this wedge into its mirror (the "3" -> "E"
// defect signature on t5r3).
static bool check_upload_preserves_column_order() {
  constexpr size_t kW = 8, kH = 8;
  std::vector<uint8_t> src(kW * kH, 0);
  for (size_t y = 0; y < kH; ++y) src[y * kW + 0] = 0xFF;
  // Positive LTR advance: pen moves +x, matching the shaped direction.
  constexpr float kAdvancePx = 8.0f;
  const bool ok = muse::text::GlyphUploadPreservesAdvanceDirection(
      src.data(), kW, kH, kW, kAdvancePx);
  return ok;
}

// LTR advance must be strictly positive-x: the raster pen for "m3-gallery"
// starts left and moves right. A zero or negative advance at the upload
// boundary mirrors or collapses the label.
static bool check_ltr_advance_is_positive_x() {
  constexpr size_t kW = 8, kH = 8;
  std::vector<uint8_t> src(kW * kH, 0xFF);
  constexpr float kAdvancePx = 8.0f;
  if (!(kAdvancePx > 0.0f)) return false;
  return muse::text::GlyphUploadPreservesAdvanceDirection(src.data(), kW, kH,
                                                         kW, kAdvancePx);
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_upload_preserves_column_order);
  RUN_CASE(check_ltr_advance_is_positive_x);
  if (failures > 0) {
    std::printf("glyph_orientation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("glyph_orientation_test: all passing\n");
  return 0;
}
