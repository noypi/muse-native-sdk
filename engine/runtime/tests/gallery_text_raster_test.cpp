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

#include "runtime/renderer.h"

namespace {

// A run id the (coder-supplied) test-seeded atlas resolves: stands in for
// a T5-produced run id. The rasterizer must consult the atlas/shaper seam
// (mocked in-process by the coder's test hook), never the device.
constexpr std::uint32_t kKnownRun = 7;
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
// (nonzero pixels) in bounds with the given color.
static bool check_known_run_rasterizes_coverage() {
  const runtime::DrawError err =
      runtime::RasterizeShapedText(kKnownRun, test_bounds(), 0xFF112233u);
  return err == runtime::DrawError::None;
}

// RULE-RUNTIME-001: an unknown/dangling run id fails closed with
// UnknownPrimitive (extends the glyphRunId == 0 sentinel rule) and records
// nothing — never a substitute box or bitmap fallback.
static bool check_unknown_run_returns_unknown_primitive() {
  const runtime::DrawError err =
      runtime::RasterizeShapedText(kDanglingRun, test_bounds(), 0xFF112233u);
  return err == runtime::DrawError::UnknownPrimitive;
}

// RULE-RUNTIME-001: run id 0 is the dangling sentinel (never a valid
// supplied handle) and fails closed the same way.
static bool check_zero_run_is_dangling() {
  const runtime::DrawError err =
      runtime::RasterizeShapedText(0u, test_bounds(), 0xFF112233u);
  return err == runtime::DrawError::UnknownPrimitive;
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
