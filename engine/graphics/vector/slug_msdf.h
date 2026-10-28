#ifndef ENGINE_GRAPHICS_VECTOR_SLUG_MSDF_H_
#define ENGINE_GRAPHICS_VECTOR_SLUG_MSDF_H_

// T2: Slug MSDF Bezier text path with subpixel alignment (CPU placement
// mirror of the Slug shader path). Consumes the FR-TEXT-007 shaped-run
// struct (mocked in unit tests); no font parsing here.

#include <cstdint>
#include <vector>

enum class Status { kOk = 0, kInvalidArg = 1 };

// Mocked FR-TEXT-007 interface: shaped glyph run from the text engine.
struct ShapedRun {
  std::vector<uint32_t> glyphIds;
  std::vector<float> advances;  // Advance width per glyph, in px.
};

struct GlyphPlacement {
  uint32_t glyph = 0;
  float x = 0.0f;       // Subpixel-shifted pen x for this glyph.
  float texelU = 0.0f;  // MSDF texel coords for this glyph.
  float texelV = 0.0f;
  int edgeCount = 0;  // Bezier edge records emitted for the Slug shader.
};

struct MsdfResult {
  Status status = Status::kOk;
  std::vector<GlyphPlacement> glyphs;
};

// Lay out the shaped run along the pen, offset by subpixelX, and emit one
// MSDF texel record per glyph for the Slug shader.
// Empty run yields an empty result with Status::kOk.
// Negative subpixelX yields Status::kInvalidArg.
MsdfResult shapeAndPlace(const ShapedRun& run, float subpixelX);

#endif  // ENGINE_GRAPHICS_VECTOR_SLUG_MSDF_H_
