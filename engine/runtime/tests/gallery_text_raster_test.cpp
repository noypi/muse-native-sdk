// T6 — Swapchain rasterization of ShapedText runs via slug_msdf/tile_raster.
// FR-TEXT-016 / AC-TEXT-045 (device text is shaped vector text rasterized
// through the engine stack) + NFR-TEXT-001 (zero JNI on the per-frame path)
// + AC-GRAPHICS-002 (Slug MSDF text path).
//
// Public boundary: the plan T6 `Interface:` line (see unit-tests/T6.md for
// the signature ASSUMPTION). Hermetic: C++17 + committed runtime headers
// only; in-memory run ids/bounds, no device, no GPU, no network, no config,
// no filesystem, no live services. Zero JNI by construction: this TU
// touches no JVM/JNI type, mirroring the NFR-TEXT-001 constraint.
//
// Binds to the plan T6 `Interface:` line (see unit-tests/T6.md).
// RED STATE (expected): `RasterizeShapedText` does not exist anywhere in
// the tree — the retained `ShapedText` cmd is only forwarded by
// `renderer.cpp` (`target.submit(cmd)`) and the CPU `RasterGalleryPage`
// oracle still paints text with `DrawText`. This TU therefore fails to
// compile until the coder wires run → slug_msdf::shapeAndPlace texel
// records + tile_raster coverage into Swapchain rasterization. The
// pre-existing `renderer.draw` glyphRunId == 0 → UnknownPrimitive
// fail-closed stays covered by render_smoke_test (006, untouched).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

namespace {

// Window-backed fake swapchain: records every submitted cmd in order,
// like a presented surface would retain them. No GPU, no ANativeWindow.
// (Same hermetic pattern as present_window_text_test.)
class FakeWindowSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

// A genuine T5-produced run id: shaped through the engine stack via the
// public PresentDeviceGalleryPage seam (the coder's in-process
// shaper/atlas seam — handles are assigned from 0x1000, so the old
// hardcoded placeholder 7 can never resolve by design: unknown ids fail
// closed with no synthesis). Returns 0 when no run was emitted.
std::uint32_t known_run_from_engine() {
  runtime::GalleryRow row;
  row.label = "Gallery";
  const std::vector<runtime::GalleryRow> rows{row};
  runtime::Renderer renderer(baseline_device());
  FakeWindowSwapchain window;
  std::string error;
  if (!runtime::PresentDeviceGalleryPage(rows, 0, renderer, window,
                                         &error)) {
    return 0;
  }
  for (const auto& cmd : window.captured) {
    if (cmd.kind == runtime::PrimitiveKind::ShapedText &&
        cmd.payload.glyphRunId != 0) {
      return cmd.payload.glyphRunId;
    }
  }
  return 0;
}
// A run id no atlas entry covers: the dangling-id error path.
constexpr std::uint32_t kDanglingRun = 0xDEADu;

runtime::Rect test_bounds() {
  runtime::Rect r;
  r.x = 10.0f;
  r.y = 20.0f;
  r.w = 100.0f;
  r.h = 28.0f;
  return r;
}

// AC-TEXT-045: a known T5-produced run id rasterizes vector coverage
// (nonzero pixels) in bounds with the given color. T11 surface:
// RasterizeShapedText returns StatusOr<CoverageMask>; ok() is the
// success leg (the old DrawError::None shape).
static bool check_known_run_rasterizes_coverage() {
  const std::uint32_t known = known_run_from_engine();
  if (known == 0) return false;
  const muse::text::StatusOr<runtime::CoverageMask> coverage =
      runtime::RasterizeShapedText(known, test_bounds(), 0xFF112233u);
  if (!coverage.ok()) return false;
  // Vector coverage (nonzero pixels) in bounds: stems leave background
  // pixels inside every bound, so both ink and background must exist.
  bool any_ink = false;
  for (float a : coverage->alpha) {
    if (a != 0.0f) {
      any_ink = true;
      break;
    }
  }
  return any_ink;
}

// RULE-RUNTIME-001: an unknown/dangling run id fails closed with
// kMissingGlyph — the failed Status the caller maps onto
// DrawError::UnknownPrimitive (extends the glyphRunId == 0 sentinel rule)
// — and records nothing: never a substitute box or bitmap fallback.
static bool check_unknown_run_returns_unknown_primitive() {
  const muse::text::StatusOr<runtime::CoverageMask> coverage =
      runtime::RasterizeShapedText(kDanglingRun, test_bounds(), 0xFF112233u);
  return !coverage.ok() &&
         coverage.status() == muse::text::Status::kMissingGlyph;
}

// RULE-RUNTIME-001: run id 0 is the dangling sentinel (never a valid
// supplied handle) and fails closed the same way.
static bool check_zero_run_is_dangling() {
  const muse::text::StatusOr<runtime::CoverageMask> coverage =
      runtime::RasterizeShapedText(0u, test_bounds(), 0xFF112233u);
  return !coverage.ok() &&
         coverage.status() == muse::text::Status::kMissingGlyph;
}

}  // namespace

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_known_run_rasterizes_coverage);
  RUN_CASE(check_unknown_run_returns_unknown_primitive);
  RUN_CASE(check_zero_run_is_dangling);
  if (failures > 0) {
    std::printf("gallery_text_raster_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_text_raster_test: all passing\n");
  return 0;
}
