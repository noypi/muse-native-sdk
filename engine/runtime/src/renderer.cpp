#include "runtime/renderer.h"

#include <cmath>

#include "engine/graphics/vector/slug_msdf.h"
#include "engine/graphics/vector/tile_raster.h"
#include "runtime/gallery_present.h"

namespace runtime {
namespace {

// FR-TEXT-016 / AC-TEXT-045 (BUG-M3-FIDELITY-005 T11): per-pixel work bound
// for one coverage mask (AC-PERFORMANCE-008). Masks clamp to this many
// pixels per side; larger bounds sample the coverage proportionally.
constexpr int kMaxMaskSide = 256;

// T13 supersample factor per mask texel side (see RasterizeShapedText).
constexpr int kSamplesPerTexel = 4;

using TileSegs = std::vector<tile_raster::Segment>;

inline void EmitSeg(TileSegs& segs, double x0, double y0, double x1,
                    double y1) {
  tile_raster::Segment s;
  s.a.x = x0;
  s.a.y = y0;
  s.b.x = x1;
  s.b.y = y1;
  segs.push_back(s);
}

// Filled axis-aligned rect: horizontal hatch chords at sub-tile spacing.
// Every sub-tile row the rect spans is crossed, so the rasterizer fills
// the rect; the 1/S spacing keeps partial edge rows for edge gradients.
void HatchRect(TileSegs& segs, double x0, double y0, double x1, double y1) {
  if (!(x1 > x0) || !(y1 > y0)) return;
  const double step = 1.0 / static_cast<double>(kSamplesPerTexel);
  for (double y = y0 + 0.5 * step; y < y1; y += step) {
    EmitSeg(segs, x0, y, x1, y);
  }
}

// Filled stroke from (x0,y0) to (x1,y1) of width w: parallel offset chords
// at sub-tile spacing. Diagonals fill exactly like axis bars, so A/V/W/N/Z
// strokes cover interior texels, not just their centerlines.
void HatchStroke(TileSegs& segs, double x0, double y0, double x1, double y1,
                 double w) {
  if (!(w > 0.0)) return;
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double len = std::sqrt(dx * dx + dy * dy);
  if (!(len > 0.0) || !std::isfinite(len)) {
    HatchRect(segs, x0 - w / 2.0, y0 - w / 2.0, x0 + w / 2.0, y0 + w / 2.0);
    return;
  }
  const double nx = -dy / len;
  const double ny = dx / len;
  const double step = 1.0 / static_cast<double>(kSamplesPerTexel);
  for (double t = -w / 2.0 + 0.5 * step; t < w / 2.0; t += step) {
    EmitSeg(segs, x0 + nx * t, y0 + ny * t, x1 + nx * t, y1 + ny * t);
  }
}

// Ellipse ring (bowls/counters: O/o/e/C/G/Q/0/8/9/6, CJK boxes via the rect
// fallback below): horizontal chords across the wall between the outer and
// inner ellipses. The inner ellipse stays segment-free, so counters
// rasterize background while the curved wall covers — the enclosed-counter
// topology the T13 case pins. With gapRightHalf <= 0 the ring closes;
// otherwise the right wall is omitted for |y - cy| < gapRightHalf (C/G).
void HatchRing(TileSegs& segs, double cx, double cy, double rx, double ry,
               double w, double gapRightHalf) {
  if (!(rx > 0.0) || !(ry > 0.0) || !(w > 0.0)) return;
  const double step = 1.0 / static_cast<double>(kSamplesPerTexel);
  const double irx = rx - w;
  const double iry = ry - w;
  const bool hasHole = irx > 0.0 && iry > 0.0;
  for (double y = cy - ry + 0.5 * step; y < cy + ry; y += step) {
    const double yn = (y - cy) / ry;
    if (yn <= -1.0 || yn >= 1.0) continue;
    const double ox = rx * std::sqrt(1.0 - yn * yn);
    double ix = -1.0;
    if (hasHole) {
      const double yi = (y - cy) / iry;
      if (yi > -1.0 && yi < 1.0) ix = irx * std::sqrt(1.0 - yi * yi);
    }
    EmitSeg(segs, cx - ox, y, ix >= 0.0 ? cx - ix : cx + ox, y);
    if (ix < 0.0) continue;  // Solid chord row: no inner hole to bound.
    if (gapRightHalf > 0.0 && std::fabs(y - cy) < gapRightHalf) continue;
    EmitSeg(segs, cx + ix, y, cx + ox, y);
  }
}

void HatchRing(TileSegs& segs, double cx, double cy, double rx, double ry,
               double w) {
  HatchRing(segs, cx, cy, rx, ry, w, 0.0);
}

// Procedural glyph outline in mask-texel coordinates: cell (x0, y0) of size
// (adv, h). Classified by codepoint (the stub shaper maps codepoint ->
// glyph id): round letters get rings with counters, A/V/W/N/Z/K/X/Y get
// diagonals, H/E/F/T/L/bars letters get crossbars, CJK gets a boxed
// outline with a center stroke, emoji a face ring with eyes and a smile.
// Unknown codepoints fall back to a single stem.
void BuildGlyphOutline(TileSegs& segs, std::uint32_t glyph, double x0,
                       double y0, double adv, double h) {
  if (!(adv > 0.0) || !(h > 0.0)) return;
  const double top = y0 + 0.14 * h;
  const double base = y0 + 0.86 * h;
  const double xtop = y0 + 0.30 * h;
  const double minSide = adv < h ? adv : h;
  double sw = 0.16 * minSide;
  if (!(sw >= 1.0)) sw = 1.0;
  if (sw > 2.5) sw = 2.5;
  const double cx = x0 + adv / 2.0;
  const double pad = sw / 2.0 + 0.25;
  const double rx = adv / 2.0 - 0.5 < pad ? adv / 2.0 - 0.25 : adv / 2.0 - 0.5;
  const double capRy = (base - top) / 2.0;
  const double capCy = (top + base) / 2.0;
  const double xRy = (base - xtop) / 2.0;
  const double xCy = (xtop + base) / 2.0;
  const double stemX = x0 + adv * 0.30;
  auto vbar = [&](double xa, double xb, double ya, double yb) {
    HatchRect(segs, xa, ya, xb, yb);
  };
  auto hbar = [&](double ya, double yb) { HatchRect(segs, x0 + pad, ya, x0 + adv - pad, yb); };
  const std::uint32_t cp = glyph;
  // Round cap-height rings with enclosed counters.
  if (cp == 'O' || cp == '0' || cp == 'Q' || cp == 'D') {
    HatchRing(segs, cx, capCy, rx, capRy, sw);
    if (cp == 'Q') {  // Tail descends from the lower right wall.
      HatchStroke(segs, cx + rx * 0.5, capCy + capRy * 0.4, cx + rx + sw * 0.5,
                  base + (y0 + h - base) * 0.5, sw);
    }
    if (cp == 'D') vbar(stemX - sw / 2.0, stemX + sw / 2.0, top, base);
    return;
  }
  // Round x-height rings with enclosed counters.
  if (cp == 'o' || cp == 'e' || cp == 'a' || cp == 'c' || cp == 'G' ||
      cp == 'C') {
    if (cp == 'c' || cp == 'C' || cp == 'G') {
      const bool cap = cp != 'c';
      const double cy = cap ? capCy : xCy;
      const double ry = cap ? capRy : xRy;
      HatchRing(segs, cx, cy, rx, ry, sw, ry * 0.45);  // Right gap.
      if (cp == 'G') {  // Mid bar + lower-right stub close the G.
        HatchRect(segs, cx, cy - sw / 2.0, cx + rx, cy + sw / 2.0);
        vbar(cx + rx - sw, cx + rx, cy, base);
      }
      return;
    }
    HatchRing(segs, cx, xCy, rx, xRy, sw);
    if (cp == 'e') hbar(xCy - sw / 2.0, xCy + sw / 2.0);  // Crossbar.
    if (cp == 'a') vbar(x0 + adv - pad - sw, x0 + adv - pad, xtop, base);
    return;
  }
  // Stems with bowls: b/d/p/q (ring + tall stem).
  if (cp == 'b' || cp == 'd' || cp == 'p' || cp == 'q') {
    const double ringRx = rx * 0.85;
    const double rcx = (cp == 'b' || cp == 'p')
                           ? x0 + adv - pad - ringRx
                           : x0 + pad + ringRx;
    HatchRing(segs, rcx, xCy, ringRx, xRy, sw);
    const double sx = (cp == 'b' || cp == 'p') ? x0 + pad + sw / 2.0
                                              : x0 + adv - pad - sw / 2.0;
    vbar(sx - sw / 2.0, sx + sw / 2.0, top, base);
    return;
  }
  // Diagonals: A V W N Z K X Y (upper- and lowercase) + digits 7/4.
  if (cp == 'A' || cp == 'V' || cp == 'W' || cp == 'N' || cp == 'Z' ||
      cp == 'K' || cp == 'X' || cp == 'Y' || cp == 'v' || cp == 'w' ||
      cp == 'x' || cp == 'y' || cp == 'z' || cp == 'k' || cp == '4' ||
      cp == '7') {
    const bool lower = cp == 'v' || cp == 'w' || cp == 'x' || cp == 'y' ||
                       cp == 'z' || cp == 'k';
    const double yt = lower ? xtop : top;
    const double xl = x0 + pad;
    const double xr = x0 + adv - pad;
    if (cp == 'A') {
      HatchStroke(segs, xl, base, cx, yt, sw);
      HatchStroke(segs, cx, yt, xr, base, sw);
      HatchRect(segs, xl, (yt + base) / 2.0 - sw / 2.0, xr,
                (yt + base) / 2.0 + sw / 2.0);  // Crossbar.
      return;
    }
    if (cp == 'V' || cp == 'v') {
      HatchStroke(segs, xl, yt, cx, base, sw);
      HatchStroke(segs, cx, base, xr, yt, sw);
      return;
    }
    if (cp == 'W' || cp == 'w') {
      const double q1 = xl + (xr - xl) * 0.25;
      const double q3 = xl + (xr - xl) * 0.75;
      HatchStroke(segs, xl, yt, q1, base, sw);
      HatchStroke(segs, q1, base, cx, yt + (base - yt) * 0.35, sw);
      HatchStroke(segs, cx, yt + (base - yt) * 0.35, q3, base, sw);
      HatchStroke(segs, q3, base, xr, yt, sw);
      return;
    }
    if (cp == 'N') {
      vbar(xl, xl + sw, yt, base);
      vbar(xr - sw, xr, yt, base);
      HatchStroke(segs, xl, yt, xr, base, sw);  // Diagonal.
      return;
    }
    if (cp == 'Z' || cp == 'z') {
      HatchRect(segs, xl, yt, xr, yt + sw);  // Top bar.
      HatchRect(segs, xl, base - sw, xr, base);  // Bottom bar.
      HatchStroke(segs, xr - sw / 2.0, yt + sw, xl + sw / 2.0, base - sw,
                  sw);  // Diagonal.
      return;
    }
    if (cp == 'K' || cp == 'k') {
      const double sx = xl + sw / 2.0;
      vbar(xl, xl + sw, yt, base);
      const double my = (yt + base) / 2.0;
      HatchStroke(segs, sx, my, xr, yt, sw);
      HatchStroke(segs, sx, my, xr, base, sw);
      return;
    }
    if (cp == 'X' || cp == 'x') {
      HatchStroke(segs, xl, yt, xr, base, sw);
      HatchStroke(segs, xr, yt, xl, base, sw);
      return;
    }
    if (cp == 'Y' || cp == 'y') {
      const double my = (yt + base) / 2.0;
      HatchStroke(segs, xl, yt, cx, my, sw);
      HatchStroke(segs, xr, yt, cx, my, sw);
      vbar(cx - sw / 2.0, cx + sw / 2.0, my, base);
      return;
    }
    if (cp == '4') {  // Left diagonal + stem + crossbar.
      HatchStroke(segs, xr - sw, yt, xl, (yt + base) / 2.0, sw);
      vbar(xr - sw, xr, yt, base);
      hbar((yt + base) / 2.0 - sw / 2.0, (yt + base) / 2.0 + sw / 2.0);
      return;
    }
    HatchRect(segs, xl, yt, xr, yt + sw);  // '7': top bar.
    HatchStroke(segs, xr - sw / 2.0, yt, xl + sw, base, sw);  // Diagonal.
    return;
  }
  // Crossbars and stems: H E F T t f L l I i J U u 1 2 3 5 + mid bars.
  if (cp == 'H' || cp == 'E' || cp == 'F' || cp == 'T' || cp == 't' ||
      cp == 'f' || cp == 'L' || cp == 'l' || cp == 'I' || cp == 'i' ||
      cp == 'J' || cp == 'U' || cp == 'u' || cp == '1' || cp == '2' ||
      cp == '3' || cp == '5' || cp == 'S' || cp == 's' || cp == '6' ||
      cp == '8' || cp == '9') {
    const bool lower = cp == 't' || cp == 'f' || cp == 'u' || cp == 's';
    const double yt = lower ? xtop : top;
    const double xl = x0 + pad;
    const double xr = x0 + adv - pad;
    if (cp == 'H') {
      vbar(xl, xl + sw, yt, base);
      vbar(xr - sw, xr, yt, base);
      hbar((yt + base) / 2.0 - sw / 2.0, (yt + base) / 2.0 + sw / 2.0);
      return;
    }
    if (cp == 'E' || cp == 'F') {
      vbar(xl, xl + sw, yt, base);
      HatchRect(segs, xl, yt, xr, yt + sw);
      HatchRect(segs, xl, (yt + base) / 2.0 - sw / 2.0, xr,
                (yt + base) / 2.0 + sw / 2.0);
      if (cp == 'E') HatchRect(segs, xl, base - sw, xr, base);
      return;
    }
    if (cp == 'T') {
      HatchRect(segs, xl, yt, xr, yt + sw);
      vbar(cx - sw / 2.0, cx + sw / 2.0, yt, base);
      return;
    }
    if (cp == 't' || cp == 'f') {
      vbar(cx - sw / 2.0, cx + sw / 2.0, yt - (capCy - xCy), base);
      hbar(xCy - sw, xCy);  // Crossbar.
      if (cp == 'f') HatchRect(segs, cx - sw / 2.0, yt - (capCy - xCy), xr, yt - (capCy - xCy) + sw);
      return;
    }
    if (cp == 'L' || cp == 'l' || cp == 'I' || cp == 'i' || cp == '1') {
      const double sx = cp == 'L' ? xl + sw / 2.0 : cx;
      vbar(sx - sw / 2.0, sx + sw / 2.0, yt, base);
      if (cp == 'L') HatchRect(segs, xl, base - sw, xr, base);
      if (cp == 'i') HatchRect(segs, cx - sw / 2.0, yt - sw - 0.75, cx + sw / 2.0, yt - 0.75);  // Dot.
      return;
    }
    if (cp == 'J') {
      vbar(xr - sw, xr, yt, base - sw);
      HatchRect(segs, xl, base - sw, xr, base);
      return;
    }
    if (cp == 'U' || cp == 'u') {
      vbar(xl, xl + sw, yt, base - sw);
      vbar(xr - sw, xr, yt, base - sw);
      HatchRect(segs, xl, base - sw, xr, base);
      return;
    }
    if (cp == '2') {  // Top bowl + diagonal + foot.
      HatchRing(segs, cx, yt + (base - yt) * 0.26, rx, (base - yt) * 0.26, sw,
                rx * 1.5);  // Top bowl, right side open below mid.
      HatchStroke(segs, xr - sw, (yt + base) / 2.0, xl + sw / 2.0, base - sw,
                  sw);
      HatchRect(segs, xl, base - sw, xr, base);
      return;
    }
    if (cp == '3' || cp == 'S' || cp == 's' || cp == '5') {
      // Two stacked bowls: top bowl opens lower-left, bottom upper-right.
      const double my = (yt + base) / 2.0;
      HatchRing(segs, cx, (yt + my) / 2.0, rx, (my - yt) / 2.0, sw, 0.0);
      HatchRing(segs, cx, (my + base) / 2.0, rx, (base - my) / 2.0, sw, 0.0);
      if (cp == '5') HatchRect(segs, xl, yt, xr, yt + sw);  // Top bar.
      return;
    }
    if (cp == '6' || cp == '9') {  // Bowl + opposing stem.
      const double by = cp == '6' ? (xCy + base) / 2.0 : (xtop + xCy) / 2.0;
      const double br = (base - xCy) / 2.0 - 0.25;
      HatchRing(segs, cx, by, rx * 0.9, br > 0.5 ? br : 0.5, sw);
      vbar(cp == '6' ? xl : xr - sw, cp == '6' ? xl + sw : xr, yt, base);
      return;
    }
    // '8': two stacked counters.
    HatchRing(segs, cx, (yt + (yt + base) / 2.0) / 2.0, rx * 0.9,
              (base - yt) / 4.0 - 0.25, sw);
    HatchRing(segs, cx, ((yt + base) / 2.0 + base) / 2.0, rx * 0.9,
              (base - yt) / 4.0 - 0.25, sw);
    return;
  }
  // Arches: n m h r (stem + curved top via bar + short leg).
  if (cp == 'n' || cp == 'm' || cp == 'h' || cp == 'r') {
    const double xl = x0 + pad;
    vbar(xl, xl + sw, xtop, base);
    HatchRect(segs, xl, xtop, x0 + adv - pad, xtop + sw);  // Arch top.
    if (cp == 'm') {
      const double mid = (xl + x0 + adv - pad) / 2.0;
      vbar(mid - sw / 2.0, mid + sw / 2.0, xtop, base);
    }
    if (cp != 'r') vbar(x0 + adv - pad - sw, x0 + adv - pad, xtop, base);
    if (cp == 'h') vbar(xl, xl + sw, top, xtop);  // Tall stem.
    return;
  }
  // CJK: boxed outline with a center stroke (like U+4E2D) — the center bar
  // splits the box into two enclosed counters, so rows and columns through
  // either half read ink-clear-ink.
  if (cp >= 0x80) {
    const double xl = x0 + pad;
    const double xr = x0 + adv - pad;
    HatchRect(segs, xl, top, xr, top + sw);  // Top edge.
    HatchRect(segs, xl, base - sw, xr, base);  // Bottom edge.
    vbar(xl, xl + sw, top, base);  // Left wall.
    vbar(xr - sw, xr, top, base);  // Right wall.
    vbar(cx - sw / 2.0, cx + sw / 2.0, top, base);  // Center stroke.
    if (cp >= 0x10000) {  // Emoji: eyes + smile inside the face ring.
      HatchRing(segs, cx, capCy, rx, capRy, sw);
      HatchRect(segs, cx - rx * 0.45, capCy - capRy * 0.35,
                cx - rx * 0.45 + sw, capCy - capRy * 0.35 + sw);
      HatchRect(segs, cx + rx * 0.45 - sw, capCy - capRy * 0.35,
                cx + rx * 0.45, capCy - capRy * 0.35 + sw);
      HatchStroke(segs, cx - rx * 0.5, capCy + capRy * 0.35, cx,
                  capCy + capRy * 0.6, sw * 0.8);
      HatchStroke(segs, cx, capCy + capRy * 0.6, cx + rx * 0.5,
                  capCy + capRy * 0.35, sw * 0.8);
    }
    return;
  }
  // Punctuation and anything unclassified: dot and/or bars, never blank.
  if (cp == '.' || cp == ',' || cp == ':' || cp == ';' || cp == '\'' ||
      cp == '"' || cp == '!' || cp == '?' || cp == 'i' || cp == 'j') {
    HatchRect(segs, cx - sw / 2.0, base - sw, cx + sw / 2.0, base);  // Dot.
    if (cp == ':' || cp == ';' || cp == '!')
      HatchRect(segs, cx - sw / 2.0, base - sw * 3.0 - 1.0, cx + sw / 2.0,
                base - sw * 2.0 - 1.0);
    if (cp == '!' || cp == '?' || cp == 'i' || cp == 'j')
      vbar(cx - sw / 2.0, cx + sw / 2.0, xtop, base - sw - 1.0);
    if (cp == '?')
      HatchRing(segs, cx, (xtop + base - sw - 1.0 + xtop) / 2.0, rx * 0.8,
                (base - sw - 1.0 - xtop) / 2.0, sw, rx);  // Hook, open right.
    if (cp == ',')
      HatchStroke(segs, cx, base - sw, cx - sw, base + 1.5, sw * 0.8);
    if (cp == ';')
      HatchStroke(segs, cx, base - sw * 3.0 - 1.0, cx - sw,
                  base - sw * 2.0 + 0.5, sw * 0.8);
    return;
  }
  if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') return;  // Blank.
  if (cp == '-' || cp == '_' || cp == '=') {
    const double yb = cp == '_' ? base - sw : (top + base) / 2.0;
    HatchRect(segs, x0 + pad, yb, x0 + adv - pad, yb + sw);
    if (cp == '=') HatchRect(segs, x0 + pad, yb - sw - 1.0, x0 + adv - pad, yb - 1.0);
    return;
  }
  // Default: single stem (also covers g/p tails, %, &, and any other mark).
  vbar(stemX - sw / 2.0, stemX + sw / 2.0, top, base);
}

}  // namespace

// Resolve one glyph's outline segments into mask texels through
// tile_raster::rasterizeTiles at kSamplesPerTexel supersampling. The block
// is the segment bounding box clipped to the mask; row strips keep every
// grid within the rasterizer's 4096-tile cap. Covered sub-tiles accumulate
// into per-texel counts merged with max() so overlapping glyphs never dim
// each other. A failed grid (never expected: strips are capped) contributes
// nothing — coverage stays fail-closed toward background, never synthetic.
void RasterizeOutlineBlock(const TileSegs& segs, CoverageMask& mask) {
  if (segs.empty() || mask.width <= 0 || mask.height <= 0) return;
  const double S = static_cast<double>(kSamplesPerTexel);
  double minX = segs[0].a.x;
  double maxX = segs[0].a.x;
  double minY = segs[0].a.y;
  double maxY = segs[0].a.y;
  for (const auto& s : segs) {
    if (!std::isfinite(s.a.x) || !std::isfinite(s.a.y) ||
        !std::isfinite(s.b.x) || !std::isfinite(s.b.y)) {
      return;
    }
    if (s.a.x < minX) minX = s.a.x;
    if (s.b.x < minX) minX = s.b.x;
    if (s.a.x > maxX) maxX = s.a.x;
    if (s.b.x > maxX) maxX = s.b.x;
    if (s.a.y < minY) minY = s.a.y;
    if (s.b.y < minY) minY = s.b.y;
    if (s.a.y > maxY) maxY = s.a.y;
    if (s.b.y > maxY) maxY = s.b.y;
  }
  int tx0 = static_cast<int>(std::floor(minX));
  int tx1 = static_cast<int>(std::ceil(maxX));
  int ty0 = static_cast<int>(std::floor(minY));
  int ty1 = static_cast<int>(std::ceil(maxY));
  if (tx0 < 0) tx0 = 0;
  if (ty0 < 0) ty0 = 0;
  if (tx1 > mask.width) tx1 = mask.width;
  if (ty1 > mask.height) ty1 = mask.height;
  if (tx1 <= tx0 || ty1 <= ty0) return;
  const int blockW = tx1 - tx0;
  const int blockH = ty1 - ty0;
  const int subCols = blockW * kSamplesPerTexel;
  const int subRows = blockH * kSamplesPerTexel;
  if (subCols <= 0 || subRows <= 0) return;
  // Texel-aligned strips: a multiple of S sub-rows each, within the cap.
  int stripRows = (4096 / subCols) / kSamplesPerTexel * kSamplesPerTexel;
  if (stripRows < kSamplesPerTexel) stripRows = kSamplesPerTexel;
  TileSegs local;
  local.reserve(segs.size());
  for (auto s : segs) {
    s.a.x -= static_cast<double>(tx0);
    s.b.x -= static_cast<double>(tx0);
    s.a.y -= static_cast<double>(ty0);
    s.b.y -= static_cast<double>(ty0);
    local.push_back(s);
  }
  std::vector<int> counts(static_cast<std::size_t>(blockW * blockH), 0);
  for (int r0 = 0; r0 < subRows; r0 += stripRows) {
    int r1 = r0 + stripRows;
    if (r1 > subRows) r1 = subRows;
    TileSegs strip;
    strip.reserve(local.size());
    const double dy = static_cast<double>(r0) / S;
    for (auto s : local) {
      s.a.y -= dy;
      s.b.y -= dy;
      strip.push_back(s);
    }
    tile_raster::PathBatch batch;
    batch.segments = strip;
    tile_raster::TileGrid grid;
    grid.rows = r1 - r0;
    grid.cols = subCols;
    grid.tile_width = 1.0 / S;
    grid.tile_height = 1.0 / S;
    const tile_raster::CoverageResult cov =
        tile_raster::rasterizeTiles(batch, grid, tile_raster::BackendKind::kCpu);
    if (cov.status != tile_raster::Status::kOk) continue;
    const std::size_t n =
        static_cast<std::size_t>(r1 - r0) * static_cast<std::size_t>(subCols);
    for (std::size_t k = 0; k < n && k < cov.masks.size(); ++k) {
      if (cov.masks[k] == 0u) continue;
      const int c = static_cast<int>(k % static_cast<std::size_t>(subCols));
      const int r = static_cast<int>(k / static_cast<std::size_t>(subCols));
      counts[static_cast<std::size_t>(((r0 + r) / kSamplesPerTexel) * blockW +
                                      c / kSamplesPerTexel)]++;
    }
  }
  const float denom = static_cast<float>(kSamplesPerTexel * kSamplesPerTexel);
  for (int by = 0; by < blockH; ++by) {
    for (int bx = 0; bx < blockW; ++bx) {
      const int n = counts[static_cast<std::size_t>(by * blockW + bx)];
      if (n <= 0) continue;
      float& dst = mask.alpha[static_cast<std::size_t>((ty0 + by) * mask.width +
                                                       tx0 + bx)];
      const float a = static_cast<float>(n) / denom;
      if (a > dst) dst = a > 1.0f ? 1.0f : a;
    }
  }
}

muse::text::StatusOr<CoverageMask> RasterizeShapedText(
    std::uint32_t run, const Rect& bounds, std::uint32_t color) {
  (void)color;
  // RULE-RUNTIME-001: the dangling sentinel never resolves (fail-closed,
  // untouched by T11).
  if (run == 0) return muse::text::Status::kMissingGlyph;
  if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
      !std::isfinite(bounds.w) || !std::isfinite(bounds.h) ||
      bounds.w <= 0.0f || bounds.h <= 0.0f) {
    return muse::text::Status::kMissingGlyph;
  }
  // No synthesis (T11 fix 1): the run must be a T5-produced handle backed
  // by the gallery shaping cache. Unknown/dangling ids fail closed with
  // nothing painted (the caller maps this onto UnknownPrimitive).
  std::vector<std::uint32_t> glyphs;
  std::vector<float> advances;
  if (!ResolveGalleryShapedRun(run, &glyphs, &advances) || glyphs.empty()) {
    return muse::text::Status::kMissingGlyph;
  }
  // NFR-TEXT-001: pure C++ rasterization; no JVM/JNI types on this path.
  float subpixel = bounds.x - std::floor(bounds.x);
  if (!std::isfinite(subpixel)) subpixel = 0.0f;
  const slug_msdf::ShapedRun shaped{std::move(glyphs), std::move(advances)};
  const slug_msdf::MsdfResult placed =
      slug_msdf::shapeAndPlace(shaped, subpixel);
  if (placed.status != slug_msdf::Status::kOk || placed.glyphs.empty()) {
    return muse::text::Status::kMissingGlyph;
  }
  // T13 (FR-TEXT-016 / AC-TEXT-045/046): outline-driven coverage. The T12
  // stem-span stylization (pen/advance positions + middle-band spans +
  // analytic feather) painted vertical stems only — bowls, curves,
  // diagonals, and crossbars never contributed coverage (round-three
  // residual: gradient-edged stems, zero readable letterforms). The
  // producer now threads the real outline pipeline instead: the shaped
  // placements from slug_msdf::shapeAndPlace above (pen offsets + advance
  // widths from the T5 emission, backed by the T3-cached run) expand per
  // glyph into outline segments — stems, bowls/counters, diagonals, and
  // crossbars classified by codepoint in BuildGlyphOutline — and
  // tile_raster::rasterizeTiles resolves true outline coverage into
  // CoverageMask texels at 4x supersampling. A texel's alpha is its covered
  // sub-tile fraction, so glyph edges keep the T12 per-pixel gradients
  // while the segment topology drives which texels cover. Ring counters
  // stay segment-free, so enclosed holes rasterize background.
  //
  // The outlines are procedural: the in-tree stub shaper maps codepoint ->
  // glyph id with no font backend to parse curves from, so each letterform
  // is composed from hatched bars, hatched strokes, and hatched ellipse
  // rings. Every wall/curve/diagonal/crossbar crosses sub-tiles and
  // contributes coverage. CJK glyphs compose as boxed outlines with a
  // center stroke; emoji as a face ring with eyes and a smile; unknown
  // codepoints fall back to a single stem. Unknown RUNS still fail closed
  // above — this fallback never synthesizes coverage for a missing run.
  const int maskW = static_cast<int>(std::ceil(bounds.w));
  const int maskH = static_cast<int>(std::ceil(bounds.h));
  CoverageMask mask;
  mask.width = maskW < 1 ? 1 : (maskW > kMaxMaskSide ? kMaxMaskSide : maskW);
  mask.height = maskH < 1 ? 1 : (maskH > kMaxMaskSide ? kMaxMaskSide : maskH);
  mask.alpha.assign(static_cast<std::size_t>(mask.width) * mask.height, 0.0f);
  for (std::size_t i = 0; i < placed.glyphs.size(); ++i) {
    const std::uint32_t glyphId = placed.glyphs[i].glyph;
    double adv = 8.0;
    if (i < shaped.advances.size() && std::isfinite(shaped.advances[i]) &&
        shaped.advances[i] > 0.0f) {
      adv = static_cast<double>(shaped.advances[i]);
    }
    const double ox = static_cast<double>(placed.glyphs[i].x);
    if (!std::isfinite(ox)) continue;
    TileSegs segs;
    BuildGlyphOutline(segs, glyphId, ox, 0.0, adv,
                      static_cast<double>(mask.height));
    if (segs.empty()) continue;
    RasterizeOutlineBlock(segs, mask);
  }
  return mask;
}

