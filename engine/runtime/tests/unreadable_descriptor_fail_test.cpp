// Batch 2 RED pin — TC-RUNTIME-053 / AC-RUNTIME-044 (FR-RUNTIME-014).
//
// An unreadable descriptor on the local gallery path must fail the proof
// gate (proofGate=fail); no golden re-baselined while red, and no fallback
// screen renders in its place.
//
// Binds ONLY to settled public headers (`runtime/gallery_present.h`,
// `runtime/renderer.h`, `runtime/vulkan_baseline.h`); drives the existing
// `ParseGalleryRows`, `PresentGallery`, and `RasterGalleryPage` entries.
// Mapping (see unit-tests/T3.md): proofGate=fail == returns false with a
// non-blank diagnostic and zero submitted cmds; "no fallback screen" ==
// `RasterGalleryPage` on the resulting empty rows leaves the pixel buffer
// untouched instead of painting the "M3 GALLERY: NO ROWS" diagnostic screen.
//
// Hermetic: C++17 + committed headers only; in-memory buffers, fake
// swapchain, no GPU/window/network/env/filesystem/services.
//
// Expected RED (pre-T4): `RasterGalleryPage` paints the NO-ROWS diagnostic
// screen over the caller buffer, so `check_no_fallback_screen_painted`
// FAILS until the coder makes the empty-rows path fail closed without
// painting. The parse/present pins already hold (documented below).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "muse/text/font_discovery.h"
#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

namespace {

runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

// Bad magic: no MUI1 envelope at all.
const char kGarbage[] = "not-a-mui1-asset-at-all";

// Truncated envelope: valid magic/version, declared length beyond the buffer.
std::vector<std::uint8_t> MakeTruncated() {
  std::vector<std::uint8_t> out = {'M', 'U', 'I', '1', 1, 0, 0, 0,
                                   200, 0,   0,   0};
  out.push_back('{');
  return out;
}

bool check_garbage_parse_fails_with_diagnostic() {
  std::string error;
  std::vector<runtime::GalleryRow> rows = runtime::ParseGalleryRows(
      kGarbage, sizeof(kGarbage) - 1, &error);
  if (!rows.empty()) {
    std::printf("  garbage parsed into %zu rows\n", rows.size());
    return false;
  }
  if (error.empty()) {
    std::printf("  garbage left no diagnostic\n");
    return false;
  }
  return true;
}

bool check_garbage_present_fails_without_cmds() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(kGarbage, sizeof(kGarbage) - 1, renderer,
                                    target, &error);
  if (ok) {
    std::printf("  garbage presented as pass\n");
    return false;
  }
  if (error.empty()) {
    std::printf("  garbage present left no diagnostic\n");
    return false;
  }
  if (!target.captured.empty()) {
    std::printf("  garbage submitted %zu cmds\n", target.captured.size());
    return false;
  }
  return true;
}

bool check_truncated_envelope_fails() {
  auto asset = MakeTruncated();
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  bool ok = runtime::PresentGallery(asset.data(), asset.size(), renderer,
                                    target, &error);
  if (ok) {
    std::printf("  truncated envelope presented as pass\n");
    return false;
  }
  if (!target.captured.empty()) {
    std::printf("  truncated envelope submitted %zu cmds\n",
                target.captured.size());
    return false;
  }
  return true;
}

bool check_no_fallback_screen_painted() {
  // Empty rows are exactly what an unreadable descriptor yields; the pixel
  // buffer must come back untouched (fail-closed, no fallback screen).
  std::vector<runtime::GalleryRow> rows;
  constexpr int kW = 64;
  constexpr int kH = 64;
  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kW * kH), 0u);
  std::string error = "Showcase children unreadable";
  runtime::RasterGalleryPage(rows, 0, pixels.data(), kW, kW, kH, &error);
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    if (pixels[i] != 0u) {
      std::printf("  fallback screen painted pixel %zu (0x%08X)\n", i,
                  pixels[i]);
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  // Hermetic shaping lane (BUG-RUNTIME-015 Gap 1, option (a)): uniform
  // with the other Batch-2 TUs — unopenable in-memory bytes route any
  // shaping through the deterministic salted-fallback stub (test-only, no
  // downloads, no system fonts, no device). This TU shapes nothing today
  // (parse/present fail before shaping; the empty-rows raster never
  // shapes), so this is a no-op lane pin, not a behavior change.
  const std::uint8_t kNoFont[] = {0x4E, 0x4F, 0x54, 0x41, 0x46,
                                  0x4F, 0x4E, 0x54, 0x00, 0xFF};
  muse::text::RegisterTestOnlyMemoryFont(kNoFont, sizeof(kNoFont));
  int failures = 0;
  RUN_CASE(check_garbage_parse_fails_with_diagnostic);
  RUN_CASE(check_garbage_present_fails_without_cmds);
  RUN_CASE(check_truncated_envelope_fails);
  RUN_CASE(check_no_fallback_screen_painted);
  if (failures > 0) {
    std::printf("unreadable_descriptor_fail_test: %d failing\n", failures);
    return 1;
  }
  std::printf("unreadable_descriptor_fail_test: all passing\n");
  return 0;
}
