// Gallery bring-up presenter (FR-RUNTIME-009 / FR-M3-007, AC-M3-015).
//
// Device reality: the JNI host entry carries the MUI1 tree asset, but no JS
// engine runs on device, so dynamic App.tsx expressions cannot evaluate.
// The static Showcase root (literal props + literal text children) carries
// the whole gallery with no evaluation — parse it here, lay it out through
// LayoutEngine (the in-repo flexbox-subset seam; a Yoga adapter sits behind
// it per layout.h), and rasterize pages of M3 rows into the ANativeWindow
// RGBA_8888 buffer. No View creation, no Java drawing: pixels stay native.
//
// Fail-closed throughout: bad envelope, bad JSON shape, or no rows yield an
// empty row set plus a diagnostic (the JNI layer then draws a diagnostic
// screen, never a blank one). Nothing here throws across the boundary.

#include "runtime/gallery_present.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "runtime/layout.h"
#include "runtime/renderer.h"
#include "runtime/tree.h"

namespace runtime {
namespace {

// ---- MUI1 envelope ----

bool ReadEnvelope(const void* data, std::size_t len, const char*& json,
                  std::size_t& jsonLen, std::string* error) {
  json = nullptr;
  jsonLen = 0;
  if (data == nullptr || len < 12) {
    if (error != nullptr) *error = "envelope too short";
    return false;
  }
  const unsigned char* b = static_cast<const unsigned char*>(data);
  if (std::memcmp(b, "MUI1", 4) != 0) {
    if (error != nullptr) *error = "bad magic (not MUI1)";
    return false;
  }
  if (b[4] != 1) {
    if (error != nullptr) *error = "unsupported envelope version";
    return false;
  }
  std::uint32_t declared = static_cast<std::uint32_t>(b[8]) |
                           (static_cast<std::uint32_t>(b[9]) << 8) |
                           (static_cast<std::uint32_t>(b[10]) << 16) |
                           (static_cast<std::uint32_t>(b[11]) << 24);
  if (declared == 0 || 12u + declared > len) {
    if (error != nullptr) *error = "envelope length out of range";
    return false;
  }
  json = reinterpret_cast<const char*>(b + 12);
  jsonLen = declared;
  return true;
}

// ---- Minimal read-only JSON scan (string-aware, no input mutation) ----

const char* SkipWs(const char* p, const char* end) {
  while (p < end &&
         (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    ++p;
  }
  return p;
}

// Parse a JSON string at *p (expects '"'), appending the decoded value to
// out and returning the position past the closing quote (or nullptr).
const char* ParseJsonString(const char* p, const char* end, std::string& out) {
  if (p >= end || *p != '"') return nullptr;
  ++p;
  while (p < end) {
    char c = *p;
    if (c == '"') return p + 1;
    if (c != '\\') {
      out.push_back(c);
      ++p;
      continue;
    }
    ++p;
    if (p >= end) return nullptr;
    switch (*p) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'n': out.push_back('\n'); break;
      case 't': out.push_back('\t'); break;
      case 'r': out.push_back('\r'); break;
      case 'u': {
        // Minimal \uXXXX: ASCII range maps directly, else '?'.
        if (p + 4 >= end) return nullptr;
        unsigned code = 0;
        for (int i = 1; i <= 4; ++i) {
          char h = p[i];
          code <<= 4;
          if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
          else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
          else return nullptr;
        }
        out.push_back(code < 128 ? static_cast<char>(code) : '?');
        p += 4;
        break;
      }
      default: out.push_back(*p); break;
    }
    ++p;
  }
  return nullptr;
}

// Find `"key"` then ':' starting at p; on success *valPos is the value start
// (after ':') and the key span is returned via spanEnd (past closing quote).
const char* FindKey(const char* p, const char* end, const char* key,
                    const char*& valPos) {
  std::size_t klen = std::strlen(key);
  for (; p < end; ++p) {
    if (*p != '"') continue;
    if (static_cast<std::size_t>(end - p) < klen + 2) return nullptr;
    if (std::memcmp(p + 1, key, klen) != 0 || p[1 + klen] != '"') continue;
    const char* q = SkipWs(p + 2 + klen, end);
    if (q >= end || *q != ':') continue;
    valPos = SkipWs(q + 1, end);
    return p;
  }
  return nullptr;
}

// Match the closing bracket for the opener at *p ('{' or '['), string-aware.
const char* MatchBracket(const char* p, const char* end) {
  if (p >= end || (*p != '{' && *p != '[')) return nullptr;
  char open = *p;
  char close = (open == '{') ? '}' : ']';
  int depth = 0;
  bool inStr = false;
  for (const char* q = p; q < end; ++q) {
    char c = *q;
    if (inStr) {
      if (c == '\\') {
        ++q;
        continue;
      }
      if (c == '"') inStr = false;
      continue;
    }
    if (c == '"') {
      inStr = true;
    } else if (c == open) {
      ++depth;
    } else if (c == close) {
      if (--depth == 0) return q;
    }
  }
  return nullptr;
}

// Split the top-level elements of the array starting at '[' (already
// located). Each element span [s, e) is appended for '{'-objects only.
bool SplitTopObjects(const char* arr, const char* end,
                     std::vector<std::pair<const char*, const char*>>& out) {
  if (arr >= end || *arr != '[') return false;
  const char* arrEnd = MatchBracket(arr, end);
  if (arrEnd == nullptr) return false;
  const char* q = arr + 1;
  while (q < arrEnd) {
    q = SkipWs(q, arrEnd);
    if (q >= arrEnd || *q == ']') break;
    if (*q == ',') {
      ++q;
      continue;
    }
    if (*q == '{') {
      const char* objEnd = MatchBracket(q, end);
      if (objEnd == nullptr || objEnd > arrEnd) return false;
      out.push_back({q, objEnd + 1});
      q = objEnd + 1;
      continue;
    }
    return false;  // unexpected element shape
  }
  return true;
}

// ---- 5x7 bitmap font (ASCII subset, mixed case) ----

struct Glyph {
  char ch;
  const char* rows[7];
};

const Glyph kGlyphs[] = {
    {' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
    {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
    {'"', {".#.#.", ".#.#.", ".....", ".....", ".....", ".....", "....."}},
    {'%', {"##..#", "##..#", "..#..", "..#..", "..#..", ".#..#", ".#..#"}},
    {'\'', {"..#..", "..#..", ".....", ".....", ".....", ".....", "....."}},
    {'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {',', {".....", ".....", ".....", ".....", "..#..", "..#..", ".#..."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'.', {".....", ".....", ".....", ".....", ".....", "..#..", "..#.."}},
    {'/', {"....#", "...#.", "...#.", "..#..", "..#..", ".#...", "#...."}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", "#####"}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
    {':', {".....", "..#..", ".....", ".....", ".....", "..#..", "..#.."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".####", "#....", "#....", "#....", "#....", "#....", ".####"}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".####", "#....", "#....", "#.###", "#...#", "#...#", ".###."}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "#####", "#####", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "##..#", "##..#", "#.#.#", "#..##", "#..##", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#####", "#####", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'a', {".....", ".....", ".###.", "....#", ".####", "#...#", ".####"}},
    {'b', {"#....", "#....", "#.###", "##..#", "#...#", "#...#", "#.###"}},
    {'c', {".....", ".....", ".####", "#....", "#....", "#....", ".####"}},
    {'d', {"....#", "....#", ".####", "#...#", "#...#", "#...#", ".####"}},
    {'e', {".....", ".....", ".###.", "#...#", "#####", "#....", ".###."}},
    {'f', {"...##", "...#.", "...#.", "#####", "...#.", "...#.", "...#."}},
    {'g', {".....", ".....", ".###.", "#...#", "#...#", ".####", "....#"}},
    {'h', {"#....", "#....", "#.###", "##..#", "#...#", "#...#", "#...#"}},
    {'i', {"..#..", ".....", ".##..", "..#..", "..#..", "..#..", ".###."}},
    {'j', {"....#", ".....", "...##", "...#.", "...#.", "#...#", ".###."}},
    {'k', {"#....", "#....", "#..#.", "#.#..", "##...", "#..#.", "#..#."}},
    {'l', {".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'m', {".....", ".....", "##.#.", "#.#.#", "#...#", "#...#", "#...#"}},
    {'n', {".....", ".....", "#.###", "##..#", "#...#", "#...#", "#...#"}},
    {'o', {".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###."}},
    {'p', {".....", ".....", "#.###", "##..#", "##..#", "#.###", "#...."}},
    {'q', {".....", ".....", ".##.#", "#...#", "#...#", ".###.", "....#"}},
    {'r', {".....", ".....", "#.###", "##..#", "#....", "#....", "#...."}},
    {'s', {".....", ".....", ".####", "#....", ".###.", "....#", "####."}},
    {'t', {"...#.", "...#.", "#####", "...#.", "...#.", "...#.", "..##."}},
    {'u', {".....", ".....", "#...#", "#...#", "#...#", "#...#", ".####"}},
    {'v', {".....", ".....", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'w', {".....", ".....", "#...#", "#...#", "#.#.#", "#####", ".#.#."}},
    {'x', {".....", ".....", "#..#.", "#..#.", ".##..", "#..#.", "#..#."}},
    {'y', {".....", ".....", "#...#", "#...#", ".###.", "....#", ".###."}},
    {'z', {".....", ".....", "#####", "...#.", "..#..", ".#...", "#####"}},
};

const Glyph* FindGlyph(char c) {
  for (const Glyph& g : kGlyphs) {
    if (g.ch == c) return &g;
  }
  for (const Glyph& g : kGlyphs) {
    if (g.ch == '?') return &g;
  }
  return nullptr;
}

// ---- M3 baseline roles (light + dark) ----
// ANativeWindow RGBA_8888 orders bytes R,G,B,A in memory, so on
// little-endian the uint32 word reads AABBGGRR, never 0xAARRGGBB. Every
// role below goes through RGBA() to stay correct on the wire.
constexpr std::uint32_t RGBA(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                             std::uint8_t a) {
  return static_cast<std::uint32_t>(r) |
         (static_cast<std::uint32_t>(g) << 8) |
         (static_cast<std::uint32_t>(b) << 16) |
         (static_cast<std::uint32_t>(a) << 24);
}

constexpr std::uint32_t kLightSurface = RGBA(0xFE, 0xF7, 0xFF, 0xFF);
constexpr std::uint32_t kLightOnSurface = RGBA(0x1D, 0x1B, 0x20, 0xFF);
constexpr std::uint32_t kLightPrimary = RGBA(0x67, 0x50, 0xA4, 0xFF);
constexpr std::uint32_t kLightOnPrimary = RGBA(0xFF, 0xFF, 0xFF, 0xFF);
constexpr std::uint32_t kDarkSurface = RGBA(0x14, 0x12, 0x18, 0xFF);
constexpr std::uint32_t kDarkOnSurface = RGBA(0xE6, 0xE0, 0xE9, 0xFF);
constexpr std::uint32_t kDarkPrimary = RGBA(0xD0, 0xBC, 0xFF, 0xFF);
constexpr std::uint32_t kDarkOnPrimary = RGBA(0x38, 0x1E, 0x72, 0xFF);
constexpr std::uint32_t kFooterBg = RGBA(0x1D, 0x1B, 0x20, 0xFF);
constexpr std::uint32_t kFooterFg = RGBA(0xFF, 0xFF, 0xFF, 0xFF);
constexpr std::uint32_t kErrorBg = RGBA(0xBA, 0x1A, 0x1A, 0xFF);

// ---- Raster helpers ----

void FillRect(std::uint32_t* px, int stride, int w, int h, int x, int y, int rw,
              int rh, std::uint32_t color) {
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + rw > w ? w : x + rw;
  int y1 = y + rh > h ? h : y + rh;
  for (int yy = y0; yy < y1; ++yy) {
    std::uint32_t* row = px + yy * stride;
    for (int xx = x0; xx < x1; ++xx) row[xx] = color;
  }
}

void DrawText(std::uint32_t* px, int stride, int w, int h, const char* s,
              int x, int y, int scale, std::uint32_t color) {
  int cx = x;
  for (const char* p = s; *p != '\0'; ++p) {
    const Glyph* g = FindGlyph(*p);
    if (g == nullptr) {
      cx += 6 * scale;
      continue;
    }
    for (int r = 0; r < 7; ++r) {
      for (int c = 0; c < 5; ++c) {
        if (g->rows[r][c] != '#') continue;
        FillRect(px, stride, w, h, cx + c * scale, y + r * scale, scale,
                 scale, color);
      }
    }
    cx += 6 * scale;
  }
}

int TextWidth(const char* s, int scale) {
  int n = 0;
  for (const char* p = s; *p != '\0'; ++p) ++n;
  if (n == 0) return 0;
  return n * 6 * scale - scale;
}

void DrawTextCentered(std::uint32_t* px, int stride, int w, int h,
                      const char* s, int cx, int y, int scale,
                      std::uint32_t color) {
  DrawText(px, stride, w, h, s, cx - TextWidth(s, scale) / 2, y, scale,
           color);
}

// Src-over blend of a solid RGBA source over the buffer (for soft shadows;
// the buffer itself stays opaque).
void BlendPixel(std::uint32_t* px, int stride, int w, int h, int x, int y,
                std::uint8_t r, std::uint8_t g, std::uint8_t b,
                std::uint8_t a) {
  if (x < 0 || y < 0 || x >= w || y >= h) return;
  std::uint32_t dst = px[y * stride + x];
  unsigned dr = dst & 0xFFu;
  unsigned dg = (dst >> 8) & 0xFFu;
  unsigned db = (dst >> 16) & 0xFFu;
  unsigned ia = 255u - a;
  unsigned nr = (r * a + dr * ia) / 255u;
  unsigned ng = (g * a + dg * ia) / 255u;
  unsigned nb = (b * a + db * ia) / 255u;
  px[y * stride + x] = nr | (ng << 8) | (nb << 16) | 0xFF000000u;
}

void ShadowRounded(std::uint32_t* px, int stride, int w, int h, int x, int y,
                   int rw, int rh, int r) {
  // Two offset soft passes approximate an M3 elevation shadow.
  for (int pass = 0; pass < 2; ++pass) {
    int ox = x + 2 + pass * 2;
    int oy = y + 3 + pass * 3;
    std::uint8_t a = pass == 0 ? 28 : 18;
    for (int yy = oy; yy < oy + rh; ++yy) {
      for (int xx = ox; xx < ox + rw; ++xx) {
        // Keep the rounded silhouette: skip outside the rounded shape.
        int dx = xx < ox + r ? (ox + r - xx)
                 : xx >= ox + rw - r ? (xx - (ox + rw - r) + 1)
                                     : 0;
        int dy = yy < oy + r ? (oy + r - yy)
                 : yy >= oy + rh - r ? (yy - (oy + rh - r) + 1)
                                     : 0;
        if (dx * dx + dy * dy > r * r) continue;
        BlendPixel(px, stride, w, h, xx, yy, 0, 0, 0, a);
      }
    }
  }
}

void FillCircle(std::uint32_t* px, int stride, int w, int h, int cx, int cy,
                int r, std::uint32_t color) {
  if (r <= 0) return;
  for (int yy = cy - r; yy <= cy + r; ++yy) {
    for (int xx = cx - r; xx <= cx + r; ++xx) {
      int dx = xx - cx;
      int dy = yy - cy;
      if (dx * dx + dy * dy <= r * r) {
        if (xx >= 0 && yy >= 0 && xx < w && yy < h) {
          px[yy * stride + xx] = color;
        }
      }
    }
  }
}

void RingCircle(std::uint32_t* px, int stride, int w, int h, int cx, int cy,
                int r, int thick, std::uint32_t color) {
  if (r <= 0 || thick <= 0) return;
  int inner = r - thick;
  if (inner < 0) inner = 0;
  for (int yy = cy - r; yy <= cy + r; ++yy) {
    for (int xx = cx - r; xx <= cx + r; ++xx) {
      int d2 = (xx - cx) * (xx - cx) + (yy - cy) * (yy - cy);
      if (d2 <= r * r && d2 >= inner * inner) {
        if (xx >= 0 && yy >= 0 && xx < w && yy < h) {
          px[yy * stride + xx] = color;
        }
      }
    }
  }
}

void FillRoundedRect(std::uint32_t* px, int stride, int w, int h, int x,
                     int y, int rw, int rh, int r, std::uint32_t color) {
  if (rw <= 0 || rh <= 0) return;
  int maxR = rw < rh ? rw : rh;
  maxR /= 2;
  if (r > maxR) r = maxR;
  if (r < 0) r = 0;
  if (r == 0) {
    FillRect(px, stride, w, h, x, y, rw, rh, color);
    return;
  }
  FillRect(px, stride, w, h, x + r, y, rw - 2 * r, rh, color);
  FillRect(px, stride, w, h, x, y + r, rw, rh - 2 * r, color);
  for (int q = 0; q < 4; ++q) {
    for (int dy = 0; dy < r; ++dy) {
      for (int dx = 0; dx < r; ++dx) {
        int ddx = (q % 2 == 0) ? (r - dx) : (dx + 1);
        int ddy = (q < 2) ? (r - dy) : (dy + 1);
        if (ddx * ddx + ddy * ddy > r * r) continue;
        int xx = (q % 2 == 0) ? (x + dx) : (x + rw - r + dx);
        int yy = (q < 2) ? (y + dy) : (y + rh - r + dy);
        if (xx >= 0 && yy >= 0 && xx < w && yy < h) {
          px[yy * stride + xx] = color;
        }
      }
    }
  }
}

void StrokeRoundedRect(std::uint32_t* px, int stride, int w, int h, int x,
                       int y, int rw, int rh, int r, int t,
                       std::uint32_t color, std::uint32_t bg) {
  if (t < 1) t = 1;
  FillRoundedRect(px, stride, w, h, x, y, rw, rh, r, color);
  FillRoundedRect(px, stride, w, h, x + t, y + t, rw - 2 * t, rh - 2 * t,
                  r > t ? r - t : 0, bg);
}

void DrawLine(std::uint32_t* px, int stride, int w, int h, int x0, int y0,
              int x1, int y1, int thick, std::uint32_t color) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int steps = dx < 0 ? -dx : dx;
  int sy = dy < 0 ? -dy : dy;
  if (sy > steps) steps = sy;
  if (steps == 0) {
    FillRect(px, stride, w, h, x0 - thick / 2, y0 - thick / 2, thick, thick,
             color);
    return;
  }
  for (int i = 0; i <= steps; ++i) {
    int xx = x0 + dx * i / steps;
    int yy = y0 + dy * i / steps;
    FillRect(px, stride, w, h, xx - thick / 2, yy - thick / 2, thick, thick,
             color);
  }
}

void DrawArc(std::uint32_t* px, int stride, int w, int h, int cx, int cy,
             int r, float startDeg, float sweepDeg, int thick,
             std::uint32_t color) {
  if (r <= 0 || thick <= 0 || sweepDeg <= 0) return;
  constexpr float kPi = 3.14159265f;
  int steps = static_cast<int>(sweepDeg * 2.0f);
  if (steps < 8) steps = 8;
  if (steps > 720) steps = 720;
  for (int i = 0; i <= steps; ++i) {
    float ang = (startDeg + sweepDeg * i / steps) * kPi / 180.0f;
    int xx = cx + static_cast<int>(r * std::cos(ang));
    int yy = cy + static_cast<int>(r * std::sin(ang));
    FillCircle(px, stride, w, h, xx, yy, thick / 2 + 1, color);
  }
}

}  // namespace

std::vector<GalleryRow> ParseGalleryRows(const void* data, std::size_t len,
                                         std::string* error) {
  std::vector<GalleryRow> rows;
  try {
    const char* json = nullptr;
    std::size_t jsonLen = 0;
    if (!ReadEnvelope(data, len, json, jsonLen, error)) return rows;
    const char* begin = json;
    const char* end = json + jsonLen;

    // Locate the roots array, then split its top-level root objects.
    const char* rootsVal = nullptr;
    if (FindKey(begin, end, "roots", rootsVal) == nullptr || rootsVal >= end ||
        *rootsVal != '[') {
      if (error != nullptr) *error = "IR has no roots array";
      return rows;
    }
    std::vector<std::pair<const char*, const char*>> rootObjs;
    if (!SplitTopObjects(rootsVal, end, rootObjs) || rootObjs.empty()) {
      if (error != nullptr) *error = "IR roots array is empty/unreadable";
      return rows;
    }

    // Prefer the static Showcase root; otherwise the first root.
    std::pair<const char*, const char*> chosen = rootObjs[0];
    for (const auto& span : rootObjs) {
      const char* fileVal = nullptr;
      if (FindKey(span.first, span.second, "file", fileVal) != nullptr &&
          fileVal < span.second && *fileVal == '"') {
        std::string file;
        if (ParseJsonString(fileVal, span.second, file) != nullptr &&
            file == "src/Showcase.tsx") {
          chosen = span;
          break;
        }
      }
    }

    // Split the chosen root's children array into child objects.
    const char* childrenVal = nullptr;
    if (FindKey(chosen.first, chosen.second, "children", childrenVal) ==
            nullptr ||
        childrenVal >= chosen.second || *childrenVal != '[') {
      if (error != nullptr) *error = "Showcase root has no children array";
      return rows;
    }
    std::vector<std::pair<const char*, const char*>> childObjs;
    if (!SplitTopObjects(childrenVal, chosen.second, childObjs)) {
      if (error != nullptr) *error = "Showcase children unreadable";
      return rows;
    }

    std::vector<std::string> textRows;
    std::vector<GalleryRow> componentRows;
    bool dark = false;
    for (const auto& span : childObjs) {
      const char* typeVal = nullptr;
      if (FindKey(span.first, span.second, "type", typeVal) == nullptr ||
          typeVal >= span.second || *typeVal != '"') {
        continue;
      }
      std::string type;
      if (ParseJsonString(typeVal, span.second, type) == nullptr) continue;
      if (type == "#text") {
        const char* textVal = nullptr;
        if (FindKey(span.first, span.second, "value", textVal) == nullptr ||
            textVal >= span.second || *textVal != '"') {
          continue;
        }
        std::string value;
        if (ParseJsonString(textVal, span.second, value) == nullptr) continue;
        // Structural lowerer leaks source lines (comments, code) as text;
        // only literal gallery copy becomes rows. Gallery labels always
        // start with a letter or digit; stray closers like "});" do not.
        if (value.empty()) continue;
        char first = value[0];
        bool alnum = (first >= 'A' && first <= 'Z') ||
                     (first >= 'a' && first <= 'z') ||
                     (first >= '0' && first <= '9');
        if (!alnum) continue;
        if (value == "M3 Gallery dark") dark = true;
        if (value == "M3 Gallery light") dark = false;
        GalleryRow row;
        row.label = value;
        row.dark = dark;
        row.header =
            value.compare(0, 10, "M3 Gallery") == 0;
        // Stash header state in the prefix: emit via ordered vectors below.
        if (row.header) {
          textRows.push_back(std::string("\x01") + value);
        } else {
          textRows.push_back(value);
        }
      } else {
        // Structural wrappers carry no visual props: <View> only groups,
        // <Text> tags hold expression children whose literal copy already
        // became #text rows. Rendering them would repeat ~100 identical
        // fallback cards, so they never become rows.
        if (type == "View" || type == "Text") continue;
        // Component inventory row from literal props only. Every literal
        // string prop is kept (quoted, brace-free so no evaluated
        // expression); the painter below dispatches on the type and reads
        // variant/label/size props back out of the stored description.
        const char* kPropKeys[] = {"label",   "title",    "headline",
                                  "message", "text",     "placeholder",
                                  "name",    "variant",  "size",
                                  "selected", "checked", "enabled",
                                  "value",   "index",    "icon",
                                  "tone",    "elevation", "orientation",
                                  "direction", "spacing", "open",
                                  "contentFit", "source"};
        const char* propsVal = nullptr;
        const char* propsEnd = nullptr;
        if (FindKey(span.first, span.second, "props", propsVal) != nullptr &&
            propsVal < span.second && *propsVal == '{') {
          propsEnd = MatchBracket(propsVal, span.second);
        }
        std::string desc = type;
        std::string picked;
        if (propsEnd != nullptr) {
          int kept = 0;
          for (const char* key : kPropKeys) {
            if (kept >= 10) break;
            const char* kv = nullptr;
            if (FindKey(propsVal, propsEnd + 1, key, kv) == nullptr ||
                kv > propsEnd || *kv != '"') {
              continue;
            }
            std::string v;
            const char* vEnd = ParseJsonString(kv, propsEnd + 1, v);
            if (vEnd == nullptr || vEnd > propsEnd) continue;
            if (v.find('{') != std::string::npos ||
                v.find('}') != std::string::npos) {
              continue;
            }
            desc += " ";
            desc += key;
            desc += "=";
            desc += v;
            if (picked.empty() &&
                (std::strcmp(key, "label") == 0 ||
                 std::strcmp(key, "title") == 0 ||
                 std::strcmp(key, "headline") == 0 ||
                 std::strcmp(key, "message") == 0 ||
                 std::strcmp(key, "text") == 0 ||
                 std::strcmp(key, "placeholder") == 0 ||
                 std::strcmp(key, "name") == 0)) {
              picked = v;
            }
            ++kept;
          }
        }
        if (desc.size() > 160) {
          desc.resize(160);
        }
        GalleryRow row;
        row.comp = type;
        row.label = picked.empty() ? desc : picked;
        row.desc = desc;
        row.dark = false;
        row.header = false;
        componentRows.push_back(row);
      }
    }

    // Component inventory first (the actual M3 gallery: every tier with its
    // documented variants), then the literal label sections. Text rows keep
    // their light/dark section order from the source.
    for (const GalleryRow& c : componentRows) {
      if (rows.size() >= 400) break;
      rows.push_back(c);
    }
    dark = false;
    for (const std::string& t : textRows) {
      GalleryRow row;
      if (!t.empty() && t[0] == '\x01') {
        row.label = t.substr(1);
        row.header = true;
        row.dark = (row.label == "M3 Gallery dark");
        dark = row.dark;
        if (row.label == "M3 Gallery light") dark = false;
      } else {
        row.label = t;
        row.dark = dark;
        row.header = false;
      }
      rows.push_back(row);
      if (rows.size() >= 400) break;
    }
    if (rows.empty() && error != nullptr) {
      *error = "Showcase root produced no gallery rows";
    }
    return rows;
  } catch (...) {
    rows.clear();
    if (error != nullptr) *error = "row parse failed (exception)";
    return rows;
  }
}

// Extra M3 roles used by the component painters below.
constexpr std::uint32_t kLightOnPrimaryContainer = RGBA(0x4F, 0x37, 0x8B, 0xFF);
constexpr std::uint32_t kLightSecondaryContainer = RGBA(0xE8, 0xDE, 0xF8, 0xFF);
constexpr std::uint32_t kLightTertiaryContainer = RGBA(0xFF, 0xD8, 0xE4, 0xFF);
constexpr std::uint32_t kLightOutline = RGBA(0x79, 0x74, 0x7E, 0xFF);
constexpr std::uint32_t kLightOutlineVariant = RGBA(0xCA, 0xC4, 0xD0, 0xFF);
constexpr std::uint32_t kLightSurfaceVariant = RGBA(0xE7, 0xE0, 0xEC, 0xFF);
constexpr std::uint32_t kLightOnSurfaceVariant = RGBA(0x49, 0x45, 0x4F, 0xFF);
constexpr std::uint32_t kLightError = RGBA(0xBA, 0x1A, 0x1A, 0xFF);
constexpr std::uint32_t kLightInverseSurface = RGBA(0x32, 0x2F, 0x35, 0xFF);

constexpr int kPad = 16;
constexpr int kGap = 8;
constexpr int kHeaderH = 84;
constexpr int kFooterH = 56;
constexpr int kScale = 2;

// Row heights by row kind. Text rows stay compact; every component row is
// tall enough for the drawn control plus its caption.
int RowHeight(const GalleryRow& row) {
  if (row.header) return 84;
  if (row.comp.empty()) return 64;
  const std::string& c = row.comp;
  const std::string& d = row.desc;
  if (c == "TopAppBar") {
    if (d.find("large") != std::string::npos) return 236;
    if (d.find("medium") != std::string::npos) return 196;
    return 156;
  }
  if (c == "Card" || c == "Dialog") return 190;
  if (c == "BottomSheet" || c == "NavigationDrawer") return 180;
  if (c == "Menu" || c == "Carousel") return 170;
  if (c == "ListItem") return 128;
  if (c == "NavigationBar" || c == "NavigationRail" ||
      c == "BottomAppBar" || c == "SearchBar" || c == "SnackbarHost" ||
      c == "Slider" || c == "LinearProgress" || c == "CircularProgress") {
    return 124;
  }
  return 104;
}

// Literal-prop lookup over the stored "Type k=v k=v" description.
bool PropIs(const std::string& desc, const char* key, const char* val) {
  std::string needle = std::string(" ") + key + "=" + val;
  return desc.find(needle) != std::string::npos;
}

std::string PropValue(const std::string& desc, const char* key) {
  std::string needle = std::string(" ") + key + "=";
  std::size_t at = desc.find(needle);
  if (at == std::string::npos) return "";
  at += needle.size();
  std::size_t end = desc.find(' ', at);
  if (end == std::string::npos) return desc.substr(at);
  return desc.substr(at, end - at);
}

double PropDouble(const std::string& desc, const char* key, double fallback) {
  std::string v = PropValue(desc, key);
  if (v.empty()) return fallback;
  try {
    return std::stod(v);
  } catch (...) {
    return fallback;
  }
}

// Greedy page packing over variable row heights. Returns the start index of
// every page; page i covers [starts[i], starts[i+1]).
std::vector<int> PageStarts(const std::vector<GalleryRow>& rows, int height) {
  std::vector<int> starts;
  starts.push_back(0);
  int avail = height - kHeaderH - kFooterH - kPad;
  int used = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    int h = RowHeight(rows[i]);
    int need = (used == 0) ? h : h + kGap;
    if (used != 0 && used + need > avail) {
      starts.push_back(static_cast<int>(i));
      used = h;
    } else {
      used += need;
    }
  }
  starts.push_back(static_cast<int>(rows.size()));
  return starts;
}

int GalleryPageCount(const std::vector<GalleryRow>& rows, int height) {
  if (rows.empty()) return 1;
  if (height <= kHeaderH + kFooterH + 64) return 1;
  std::vector<int> starts = PageStarts(rows, height);
  int pages = static_cast<int>(starts.size()) - 1;
  return pages < 1 ? 1 : pages;
}

constexpr std::uint32_t kCardBg = RGBA(0xF7, 0xF2, 0xFA, 0xFF);
constexpr std::uint32_t kWhite = RGBA(0xFF, 0xFF, 0xFF, 0xFF);

// Caption for a component row: the picked human label, or a short
// "Type key=value" fallback when the row carries no label-ish prop.
std::string ShortCaption(const GalleryRow& row) {
  if (!row.label.empty() && row.label != row.desc) return row.label;
  std::string variant = PropValue(row.desc, "variant");
  std::string out = row.comp;
  if (!variant.empty()) {
    out += " ";
    out += variant;
  } else {
    std::string sel = PropValue(row.desc, "selected");
    if (!sel.empty()) {
      out += " ";
      out += sel;
    }
  }
  return out;
}

void PaintCaption(std::uint32_t* px, int stride, int w, int h, int bx, int by,
                  int bw, int bh, const std::string& caption) {
  int maxChars = (bw - 32) / (6 * kScale);
  if (maxChars < 4) maxChars = 4;
  std::string c = caption;
  if (static_cast<int>(c.size()) > maxChars) c.resize(maxChars);
  DrawTextCentered(px, stride, w, h, c.c_str(), bx + bw / 2,
                   by + bh - 14 - kScale * 7, kScale, kLightOnSurfaceVariant);
}

// One M3 component miniature inside its card. (bx,by,bw,bh) is the card
// rect; the painter draws the card background plus the control.
inline int cy0(int y0, int size) { return y0 + size / 2; }

void PaintComponent(std::uint32_t* px, int stride, int w, int h, int bx,
                    int by, int bw, int bh, const GalleryRow& row) {
  const std::string& c = row.comp;
  const std::string& d = row.desc;
  // Card backdrop shared by every inventory row.
  ShadowRounded(px, stride, w, h, bx, by, bw, bh, 20);
  FillRoundedRect(px, stride, w, h, bx, by, bw, bh, 20, kCardBg);
  int cx = bx + bw / 2;

  if (c == "Button") {
    std::string variant = PropValue(d, "variant");
    if (variant.empty()) variant = "filled";
    std::string label = ShortCaption(row);
    int pillH = 56;
    int pillW = TextWidth(label.c_str(), kScale) + 72;
    if (pillW > bw - 32) pillW = bw - 32;
    if (pillW < 120) pillW = 120;
    int x0 = cx - pillW / 2;
    int y0 = by + (bh - 20 - pillH) / 2;
    if (variant == "filled") {
      FillRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 28,
                      kLightPrimary);
      DrawTextCentered(px, stride, w, h, label.c_str(), cx, y0 + 21, kScale,
                       kLightOnPrimary);
    } else if (variant == "tonal") {
      FillRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 28,
                      kLightSecondaryContainer);
      DrawTextCentered(px, stride, w, h, label.c_str(), cx, y0 + 21, kScale,
                       kLightOnPrimaryContainer);
    } else if (variant == "outlined") {
      StrokeRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 28, 2,
                        kLightPrimary, kCardBg);
      DrawTextCentered(px, stride, w, h, label.c_str(), cx, y0 + 21, kScale,
                       kLightPrimary);
    } else if (variant == "text") {
      DrawTextCentered(px, stride, w, h, label.c_str(), cx, y0 + 21, kScale,
                       kLightPrimary);
    } else {  // elevated
      ShadowRounded(px, stride, w, h, x0, y0, pillW, pillH, 28);
      FillRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 28, kWhite);
      DrawTextCentered(px, stride, w, h, label.c_str(), cx, y0 + 21, kScale,
                       kLightPrimary);
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Button " + variant);
    return;
  }

  if (c == "IconButton" || c == "Fab" || c == "ExtendedFab") {
    std::string variant = PropValue(d, "variant");
    std::string label = PropValue(d, "label");
    std::string icon = PropValue(d, "icon");
    bool extended = (c == "ExtendedFab");
    int size = (c == "Fab") ? 72 : 56;
    int pillW = extended ? TextWidth(label.c_str(), kScale) + 96 : size;
    if (pillW > bw - 32) pillW = bw - 32;
    int x0 = cx - pillW / 2;
    int y0 = by + (bh - 20 - size) / 2;
    std::uint32_t bg = (variant == "filled") ? kLightPrimary
                                             : kLightSecondaryContainer;
    std::uint32_t dot = (variant == "filled") ? kLightOnPrimary
                                               : kLightOnPrimaryContainer;
    if (extended) {
      FillRoundedRect(px, stride, w, h, x0, y0, pillW, size, size / 2, bg);
      FillCircle(px, stride, w, h, x0 + 34, y0 + size / 2, 9, dot);
      DrawText(px, stride, w, h, label.c_str(), x0 + 52, y0 + size / 2 - 7,
               kScale, dot == kLightOnPrimary ? kLightOnPrimary
                                              : kLightOnPrimaryContainer);
    } else {
      if (c == "Fab") {
        FillRoundedRect(px, stride, w, h, x0, y0, size, size, 18, bg);
        DrawLine(px, stride, w, h, cx - 10, cy0(y0, size), cx + 10,
                 cy0(y0, size), 5, dot);
        DrawLine(px, stride, w, h, cx, cy0(y0, size) - 10, cx,
                 cy0(y0, size) + 10, 5, dot);
      } else {
        if (variant.empty()) {
          RingCircle(px, stride, w, h, cx, cy0(y0, size), 26, 3,
                     kLightOutline);
          FillCircle(px, stride, w, h, cx, cy0(y0, size), 9,
                     kLightOnSurfaceVariant);
        } else {
          FillCircle(px, stride, w, h, cx, cy0(y0, size), 26, bg);
          FillCircle(px, stride, w, h, cx, cy0(y0, size), 9, dot);
        }
      }
    }
    if (!label.empty() && extended) {
      PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    } else if (!icon.empty()) {
      PaintCaption(px, stride, w, h, bx, by, bw, bh, c + " " + icon);
    } else {
      PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    }
    (void)icon;
    return;
  }

  if (c == "Card") {
    std::string variant = PropValue(d, "variant");
    if (variant.empty()) variant = "elevated";
    int cw = bw - 96;
    if (cw > 460) cw = 460;
    int chh = bh - 56;
    int x0 = cx - cw / 2;
    int y0 = by + 12;
    if (variant == "elevated") {
      ShadowRounded(px, stride, w, h, x0, y0, cw, chh, 16);
      FillRoundedRect(px, stride, w, h, x0, y0, cw, chh, 16, kWhite);
    } else if (variant == "filled") {
      FillRoundedRect(px, stride, w, h, x0, y0, cw, chh, 16,
                      kLightSurfaceVariant);
    } else {
      StrokeRoundedRect(px, stride, w, h, x0, y0, cw, chh, 16, 2,
                        kLightOutlineVariant, kCardBg);
    }
    DrawTextCentered(px, stride, w, h, variant.c_str(), cx,
                     y0 + chh / 2 - 7, kScale, kLightOnSurfaceVariant);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Card " + variant);
    return;
  }

  if (c == "Chip") {
    std::string variant = PropValue(d, "variant");
    std::string label = ShortCaption(row);
    bool selected = PropIs(d, "selected", "true");
    int pillH = 52;
    int pillW = TextWidth(label.c_str(), kScale) + 88;
    if (pillW > bw - 32) pillW = bw - 32;
    int x0 = cx - pillW / 2;
    int y0 = by + (bh - 20 - pillH) / 2;
    if (selected) {
      FillRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 14,
                      kLightSecondaryContainer);
      DrawLine(px, stride, w, h, x0 + 20, y0 + 27, x0 + 27, y0 + 34, 4,
               kLightOnPrimaryContainer);
      DrawLine(px, stride, w, h, x0 + 27, y0 + 34, x0 + 38, y0 + 18, 4,
               kLightOnPrimaryContainer);
      DrawText(px, stride, w, h, label.c_str(), x0 + 48, y0 + 19, kScale,
               kLightOnPrimaryContainer);
    } else {
      StrokeRoundedRect(px, stride, w, h, x0, y0, pillW, pillH, 14, 2,
                        kLightOutlineVariant, kCardBg);
      FillCircle(px, stride, w, h, x0 + 30, y0 + pillH / 2, 8,
                 kLightOnSurfaceVariant);
      DrawText(px, stride, w, h, label.c_str(), x0 + 46, y0 + 19, kScale,
               kLightOnSurface);
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Chip " + variant);
    return;
  }

  if (c == "Checkbox" || c == "Radio" || c == "Switch") {
    bool on = PropIs(d, "checked", "true") || PropIs(d, "selected", "true");
    int ymid = by + (bh - 20) / 2;
    std::string name = c + " ";
    if (c == "Switch") {
      name += on ? "on" : "off";
    } else if (c == "Radio") {
      name += on ? "selected" : "unselected";
    } else {
      name += on ? "checked" : "unchecked";
    }
    if (c == "Checkbox") {
      int s = 30;
      int x0 = cx - 90;
      if (on) {
        FillRoundedRect(px, stride, w, h, x0, ymid - s / 2, s, s, 7,
                        kLightPrimary);
        DrawLine(px, stride, w, h, x0 + 7, ymid, x0 + 13, ymid + 6, 4,
                 kLightOnPrimary);
        DrawLine(px, stride, w, h, x0 + 13, ymid + 6, x0 + 23, ymid - 8, 4,
                 kLightOnPrimary);
      } else {
        StrokeRoundedRect(px, stride, w, h, x0, ymid - s / 2, s, s, 7, 3,
                          kLightOutline, kCardBg);
      }
      DrawText(px, stride, w, h, on ? "Checked" : "Unchecked", x0 + 44,
               ymid - 7, kScale, kLightOnSurface);
    } else if (c == "Radio") {
      int x0 = cx - 90;
      RingCircle(px, stride, w, h, x0 + 15, ymid, 14, on ? 4 : 3,
                 on ? kLightPrimary : kLightOutline);
      if (on) FillCircle(px, stride, w, h, x0 + 15, ymid, 7, kLightPrimary);
      DrawText(px, stride, w, h, on ? "Selected" : "Unselected", x0 + 44,
               ymid - 7, kScale, kLightOnSurface);
    } else {
      int tw = 60;
      int th = 34;
      int x0 = cx - 100;
      if (on) {
        FillRoundedRect(px, stride, w, h, x0, ymid - th / 2, tw, th, 17,
                        kLightPrimary);
        FillCircle(px, stride, w, h, x0 + tw - 18, ymid, 12, kLightOnPrimary);
        DrawLine(px, stride, w, h, x0 + tw - 24, ymid, x0 + tw - 20,
                 ymid + 4, 3, kLightPrimary);
        DrawLine(px, stride, w, h, x0 + tw - 20, ymid + 4, x0 + tw - 12,
                 ymid - 5, 3, kLightPrimary);
      } else {
        StrokeRoundedRect(px, stride, w, h, x0, ymid - th / 2, tw, th, 17, 3,
                          kLightOutline, kCardBg);
        FillCircle(px, stride, w, h, x0 + 18, ymid, 10,
                   kLightOnSurfaceVariant);
      }
      DrawText(px, stride, w, h, on ? "On" : "Off", x0 + tw + 16, ymid - 7,
               kScale, kLightOnSurface);
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, name);
    return;
  }

  if (c == "Slider" || c == "LinearProgress") {
    double v = PropDouble(d, c == "Slider" ? "value" : "progress", 0.5);
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    int tx0 = bx + 56;
    int tx1 = bx + bw - 56;
    int ty = by + (bh - 20) / 2;
    FillRoundedRect(px, stride, w, h, tx0, ty - 6, tx1 - tx0, 12, 6,
                    kLightSurfaceVariant);
    int fillW = static_cast<int>((tx1 - tx0) * v);
    if (fillW > 0) {
      FillRoundedRect(px, stride, w, h, tx0, ty - 6, fillW, 12, 6,
                      kLightPrimary);
    }
    if (c == "Slider") {
      FillCircle(px, stride, w, h, tx0 + fillW, ty, 13, kLightPrimary);
      FillCircle(px, stride, w, h, tx0 + fillW, ty, 5, kLightOnPrimary);
    }
    char cap[48];
    std::snprintf(cap, sizeof(cap), "%s %d%%", c.c_str(),
                  static_cast<int>(v * 100));
    PaintCaption(px, stride, w, h, bx, by, bw, bh, cap);
    return;
  }

  if (c == "CircularProgress") {
    double v = PropDouble(d, "progress", 0.4);
    int r = 30;
    RingCircle(px, stride, w, h, cx, by + (bh - 20) / 2, r, 9,
               kLightSurfaceVariant);
    DrawArc(px, stride, w, h, cx, by + (bh - 20) / 2, r, -90.0f,
            static_cast<float>(v * 360.0), 9, kLightPrimary);
    char cap[48];
    std::snprintf(cap, sizeof(cap), "Circular %d%%",
                  static_cast<int>(v * 100));
    PaintCaption(px, stride, w, h, bx, by, bw, bh, cap);
    return;
  }

  if (c == "Badge") {
    std::string label = PropValue(d, "label");
    if (label.empty()) label = "3";
    int ymid = by + (bh - 20) / 2;
    FillCircle(px, stride, w, h, cx - 40, ymid, 17, kLightError);
    DrawTextCentered(px, stride, w, h, label.c_str(), cx - 40, ymid - 7,
                     kScale, kLightOnPrimary);
    DrawText(px, stride, w, h, "Badge", cx - 8, ymid - 7, kScale,
             kLightOnSurface);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Badge");
    return;
  }

  if (c == "ListItem") {
    std::string headline = PropValue(d, "headline");
    std::string supporting = PropValue(d, "supporting");
    if (headline.empty()) headline = ShortCaption(row);
    int y0 = by + (bh - 20) / 2 - 16;
    FillCircle(px, stride, w, h, bx + 64, y0 + 16, 20,
               kLightSecondaryContainer);
    if (!headline.empty()) {
      DrawText(px, stride, w, h, headline.c_str(), bx + 100, y0 - 4, kScale,
               kLightOnSurface);
    }
    if (!supporting.empty()) {
      std::string s = supporting;
      int maxChars = (bw - 140) / (6 * kScale);
      if (static_cast<int>(s.size()) > maxChars) s.resize(maxChars);
      DrawText(px, stride, w, h, s.c_str(), bx + 100, y0 + 18, kScale,
               kLightOnSurfaceVariant);
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "ListItem");
    return;
  }

  if (c == "Avatar") {
    std::string label = PropValue(d, "label");
    if (label.empty()) label = ShortCaption(row);
    std::string initials;
    for (char ch : label) {
      if ((ch >= 'A' && ch <= 'Z') && initials.size() < 2) initials += ch;
    }
    if (initials.empty() && !label.empty()) initials = label.substr(0, 1);
    int ymid = by + (bh - 20) / 2;
    FillCircle(px, stride, w, h, cx - 60, ymid, 30, kLightPrimary);
    DrawTextCentered(px, stride, w, h, initials.c_str(), cx - 60, ymid - 7,
                     kScale, kLightOnPrimary);
    DrawText(px, stride, w, h, label.c_str(), cx - 12, ymid - 7, kScale,
             kLightOnSurface);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Avatar");
    return;
  }

  if (c == "Icon" || c == "Image") {
    std::string name = PropValue(d, c == "Icon" ? "name" : "source");
    if (name.empty()) name = ShortCaption(row);
    int ymid = by + (bh - 20) / 2;
    if (c == "Icon") {
      FillCircle(px, stride, w, h, cx - 60, ymid, 26,
                 kLightSecondaryContainer);
      std::string initial = name.empty() ? "?" : name.substr(0, 1);
      DrawTextCentered(px, stride, w, h, initial.c_str(), cx - 60, ymid - 7,
                       kScale, kLightOnPrimaryContainer);
    } else {
      int iw = 220;
      int ih = bh - 64;
      StrokeRoundedRect(px, stride, w, h, cx - 60 - iw / 2, ymid - ih / 2, iw,
                        ih, 16, 2, kLightOutlineVariant, kLightSurfaceVariant);
      FillCircle(px, stride, w, h, cx - 60 - 40, ymid - 14, 12,
                 kLightOnSurfaceVariant);
      DrawLine(px, stride, w, h, cx - 60 - iw / 2 + 24, ymid + ih / 2 - 24,
               cx - 60 - 10, ymid - 6, 5, kLightOnSurfaceVariant);
      DrawLine(px, stride, w, h, cx - 60 - 10, ymid - 6, cx - 60 + 30,
               ymid + ih / 2 - 24, 5, kLightOnSurfaceVariant);
    }
    std::string cap = c + " " + name;
    int maxChars = (bw - 32) / (6 * kScale);
    if (static_cast<int>(cap.size()) > maxChars) cap.resize(maxChars);
    DrawText(px, stride, w, h, cap.c_str(), cx - 20, ymid - 7, kScale,
             kLightOnSurface);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    return;
  }

  if (c == "Divider") {
    int ymid = by + bh / 2;
    FillRect(px, stride, w, h, bx + 32, ymid - 1, bw - 64, 2,
             kLightOutlineVariant);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "Divider");
    return;
  }

  if (c == "TextFieldEditCore" || c == "TextInput" || c == "SearchBar") {
    std::string hint = PropValue(d, "placeholder");
    if (hint.empty()) hint = PropValue(d, "value");
    if (hint.empty()) hint = "Search";
    bool pill = (c == "SearchBar");
    int fw = bw - 64;
    int fh = pill ? 68 : 60;
    int x0 = bx + 32;
    int y0 = by + (bh - 20 - fh) / 2 - 4;
    if (pill) ShadowRounded(px, stride, w, h, x0, y0, fw, fh, fh / 2);
    std::uint32_t fieldBg = pill ? kWhite : kCardBg;
    if (pill) {
      FillRoundedRect(px, stride, w, h, x0, y0, fw, fh, fh / 2, fieldBg);
    } else {
      StrokeRoundedRect(px, stride, w, h, x0, y0, fw, fh, 10, 2,
                        kLightOutline, kCardBg);
    }
    FillCircle(px, stride, w, h, x0 + 34, y0 + fh / 2, 9,
               kLightOnSurfaceVariant);
    std::string t = hint;
    int maxChars = (fw - 100) / (6 * kScale);
    if (static_cast<int>(t.size()) > maxChars) t.resize(maxChars);
    DrawText(px, stride, w, h, t.c_str(), x0 + 52, y0 + fh / 2 - 7, kScale,
             kLightOnSurfaceVariant);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    return;
  }

  if (c == "TopAppBar") {
    std::string size = PropValue(d, "size");
    std::string title = PropValue(d, "title");
    if (title.empty()) title = "m3-gallery";
    int barH = (size == "large") ? bh - 48 : (size == "medium" ? 150 : 110);
    int y0 = by + 10;
    FillRoundedRect(px, stride, w, h, bx + 8, y0, bw - 16, barH, 20, kWhite);
    int tscale = (size == "small") ? kScale : 3;
    DrawText(px, stride, w, h, title.c_str(), bx + 40, y0 + 22, tscale,
             kLightOnSurface);
    FillCircle(px, stride, w, h, bx + bw - 96, y0 + 34, 9,
               kLightOnSurfaceVariant);
    FillCircle(px, stride, w, h, bx + bw - 56, y0 + 34, 9,
               kLightOnSurfaceVariant);
    PaintCaption(px, stride, w, h, bx, by, bw, bh, "TopAppBar " + size);
    return;
  }

  if (c == "NavigationBar" || c == "SegmentedButton" || c == "ButtonGroup" ||
      c == "SplitButton") {
    int barH = 76;
    int segs = 3;
    int segW = (bw - 96) / segs;
    int x0 = cx - segW * segs / 2;
    int y0 = by + (bh - 20 - barH) / 2 - 6;
    std::string sel = PropValue(d, "selected");
    if (sel.empty()) sel = PropValue(d, "label");
    for (int s = 0; s < segs; ++s) {
      int sx = x0 + s * segW;
      bool first = (s == 0);
      if (s == 0) {
        FillRoundedRect(px, stride, w, h, sx, y0, segW, barH, 20, kCardBg);
      } else if (s == segs - 1) {
        FillRoundedRect(px, stride, w, h, sx, y0, segW, barH, 20, kCardBg);
        FillRect(px, stride, w, h, sx, y0, 20, barH, kCardBg);
      } else {
        FillRect(px, stride, w, h, sx, y0, segW, barH, kCardBg);
      }
      DrawLine(px, stride, w, h, sx, y0 + 8, sx, y0 + barH - 8, 2,
               kLightOutlineVariant);
      if (first) {
        FillRoundedRect(px, stride, w, h, sx + segW / 2 - 26, y0 + 12, 52, 30,
                        15, kLightSecondaryContainer);
      }
      FillCircle(px, stride, w, h, sx + segW / 2, y0 + 27, 7,
                 first ? kLightOnPrimaryContainer
                       : kLightOnSurfaceVariant);
    }
    StrokeRoundedRect(px, stride, w, h, x0, y0, segW * segs, barH, 20, 2,
                      kLightOutlineVariant, kCardBg);
    PaintCaption(px, stride, w, h, bx, by, bw, bh,
                 sel.empty() ? c : c + " " + sel);
    return;
  }

  if (c == "NavigationRail" || c == "NavigationDrawer" ||
      c == "BottomAppBar") {
    if (c == "NavigationRail") {
      int rw = 76;
      int x0 = cx - 150;
      int y0 = by + 8;
      int rhh = bh - 52;
      FillRoundedRect(px, stride, w, h, x0, y0, rw, rhh, 20, kWhite);
      for (int s = 0; s < 3; ++s) {
        int yy = y0 + 34 + s * 44;
        if (s == 0) {
          FillRoundedRect(px, stride, w, h, x0 + 10, yy - 15, rw - 20, 32, 16,
                          kLightSecondaryContainer);
        }
        FillCircle(px, stride, w, h, x0 + rw / 2, yy + 1, 7,
                   s == 0 ? kLightOnPrimaryContainer
                          : kLightOnSurfaceVariant);
      }
      DrawText(px, stride, w, h, "Rail", x0 + rw + 20, by + bh / 2 - 7,
               kScale, kLightOnSurface);
    } else if (c == "NavigationDrawer") {
      int dw = 300;
      if (dw > bw - 32) dw = bw - 32;
      int x0 = cx - dw / 2;
      int y0 = by + 8;
      int dhh = bh - 52;
      FillRoundedRect(px, stride, w, h, x0, y0, dw, dhh, 20, kWhite);
      for (int s = 0; s < 3; ++s) {
        int yy = y0 + 26 + s * 40;
        if (s == 0) {
          FillRoundedRect(px, stride, w, h, x0 + 12, yy, dw - 24, 34, 17,
                          kLightSecondaryContainer);
        }
        FillCircle(px, stride, w, h, x0 + 40, yy + 17, 8,
                   s == 0 ? kLightOnPrimaryContainer
                          : kLightOnSurfaceVariant);
        DrawText(px, stride, w, h, s == 0 ? "Home" : "Item", x0 + 60, yy + 10,
                 kScale,
                 s == 0 ? kLightOnPrimaryContainer : kLightOnSurface);
      }
    } else {
      std::string title = PropValue(d, "title");
      if (title.empty()) title = ShortCaption(row);
      int barH = 76;
      int y0 = by + (bh - 20 - barH) / 2;
      FillRoundedRect(px, stride, w, h, bx + 8, y0, bw - 16, barH, 20,
                      kWhite);
      FillCircle(px, stride, w, h, bx + 56, y0 + barH / 2, 9,
                 kLightOnSurfaceVariant);
      DrawText(px, stride, w, h, title.c_str(), bx + 76, y0 + barH / 2 - 7,
               kScale, kLightOnSurface);
      FillCircle(px, stride, w, h, bx + bw - 72, y0 - 6, 24,
                 kLightSecondaryContainer);
      DrawLine(px, stride, w, h, bx + bw - 80, y0 - 6, bx + bw - 64, y0 - 6,
               5, kLightOnPrimaryContainer);
      DrawLine(px, stride, w, h, bx + bw - 72, y0 - 14, bx + bw - 72, y0 + 2,
               5, kLightOnPrimaryContainer);
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    return;
  }

  if (c == "Dialog" || c == "Menu" || c == "BottomSheet" || c == "SnackbarHost" ||
      c == "Tooltip" || c == "Carousel") {
    if (c == "Dialog") {
      std::string title = PropValue(d, "title");
      if (title.empty()) title = "Dialog";
      int dw = 380;
      if (dw > bw - 32) dw = bw - 32;
      int dhh = bh - 60;
      int x0 = cx - dw / 2;
      int y0 = by + 8;
      ShadowRounded(px, stride, w, h, x0, y0, dw, dhh, 28);
      FillRoundedRect(px, stride, w, h, x0, y0, dw, dhh, 28, kWhite);
      DrawText(px, stride, w, h, title.c_str(), x0 + 28, y0 + 22, kScale,
               kLightOnSurface);
      DrawLine(px, stride, w, h, x0 + 28, y0 + 56, x0 + dw - 28, y0 + 56, 2,
               kLightOutlineVariant);
      DrawText(px, stride, w, h, "Cancel", x0 + dw - 170, y0 + dhh - 34,
               kScale, kLightPrimary);
      DrawText(px, stride, w, h, "OK", x0 + dw - 70, y0 + dhh - 34, kScale,
               kLightPrimary);
    } else if (c == "Menu") {
      int dw = 260;
      int dhh = bh - 60;
      int x0 = cx - dw / 2;
      int y0 = by + 8;
      ShadowRounded(px, stride, w, h, x0, y0, dw, dhh, 16);
      FillRoundedRect(px, stride, w, h, x0, y0, dw, dhh, 16, kWhite);
      const char* items[3] = {"Edit", "Share", "Delete"};
      for (int i = 0; i < 3; ++i) {
        DrawText(px, stride, w, h, items[i], x0 + 24, y0 + 20 + i * 34,
                 kScale, kLightOnSurface);
      }
    } else if (c == "BottomSheet") {
      int sh = 110;
      int y0 = by + bh - 20 - sh;
      FillRoundedRect(px, stride, w, h, bx + 8, y0, bw - 16, sh + 20, 24,
                      kWhite);
      FillRoundedRect(px, stride, w, h, cx - 40, y0 + 12, 80, 8, 4,
                      kLightOutlineVariant);
      DrawTextCentered(px, stride, w, h, "Bottom sheet", cx, y0 + 36, kScale,
                       kLightOnSurface);
    } else if (c == "SnackbarHost") {
      std::string msg = PropValue(d, "message");
      if (msg.empty()) msg = ShortCaption(row);
      int barH = 64;
      int y0 = by + (bh - 20 - barH) / 2;
      FillRoundedRect(px, stride, w, h, bx + 24, y0, bw - 48, barH, 12,
                      kLightInverseSurface);
      std::string m = msg;
      int maxChars = (bw - 140) / (6 * kScale);
      if (static_cast<int>(m.size()) > maxChars) m.resize(maxChars);
      DrawText(px, stride, w, h, m.c_str(), bx + 48, y0 + 25, kScale, kWhite);
      DrawText(px, stride, w, h, "Undo", bx + bw - 110, y0 + 25, kScale,
               kLightSecondaryContainer);
    } else if (c == "Tooltip") {
      std::string text = PropValue(d, "text");
      if (text.empty()) text = ShortCaption(row);
      int tw = TextWidth(text.c_str(), kScale) + 48;
      if (tw > bw - 32) tw = bw - 32;
      int th = 52;
      int x0 = cx - tw / 2;
      int y0 = by + (bh - 20 - th) / 2;
      FillRoundedRect(px, stride, w, h, x0, y0, tw, th, 10,
                      kLightInverseSurface);
      DrawTextCentered(px, stride, w, h, text.c_str(), cx, y0 + 19, kScale,
                       kWhite);
    } else {  // Carousel
      int cw = bw - 64;
      int chh = bh - 76;
      int x0 = bx + 32;
      int y0 = by + 8;
      FillRoundedRect(px, stride, w, h, x0, y0, cw, chh, 20,
                      kLightTertiaryContainer);
      FillCircle(px, stride, w, h, x0 + 60, y0 + chh / 2, 16, kWhite);
      for (int i = 0; i < 3; ++i) {
        FillCircle(px, stride, w, h, cx - 24 + i * 24, y0 + chh + 20,
                   i == 0 ? 7 : 5,
                   i == 0 ? kLightPrimary : kLightOutlineVariant);
      }
    }
    PaintCaption(px, stride, w, h, bx, by, bw, bh, c);
    return;
  }

  // Fallback: tonal card with the component name — never a blank band.
  FillRoundedRect(px, stride, w, h, bx + 8, by + 8, bw - 16, bh - 44, 18,
                  kLightSecondaryContainer);
  std::string name = c.empty() ? ShortCaption(row) : c;
  DrawTextCentered(px, stride, w, h, name.c_str(), cx, by + (bh - 44) / 2,
                   kScale, kLightOnPrimaryContainer);
  PaintCaption(px, stride, w, h, bx, by, bw, bh, ShortCaption(row));
}

// Text/label rows: rounded theme bands with the gallery copy.
void PaintTextRow(std::uint32_t* px, int stride, int w, int h, int bx, int by,
                  int bw, int bh, const GalleryRow& row) {
  std::uint32_t bg = row.dark ? kDarkSurface : kLightSurface;
  std::uint32_t fg = row.dark ? kDarkOnSurface : kLightOnSurface;
  if (row.header) {
    bg = row.dark ? kDarkPrimary : kLightPrimary;
    fg = row.dark ? kDarkOnPrimary : kLightOnPrimary;
  }
  FillRoundedRect(px, stride, w, h, bx, by, bw, bh, 18, bg);
  std::string label = row.label;
  int maxChars = (bw - 48) / (6 * kScale);
  if (maxChars < 8) maxChars = 8;
  if (static_cast<int>(label.size()) > maxChars) {
    label.resize(static_cast<std::size_t>(maxChars));
  }
  DrawText(px, stride, w, h, label.c_str(), bx + 24, by + bh / 2 - 7, kScale,
           fg);
}

int RasterGalleryPage(const std::vector<GalleryRow>& rows, int page,
                      std::uint32_t* pixels, int stridePx, int width,
                      int height, std::string* error) {
  try {
    if (pixels == nullptr || stridePx < width || width <= 0 || height <= 0) {
      if (error != nullptr) *error = "bad raster target";
      return 0;
    }
    // Geometry constants (kPad/kGap/kHeaderH/kFooterH/kScale) are the
    // file-scope M3 metrics above; rows size themselves via RowHeight.

    if (rows.empty()) {
      // Diagnostic screen, never blank: names the failure in plain text.
      FillRect(pixels, stridePx, width, height, 0, 0, width, height, kErrorBg);
      std::string msg = "M3 GALLERY: NO ROWS";
      if (error != nullptr && !error->empty()) msg += " - " + *error;
      for (std::size_t i = 0; i < msg.size(); ++i) {
        if (msg[i] == '\n') msg[i] = ' ';
      }
      if (msg.size() > 40) msg.resize(40);
      DrawText(pixels, stridePx, width, height, msg.c_str(), kPad,
               height / 2, kScale, kLightOnPrimary);
      return 0;
    }

    // Page wash first: no black gutters between bands. Rows paint their own
    // light/dark theme bands over this.
    FillRect(pixels, stridePx, width, height, 0, 0, width, height,
             kLightSurface);

    std::vector<int> starts = PageStarts(rows, height);
    int pages = static_cast<int>(starts.size()) - 1;
    if (pages < 1) pages = 1;
    if (page < 0) page = 0;
    if (page >= pages) page = pages - 1;
    int start = starts[static_cast<std::size_t>(page)];
    int count = starts[static_cast<std::size_t>(page) + 1] - start;

    // Row geometry through the flexbox-subset seam: a column root with
    // padding/gap and one fixed child per visible row.
    std::vector<float> rowY(static_cast<std::size_t>(count), 0.0f);
    {
      std::vector<CompiledNode> nodes;
      nodes.reserve(static_cast<std::size_t>(count) + 1);
      CompiledNode root;
      root.parent = -1;
      root.style.direction = Direction::Column;
      root.style.padding = static_cast<float>(kPad);
      root.style.gap = static_cast<float>(kGap);
      root.style.width = static_cast<float>(width);
      root.style.height = static_cast<float>(height);
      nodes.push_back(root);
      for (int i = 0; i < count; ++i) {
        const GalleryRow& r = rows[static_cast<std::size_t>(start + i)];
        CompiledNode child;
        child.parent = 0;
        child.style.width = static_cast<float>(width - 2 * kPad);
        child.style.height = static_cast<float>(RowHeight(r));
        nodes.push_back(child);
      }
      RuntimeTree tree;
      LayoutEngine engine;
      bool laid = false;
      try {
        NodeId rootId = instantiate(nodes.data(), nodes.size(), tree);
        if (rootId != kInvalidNodeId) {
          engine.compute(tree, rootId);
          for (int i = 0; i < count; ++i) {
            rowY[static_cast<std::size_t>(i)] =
                engine.boxOf(static_cast<NodeId>(i + 1)).y;
          }
          laid = true;
        }
      } catch (...) {
        laid = false;
      }
      if (!laid) {
        float cursor = static_cast<float>(kPad);
        for (int i = 0; i < count; ++i) {
          rowY[static_cast<std::size_t>(i)] = cursor;
          cursor += static_cast<float>(
                        RowHeight(rows[static_cast<std::size_t>(start + i)])) +
                    static_cast<float>(kGap);
        }
      }
    }

    // Title bar.
    FillRect(pixels, stridePx, width, height, 0, 0, width, kHeaderH,
             kLightPrimary);
    char title[64];
    std::snprintf(title, sizeof(title), "M3 GALLERY %d/%d", page + 1, pages);
    DrawText(pixels, stridePx, width, height, title, kPad, 28, 3,
             kLightOnPrimary);

    for (int i = 0; i < count; ++i) {
      const GalleryRow& row = rows[static_cast<std::size_t>(start + i)];
      // Bands sit below the title bar; the flexbox seam owns the
      // within-list offsets (padding + gap), the chrome offset is ours.
      int y = static_cast<int>(rowY[static_cast<std::size_t>(i)]) + kHeaderH;
      int rh = RowHeight(row);
      int bx = kPad;
      int bw = width - 2 * kPad;
      if (row.comp.empty()) {
        PaintTextRow(pixels, stridePx, width, height, bx, y, bw, rh, row);
      } else {
        PaintComponent(pixels, stridePx, width, height, bx, y, bw, rh, row);
      }
    }

    // Footer with paging hint.
    FillRect(pixels, stridePx, width, height, 0, height - kFooterH, width,
             kFooterH, kFooterBg);
    char footer[96];
    std::snprintf(footer, sizeof(footer), "PAGE %d OF %d - TAP FOR NEXT",
                  page + 1, pages);
    DrawText(pixels, stridePx, width, height, footer, kPad,
             height - kFooterH + 18, kScale, kFooterFg);
    return count;
  } catch (...) {
    if (error != nullptr) *error = "raster failed (exception)";
    return 0;
  }
}

// ---- Retained present path (T9: FR-GRAPHICS-010 / AC-GRAPHICS-030) ----
// Fixed present viewport: matches the RasterGalleryPage reference geometry
// (720x1600) so retained DrawCmd bounds diff against the CPU oracle 1:1.
// The virtual canvas stacks every page at pageIndex * kPresentH; golden
// tooling slices it back into per-page PNGs.
constexpr int kPresentW = 720;
constexpr int kPresentH = 1600;
constexpr std::uint32_t kShadowBlack = RGBA(0x00, 0x00, 0x00, 0xFF);

// Content-kind assignment per component family. Every family maps to a
// distinct retained kind so the per-page goldens exercise the whole T2-T8
// vocabulary; unknown components fall back to a tonal Rect (never blank).
PrimitiveKind FamilyKind(const std::string& comp) {
  if (comp == "Button" || comp == "ExtendedFab" || comp == "Chip" ||
      comp == "Checkbox" || comp == "Switch" || comp == "Slider" ||
      comp == "LinearProgress") {
    return PrimitiveKind::StateLayer;
  }
  if (comp == "IconButton" || comp == "Fab") return PrimitiveKind::Ripple;
  if (comp == "Dialog" || comp == "Menu" || comp == "Tooltip") {
    return PrimitiveKind::OverlayScrim;
  }
  if (comp == "Icon" || comp == "Avatar" || comp == "Image") {
    return PrimitiveKind::VectorIcon;
  }
  if (comp == "CircularProgress" || comp == "Radio" || comp == "Badge") {
    return PrimitiveKind::CircleArc;
  }
  if (comp == "TextFieldEditCore" || comp == "TextInput" ||
      comp == "SearchBar" || comp == "Carousel") {
    return PrimitiveKind::Image;
  }
  if (comp == "Card" || comp == "Divider" || comp == "BottomSheet" ||
      comp == "NavigationBar" || comp == "SegmentedButton" ||
      comp == "ButtonGroup" || comp == "SplitButton" ||
      comp == "NavigationRail" || comp == "NavigationDrawer" ||
      comp == "BottomAppBar" || comp == "SnackbarHost") {
    return PrimitiveKind::Rect;
  }
  return PrimitiveKind::ShapedText;
}

// One accent content cmd for a component row: centered control rect with
// theme payload colors. nodeId (== rowIndex + 1, never the 0 sentinel) is
// the row key golden tooling uses to resolve label strings.
void EmitFamilyCmd(const GalleryRow& /*row*/, PrimitiveKind kind, int bx,
                   int by, int bw, int bh, std::uint32_t node,
                   std::vector<DrawCmd>& out) {
  int cx = bx + bw / 2;
  int cw = bw - 96;
  if (cw > 320) cw = 320;
  if (cw < 64) cw = 64;
  int ch = bh - 56;
  if (ch > 56) ch = 56;
  if (ch < 24) ch = 24;
  int x0 = cx - cw / 2;
  int y0 = by + 12;
  DrawCmd cmd;
  cmd.kind = kind;
  cmd.nodeId = node;
  switch (kind) {
    case PrimitiveKind::StateLayer:
      cmd.bounds = {static_cast<float>(x0), static_cast<float>(y0),
                    static_cast<float>(cw), static_cast<float>(ch)};
      cmd.radius = static_cast<float>(ch) / 2.0f;
      cmd.payload.stateColor = kLightPrimary;
      cmd.payload.stateOpacity = 0.9f;
      break;
    case PrimitiveKind::Ripple: {
      int s = ch < cw ? ch : cw;
      cmd.bounds = {static_cast<float>(cx - s / 2),
                    static_cast<float>(y0 + ch / 2 - s / 2),
                    static_cast<float>(s), static_cast<float>(s)};
      cmd.payload.rippleBounds = cmd.bounds;
      cmd.payload.rippleProgress = 0.5f;
      cmd.payload.rippleColor = kLightPrimary;
      break;
    }
    case PrimitiveKind::OverlayScrim:
      cmd.bounds = {static_cast<float>(bx + 8), static_cast<float>(by + 8),
                    static_cast<float>(bw - 16),
                    static_cast<float>(bh - 52)};
      cmd.payload.scrimColor = kLightInverseSurface;
      cmd.payload.scrimAlpha = 0.25f;
      break;
    case PrimitiveKind::VectorIcon:
      cmd.bounds = {static_cast<float>(cx - 72), static_cast<float>(y0),
                    48.0f, 48.0f};
      cmd.payload.assetId = node;
      cmd.payload.tintColor = kLightOnPrimaryContainer;
      break;
    case PrimitiveKind::CircleArc: {
      int s = ch < 60 ? ch : 60;
      cmd.bounds = {static_cast<float>(cx - s / 2),
                    static_cast<float>(y0 + ch / 2 - s / 2),
                    static_cast<float>(s), static_cast<float>(s)};
      cmd.payload.arcStartDeg = 0.0f;
      cmd.payload.arcSweepDeg = 270.0f;
      cmd.payload.strokeWidth = 8.0f;
      cmd.payload.arcColor = kLightPrimary;
      break;
    }
    case PrimitiveKind::Image:
      cmd.bounds = {static_cast<float>(x0), static_cast<float>(y0),
                    static_cast<float>(cw), static_cast<float>(ch)};
      cmd.radius = 16.0f;
      break;
    case PrimitiveKind::Rect:
      cmd.bounds = {static_cast<float>(bx + 32), static_cast<float>(y0 + 8),
                    static_cast<float>(bw - 64),
                    static_cast<float>(ch - 16)};
      break;
    case PrimitiveKind::ShapedText:
    default:
      cmd.kind = PrimitiveKind::ShapedText;
      cmd.bounds = {static_cast<float>(bx + 40), static_cast<float>(y0 + 4),
                    static_cast<float>(bw - 80), 28.0f};
      cmd.payload.glyphRunId = node;
      cmd.payload.textColor = kLightOnSurface;
      break;
  }
  out.push_back(cmd);
}

// Caption strip cmd shared by component and text rows (golden tooling
// resolves ShortCaption(row) via nodeId and paints it with DrawText).
void EmitCaptionCmd(int bx, int by, int bw, int bh, std::uint32_t node,
                    std::vector<DrawCmd>& out) {
  DrawCmd cmd;
  cmd.kind = PrimitiveKind::Text;
  cmd.bounds = {static_cast<float>(bx + 16),
                static_cast<float>(by + bh - 14 - kScale * 7),
                static_cast<float>(bw - 32), 14.0f};
  cmd.nodeId = node;
  cmd.payload.glyphRunId = node;
  cmd.payload.textColor = kLightOnSurfaceVariant;
  out.push_back(cmd);
}

void EmitComponentRowCmds(const GalleryRow& row, int bx, int by, int bw,
                          int bh, std::uint32_t node,
                          std::vector<DrawCmd>& out) {
  DrawCmd shadow;
  shadow.kind = PrimitiveKind::ElevationShadow;
  shadow.bounds = {static_cast<float>(bx), static_cast<float>(by),
                   static_cast<float>(bw), static_cast<float>(bh)};
  shadow.radius = 20.0f;
  shadow.nodeId = node;
  shadow.payload.elevationDp = 2.0f;
  shadow.payload.shadowBlur = 4.0f;
  shadow.payload.shadowColor = kShadowBlack;
  out.push_back(shadow);
  DrawCmd card;
  card.kind = PrimitiveKind::RoundedRect;
  card.bounds = shadow.bounds;
  card.radius = 20.0f;
  card.nodeId = node;
  out.push_back(card);
  EmitFamilyCmd(row, FamilyKind(row.comp), bx, by, bw, bh, node, out);
  EmitCaptionCmd(bx, by, bw, bh, node, out);
}

void EmitTextRowCmds(const GalleryRow& row, int bx, int by, int bw, int bh,
                     std::uint32_t node, std::vector<DrawCmd>& out) {
  (void)row;
  DrawCmd band;
  band.kind = PrimitiveKind::RoundedRect;
  band.bounds = {static_cast<float>(bx), static_cast<float>(by),
                 static_cast<float>(bw), static_cast<float>(bh)};
  band.radius = 18.0f;
  band.nodeId = node;
  out.push_back(band);
  EmitCaptionCmd(bx, by, bw, bh, node, out);
}

bool PresentGallery(const void* mui1Data, std::size_t len, Renderer& renderer,
                    Swapchain& target, std::string* error) {
  try {
    std::string parseError;
    std::vector<GalleryRow> rows =
        ParseGalleryRows(mui1Data, len, &parseError);
    if (rows.empty()) {
      if (error != nullptr) {
        *error = parseError.empty()
                     ? "Showcase root produced no gallery rows"
                     : parseError;
      }
      return false;
    }
    // Page packing mirrors RasterGalleryPage (same RowHeight/PageStarts
    // metrics); each page lands at pageIndex * kPresentH in the virtual
    // canvas so per-page goldens slice back out exactly.
    std::vector<DrawCmd> cmds;
    cmds.reserve(rows.size() * 4 + 2);
    std::vector<int> starts = PageStarts(rows, kPresentH);
    int pages = static_cast<int>(starts.size()) - 1;
    if (pages < 1) {
      pages = 1;
      starts = {0, static_cast<int>(rows.size())};
    }
    for (int p = 0; p < pages; ++p) {
      float yOff = static_cast<float>(p * kPresentH);
      DrawCmd title;
      title.kind = PrimitiveKind::Rect;
      title.bounds = {0.0f, yOff, static_cast<float>(kPresentW),
                      static_cast<float>(kHeaderH)};
      title.nodeId = static_cast<std::uint32_t>(900000 + p * 2);
      cmds.push_back(title);
      DrawCmd footer;
      footer.kind = PrimitiveKind::Rect;
      footer.bounds = {0.0f, yOff + kPresentH - kFooterH,
                       static_cast<float>(kPresentW),
                       static_cast<float>(kFooterH)};
      footer.nodeId = static_cast<std::uint32_t>(900000 + p * 2 + 1);
      cmds.push_back(footer);
      int start = starts[static_cast<std::size_t>(p)];
      int end = starts[static_cast<std::size_t>(p + 1)];
      float cursor = static_cast<float>(kPad);
      for (int i = start; i < end; ++i) {
        const GalleryRow& row = rows[static_cast<std::size_t>(i)];
        int rh = RowHeight(row);
        int bx = kPad;
        int bw = kPresentW - 2 * kPad;
        int by = static_cast<int>(cursor) + kHeaderH +
                 static_cast<int>(yOff);
        std::uint32_t node = static_cast<std::uint32_t>(i + 1);
        if (row.comp.empty()) {
          EmitTextRowCmds(row, bx, by, bw, rh, node, cmds);
        } else {
          EmitComponentRowCmds(row, bx, by, bw, rh, node, cmds);
        }
        cursor += static_cast<float>(rh + kGap);
      }
    }
    DrawError derr = renderer.draw(cmds, target);
    if (derr != DrawError::None) {
      if (error != nullptr) {
        if (derr == DrawError::UnsupportedBaseline) {
          if (renderer.lastError().empty()) {
            *error = "unsupported baseline: retained present refused";
          } else {
            *error = renderer.lastError();
          }
        } else {
          *error = "retained present rejected primitive";
        }
      }
      return false;
    }
    return true;
  } catch (...) {
    if (error != nullptr) *error = "present failed (exception)";
    return false;
  }
}

}  // namespace runtime
