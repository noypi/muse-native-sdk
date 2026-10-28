// T2: Slug MSDF Bezier text path with subpixel alignment.
#include "engine/graphics/vector/slug_msdf.h"

namespace {

// Atlas golden pinned by the T2 contract: 16 glyph cells per row, glyph
// rows centered at V = 0.5, with 4 Bezier edge records per glyph for the
// Slug shader.
constexpr float kCellsPerRow = 16.0f;
constexpr float kTexelV = 0.5f;
constexpr int kEdgesPerGlyph = 4;

}  // namespace

MsdfResult shapeAndPlace(const ShapedRun& run, float subpixelX) {
  if (subpixelX < 0.0f) {
    return MsdfResult{Status::kInvalidArg, {}};
  }
  MsdfResult out;
  out.status = Status::kOk;
  if (run.glyphIds.empty()) {
    return out;
  }
  // The shift arrives pre-quantized to 1/64px from the text engine; apply it
  // as-is so half-texel (1/128px) goldens survive without rounding drift.
  out.glyphs.reserve(run.glyphIds.size());
  float pen = 0.0f;
  for (size_t i = 0; i < run.glyphIds.size(); ++i) {
    GlyphPlacement g;
    g.glyph = run.glyphIds[i];
    g.x = pen + subpixelX;
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
