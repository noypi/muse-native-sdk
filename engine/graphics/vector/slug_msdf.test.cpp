// T2 hermetic unit test — Slug MSDF text path (FR-GRAPHICS-001, AC-GRAPHICS-002).
// Style: plain asserts, no dependencies, no I/O, no network.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/graphics/vector/slug_msdf.h"

static slug_msdf::ShapedRun TwoGlyphRun() {
  slug_msdf::ShapedRun r;
  r.glyphIds = {7u, 9u};
  r.advances = {10.0f, 12.0f};
  return r;
}

// Golden texel mapping pinned by this contract: texelU = glyphId / 16,
// texelV = 0.5, edgeCount = 4 per glyph.
static void AssertGoldenTexel(const slug_msdf::GlyphPlacement& g) {
  float wantU = static_cast<float>(g.glyph) / 16.0f;
  float diffU = g.texelU > wantU ? g.texelU - wantU : wantU - g.texelU;
  assert(diffU < 1e-6f);
  float diffV = g.texelV > 0.5f ? g.texelV - 0.5f : 0.5f - g.texelV;
  assert(diffV < 1e-6f);
  assert(g.edgeCount == 4);
}

static void AssertNear(float got, float want, float eps = 1e-6f) {
  float d = got > want ? got - want : want - got;
  assert(d < eps);
}

int main() {
  // 1. Golden: 2-glyph run with raw subpixelX = 0.5/64 quantizes to 1/64
  // (round(0.5) = 1), so glyphs sit at pen 0 + 1/64 and pen 10 + 1/64.
  // MSDF texel coords + edge records set per the golden texel mapping.
  {
    float rawShift = 0.5f / 64.0f;
    float qShift = 1.0f / 64.0f;
    slug_msdf::MsdfResult r = slug_msdf::shapeAndPlace(TwoGlyphRun(), rawShift);
    assert(r.status == slug_msdf::Status::kOk);
    assert(r.glyphs.size() == 2u);
    assert(r.glyphs[0].glyph == 7u);
    assert(r.glyphs[1].glyph == 9u);
    AssertNear(r.glyphs[0].x, qShift);
    AssertNear(r.glyphs[1].x, 10.0f + qShift);
    AssertGoldenTexel(r.glyphs[0]);
    AssertGoldenTexel(r.glyphs[1]);
    printf("PASS golden-2glyph-half-texel-shift\n");
  }
  // 2. Empty run returns empty result, no error.
  {
    slug_msdf::ShapedRun empty;
    slug_msdf::MsdfResult r = slug_msdf::shapeAndPlace(empty, 0.0f);
    assert(r.status == slug_msdf::Status::kOk);
    assert(r.glyphs.empty());
    printf("PASS empty-run-empty-result\n");
  }
  // 3. Negative subpixelX returns kInvalidArg.
  {
    slug_msdf::MsdfResult r = slug_msdf::shapeAndPlace(TwoGlyphRun(), -0.25f);
    assert(r.status == slug_msdf::Status::kInvalidArg);
    printf("PASS negative-shift-invalid-arg\n");
  }
  // 4. Boundary: zero shift places glyphs exactly on pen positions.
  {
    slug_msdf::MsdfResult r = slug_msdf::shapeAndPlace(TwoGlyphRun(), 0.0f);
    assert(r.status == slug_msdf::Status::kOk);
    assert(r.glyphs.size() == 2u);
    assert(r.glyphs[0].x == 0.0f);
    assert(r.glyphs[1].x == 10.0f);
    printf("PASS zero-shift-exact-pens\n");
  }
  // 5. Boundary: subpixel shift quantizes to 1/64px (whole texel step preserved).
  {
    float shift = 3.0f / 64.0f;
    slug_msdf::MsdfResult r = slug_msdf::shapeAndPlace(TwoGlyphRun(), shift);
    assert(r.status == slug_msdf::Status::kOk);
    assert(r.glyphs.size() == 2u);
    AssertNear(r.glyphs[0].x, shift);
    AssertNear(r.glyphs[1].x, 10.0f + shift);
    printf("PASS shift-quantized-1-64px\n");
  }
  // 6. Every glyph emits Bezier edge records for the Slug shader.
  {
    slug_msdf::MsdfResult r = slug_msdf::shapeAndPlace(TwoGlyphRun(), 0.0f);
    assert(r.status == slug_msdf::Status::kOk);
    for (const auto& g : r.glyphs) assert(g.edgeCount > 0);
    printf("PASS every-glyph-emits-edges\n");
  }
  // 7. Regression (review finding): an off-grid shift (0.5/64) is quantized
  // to the 1/64 grid — the placed x must equal round(shift*64)/64, not the
  // raw shift.
  {
    float rawShift = 0.5f / 64.0f;
    float wantQ = std::round(rawShift * 64.0f) / 64.0f;
    assert(wantQ == 1.0f / 64.0f);
    slug_msdf::MsdfResult r = slug_msdf::shapeAndPlace(TwoGlyphRun(), rawShift);
    assert(r.status == slug_msdf::Status::kOk);
    assert(r.glyphs.size() == 2u);
    AssertNear(r.glyphs[0].x, wantQ);
    // Raw shift itself is off-grid: placed x must differ from it.
    assert(r.glyphs[0].x != rawShift);
    // And the quantized value is on the 1/64 grid.
    float gridPos = r.glyphs[0].x * 64.0f;
    assert(std::round(gridPos) == gridPos);
    printf("PASS off-grid-shift-quantized-to-1-64-grid\n");
  }
  printf("ALL T2 TESTS PASSED\n");
  return 0;
}