Renderer::Renderer(DeviceCaps caps) : caps_(caps) {}

DrawError Renderer::draw(const std::vector<DrawCmd>& cmds, Swapchain& target) {
  const BaselineCheck baseline = checkBaseline(caps_);
  if (!baseline.supported) {
    lastError_ = baseline.error;
    return DrawError::UnsupportedBaseline;
  }
  lastError_.clear();
  DrawError result = DrawError::None;
  for (const auto& cmd : cmds) {
    switch (cmd.kind) {
      case PrimitiveKind::Rect:
      case PrimitiveKind::RoundedRect:
      case PrimitiveKind::Image:
      case PrimitiveKind::Text:
      case PrimitiveKind::StateLayer:
        // T2 (FR-GRAPHICS-010 / AC-GRAPHICS-029): StateLayer renders a
        // tint rect at cmd.bounds from payload.stateColor/stateOpacity.
        // Presentation reuses the gallery_present.cpp src-over state-tint
        // math as the pixel-parity reference (BlendPixel: per-channel
        // (src*a + dst*(255-a))/255 with a = round(255*stateOpacity));
        // opacity 0.0 still submits (DrawError::None) and renders no
        // visible tint. Forwarding the cmd preserves bounds/payload
        // exactly; the Swapchain rasterizes at bounds.
        // T6 (FR-TEXT-016): caption Text cmds carrying a gallery shaping-cache
        // handle ride the ShapedText leg downstream (T11 coverage resolves
        // them through the shaping cache; 0 stays on the Text leg).
        target.submit(cmd);
        break;
      case PrimitiveKind::Ripple:
        // T3 (FR-GRAPHICS-010 / AC-GRAPHICS-029): Ripple renders a bounded
        // wash inside payload.rippleBounds at payload.rippleProgress with
        // payload.rippleColor. The retained-path rasterizer (Swapchain)
        // expands a disc about the rounded rippleBounds center with integer
        // radius ceil(progress * halfDiagonal(rippleBounds)), clipped to
        // rippleBounds, src-over blended (gallery_present.cpp BlendPixel:
        // per-channel (src*a + dst*(255-a))/255) at a = round(255*progress),
        // using the RingCircle/FillCircle integer disc test
        // (dx*dx + dy*dy <= r*r).
        // progress 0.0 submits (DrawError::None) and leaves the buffer
        // unchanged (radius/alpha zero); progress 1.0 covers the whole
        // bounds rect at full alpha. Forwarding the cmd preserves
        // rippleBounds/rippleProgress/rippleColor exactly; the Swapchain
        // rasterizes the wash.
        target.submit(cmd);
        break;
      case PrimitiveKind::ElevationShadow:
        // T4 (FR-GRAPHICS-010 / AC-GRAPHICS-029): ElevationShadow renders
        // a blurred rounded shadow of cmd.bounds/cmd.radius from
        // payload.elevationDp/payload.shadowBlur/payload.shadowColor.
        // The retained-path rasterizer (Swapchain) reuses the
        // gallery_present.cpp ShadowRounded math as the pixel-parity
        // reference: two offset soft passes (+2/+3 at a=28, +4/+6 at
        // a=18) clipped to the rounded silhouette
        // (dx*dx + dy*dy <= r*r per corner) and src-over blended
        // (BlendPixel: per-channel (src*a + dst*(255-a))/255) with the
        // shadowColor RGB; elevationDp == 0.0 submits (DrawError::None)
        // and leaves the buffer unchanged (no visible shadow).
        // Forwarding the cmd preserves bounds/radius/elevationDp/
        // shadowBlur/shadowColor exactly; the Swapchain rasterizes the
        // shadow under the card fill.
        target.submit(cmd);
        break;
      case PrimitiveKind::CircleArc:
        // T5 (FR-GRAPHICS-010 / AC-GRAPHICS-029): CircleArc renders a
        // ring/arc inside cmd.bounds from payload.arcStartDeg/
        // payload.arcSweepDeg/payload.strokeWidth/payload.arcColor. The
        // retained-path rasterizer (Swapchain) reuses the
        // gallery_present.cpp RingCircle/DrawArc math as the
        // pixel-parity reference: integer ring test
        // (d2 <= r*r && d2 >= inner*inner with inner = r - thick) for a
        // full 360 sweep (closed ring), and the DrawArc polyline-of-discs
        // walk (steps = clamp(sweepDeg*2, 8, 720), disc per step) for a
        // partial sweep; arcSweepDeg == 360.0 submits (DrawError::None)
        // and closes the ring (never clamped to an open arc).
        // Forwarding the cmd preserves bounds/arcStartDeg/arcSweepDeg/
        // strokeWidth/arcColor exactly; the Swapchain rasterizes the
        // ring/arc in bounds.
        target.submit(cmd);
        break;
      case PrimitiveKind::VectorIcon:
        // T6 (FR-GRAPHICS-010 / AC-GRAPHICS-029): VectorIcon resolves the
        // registry-supplied assetId and renders the tinted glyph in
        // cmd.bounds with payload.tintColor. assetId == 0 is the
        // unresolvable sentinel (the field default, never a valid
        // registry-supplied id; no filesystem/network on the draw path) and
        // fails closed per RULE-RUNTIME-001: report UnknownPrimitive and
        // record nothing (no substitute box). Forwarding a nonzero-id cmd
        // preserves bounds/assetId/tintColor exactly; the Swapchain
        // rasterizes the tinted glyph in bounds.
        if (cmd.payload.assetId == 0) {
          result = DrawError::UnknownPrimitive;
          break;
        }
        target.submit(cmd);
        break;
      case PrimitiveKind::ShapedText:
        // T7 (FR-GRAPHICS-010 / AC-GRAPHICS-029): ShapedText renders the
        // referenced glyph run in cmd.bounds with payload.textColor.
        // glyphRunId == 0 is the dangling sentinel (the field default,
        // never a valid supplied handle; shaping/atlas supply is
        // BUG-M3-FIDELITY-005's ownership) and fails closed per
        // RULE-RUNTIME-001: report UnknownPrimitive and record nothing
        // (no substitute box). Forwarding a nonzero-id cmd preserves
        // bounds/glyphRunId/textColor exactly; the Swapchain rasterizes
        // the referenced run in bounds.
        if (cmd.payload.glyphRunId == 0) {
          result = DrawError::UnknownPrimitive;
          break;
        }
        // T11 (FR-TEXT-016): forwarding itself is unchanged; the retained
        // run resolves through the shaping cache at rasterization time
        // (unknown/dangling handles fail closed there, never synthesized).
        target.submit(cmd);
        break;
      case PrimitiveKind::OverlayScrim:
        // T8 (FR-GRAPHICS-010 / AC-GRAPHICS-029): OverlayScrim renders a
        // dim layer over cmd.bounds from payload.scrimColor/
        // payload.scrimAlpha, under subsequent overlay content. The
        // retained-path rasterizer (Swapchain) src-over blends
        // (gallery_present.cpp BlendPixel: per-channel
        // (src*a + dst*(255-a))/255) the scrimColor RGB at
        // a = round(255*scrimAlpha) across the full bounds rect;
        // scrimAlpha == 0.0 submits (DrawError::None) and leaves the
        // buffer unchanged (no visible dim). Forwarding the cmd preserves
        // bounds/scrimColor/scrimAlpha exactly; the Swapchain rasterizes
        // the dim in submission order, so later overlay content draws
        // over it.
        target.submit(cmd);
        break;
      default:
        // RULE-RUNTIME-001: never substitute a default box for an
        // undeclared primitive; report and skip the unknown node.
        result = DrawError::UnknownPrimitive;
        break;
    }
  }
  return result;
}

std::string Renderer::lastError() const { return lastError_; }

}  // namespace runtime
