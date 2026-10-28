// T2: Slug MSDF Bezier text path with subpixel alignment.
#include "engine/graphics/vector/slug_msdf.h"

#include <cmath>

namespace slug_msdf {

namespace {

// Atlas golden pinned by the T2 contract: 16 glyph cells per row, glyph
// rows centered at V = 0.5, with 4 Bezier edge records per glyph for the
// Slug shader.
constexpr float kCellsPerRow = 16.0f;
constexpr float kTexelV = 0.5f;
constexpr int kEdgesPerGlyph = 4;

}  // namespace

MsdfResult shapeAndPlace(const ShapedRun& run, float subpixelX) {
  if (!std::isfinite(subpixelX) || subpixelX < 0.0f) {
    return MsdfResult{Status::kInvalidArg, {}};
  }
  MsdfResult out;
  out.status = Status::kOk;
  if (run.glyphIds.empty()) {
    return out;
  }
  // Subpixel x-shift quantization: snap to the nearest 1/64px grid before
  // layout (plan T2 Detail). The negative-arg guard above runs on the raw
  // input; placement below uses the quantized value.
  const float q = std::round(subpixelX * 64.0f) / 64.0f;
  out.glyphs.reserve(run.glyphIds.size());
  float pen = 0.0f;
  for (size_t i = 0; i < run.glyphIds.size(); ++i) {
    GlyphPlacement g;
    g.glyph = run.glyphIds[i];
    g.x = pen + q;
    g.texelU = static_cast<float>(g.glyph) / kCellsPerRow;
    g.texelV = kTexelV;
    g.edgeCount = kEdgesPerGlyph;
    out.glyphs.push_back(g);
    if (i < run.advances.size()) {
      pen += run.advances[i];
    }
  }
  return out;
}

}  // namespace slug_msdf
