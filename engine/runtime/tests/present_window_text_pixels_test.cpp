// T11 — Pixel-level present-window tests for vector text (F1 bars/frozen/blank).
// T12 — hardened cell-interior modulation (uniform per-glyph cells fail).
// T13 — curved-glyph enclosed-counter case (stems-only coverage fails).
// FR-TEXT-016 / AC-TEXT-045 (shaped vector text visible on device; atlas reuse)
// / AC-TEXT-046 (script/scale rendering reaches pixels).
//
// Public boundary: the plan T11 `Interface:` lines (see unit-tests/T11.md),
// reproduced verbatim in the contract:
//   PresentDeviceGalleryPage(rows, page, renderer, target, viewportH, error)
//   RasterizeShapedText(run, bounds, color) -> StatusOr<CoverageMask>
// Hermetic: C++17 + committed runtime/text headers only; in-memory
// GalleryRows, an in-process pixel-buffer FakePixelWindow; no device, no
// ANativeWindow, no GPU, no network, no config, no filesystem, no live
// services, no JNI on the frame path.
//
// RED STATE (expected): the T11 signatures exist nowhere in the tree —
// `RasterizeShapedText` still returns `DrawError` with no coverage output
// (renderer.h:92) and `PresentDeviceGalleryPage` takes no `viewportH`
// (gallery_present.h:103). This TU therefore fails to compile until the
// coder threads real coverage out of `RasterizeShapedText`, honors the page
// slice with one viewport height, and paints real card/shadow ink.
// Once it compiles, each case pins one F1 defect: (a) uniform solid fill OR
// uniform per-glyph cells (T12: a per-cell-constant CoverageMask fails the
// cell-interior modulation leg), (b) frozen frames, (c) cleared-white post on failure, (d) near-white cards,
// (e) stems-only coverage (T13: a stem-span CoverageMask with no enclosed
// counter fails the counter-hole legs; true outline coverage passes).
// `glyphRunId == 0 -> UnknownPrimitive` stays untouched (render_smoke_test).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "muse/text/text_types.h"
#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

namespace {

// Clear color of the presented window (production WindowSwapchain clear).
constexpr std::uint32_t kClear = 0xFEF7FFu;
// Sentinel pre-fill proving "buffer untouched" (distinct from clear AND white).
constexpr std::uint32_t kSentinel = 0xDEADBEEFu;
// Pure failure white that must NEVER be posted (RenderLocked pre-clear bug).
constexpr std::uint32_t kWhite = 0xFFFFFFFFu;

runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

runtime::GalleryRow MakeRow(const std::string& label) {
  runtime::GalleryRow row;
  row.label = label;
  return row;
}

runtime::GalleryRow MakeComponentRow(const std::string& label,
                                     const std::string& comp) {
  runtime::GalleryRow row;
  row.label = label;
  row.comp = comp;
  row.desc = "Type " + comp + " label=" + label;
  return row;
}

// In-memory window buffer: owns a W x H RGBA pixel plane (pre-filled by the
// caller) and blends every submitted cmd into it. Text blends the production
// CoverageMask src-over in bounds (the T11 seam: coverage must modulate
// pixels); non-text kinds paint from the cmd payload ink (stateColor /
// shadowColor), so kinds the coder leaves at near-background ink stay
// near-background here too. No GPU, no ANativeWindow.
class FakePixelWindow : public runtime::Swapchain {
 public:
  FakePixelWindow(int w, int h, std::uint32_t fill)
      : w_(w), h_(h), pixels_(static_cast<std::size_t>(w * h), fill) {}

  void submit(const runtime::DrawCmd& cmd) override {
    submitted_.push_back(cmd);
    switch (cmd.kind) {
      case runtime::PrimitiveKind::ShapedText: {
        if (cmd.payload.glyphRunId == 0) return;
        // T11 seam: real glyph coverage out of RasterizeShapedText, blended
        // per-pixel inside cmd.bounds (never a whole-bounds solid fill).
        muse::text::StatusOr<runtime::CoverageMask> coverage =
            runtime::RasterizeShapedText(cmd.payload.glyphRunId, cmd.bounds,
                                         cmd.payload.textColor);
        if (!coverage.ok()) return;
        BlendMask(*coverage, cmd.bounds, cmd.payload.textColor);
        return;
      }
      case runtime::PrimitiveKind::Rect:
      case runtime::PrimitiveKind::RoundedRect:
      case runtime::PrimitiveKind::Image: {
        // Card/band ink travels in the cmd payload; kinds left at
        // near-background ink paint (almost) invisibly by construction.
        if (cmd.payload.stateColor == 0) return;
        FillRect(cmd.bounds, cmd.payload.stateColor);
        return;
      }
      case runtime::PrimitiveKind::ElevationShadow: {
        // Shadow wash from the cmd payload; a skipped wash paints nothing.
        if (cmd.payload.shadowColor == 0 ||
            cmd.payload.elevationDp <= 0.0f) {
          return;
        }
        WashRect(cmd.bounds, cmd.payload.shadowColor);
        return;
      }
      default:
        return;
    }
  }

  const std::vector<std::uint32_t>& pixels() const { return pixels_; }
  const std::vector<runtime::DrawCmd>& submitted() const { return submitted_; }
  int w() const { return w_; }
  int h() const { return h_; }

 private:
  static std::uint8_t BlendChannel(std::uint8_t src, std::uint8_t dst,
                                   float a) {
    return static_cast<std::uint8_t>(std::round(src * a + dst * (1.0f - a)));
  }

  void BlendPixel(int x, int y, std::uint32_t src, float a) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    std::uint32_t& d = pixels_[static_cast<std::size_t>(y * w_ + x)];
    const auto ch = [](std::uint32_t v, int s) {
      return static_cast<std::uint8_t>((v >> s) & 0xFFu);
    };
    const std::uint8_t r = BlendChannel(ch(src, 16), ch(d, 16), a);
    const std::uint8_t g = BlendChannel(ch(src, 8), ch(d, 8), a);
    const std::uint8_t b = BlendChannel(ch(src, 0), ch(d, 0), a);
    d = (static_cast<std::uint32_t>(r) << 16) |
        (static_cast<std::uint32_t>(g) << 8) | b;
  }

  void BlendMask(const runtime::CoverageMask& mask,
                 const runtime::Rect& bounds, std::uint32_t ink) {
    for (int my = 0; my < mask.height; ++my) {
      for (int mx = 0; mx < mask.width; ++mx) {
        const float a = mask.alpha[static_cast<std::size_t>(my * mask.width +
                                                            mx)];
        BlendPixel(static_cast<int>(std::round(bounds.x)) + mx,
                   static_cast<int>(std::round(bounds.y)) + my, ink, a);
      }
    }
  }

  void FillRect(const runtime::Rect& bounds, std::uint32_t ink) {
    const int x0 = static_cast<int>(std::round(bounds.x));
    const int y0 = static_cast<int>(std::round(bounds.y));
    const int x1 = x0 + static_cast<int>(std::round(bounds.w));
    const int y1 = y0 + static_cast<int>(std::round(bounds.h));
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) BlendPixel(x, y, ink, 1.0f);
    }
  }

  void WashRect(const runtime::Rect& bounds, std::uint32_t ink) {
    const int x0 = static_cast<int>(std::round(bounds.x));
    const int y0 = static_cast<int>(std::round(bounds.y));
    const int x1 = x0 + static_cast<int>(std::round(bounds.w));
    const int y1 = y0 + static_cast<int>(std::round(bounds.h));
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) BlendPixel(x, y, ink, 0.15f);
    }
  }

  int w_;
  int h_;
  std::vector<std::uint32_t> pixels_;
  std::vector<runtime::DrawCmd> submitted_;
};

std::vector<runtime::GalleryRow> ScriptRows() {
  return {MakeRow("Primary action"), MakeRow("M3 Gallery header"),
          MakeRow("\xe4\xb8\xad\xe6\x96\x87"),  // U+4E2D U+6587 (CJK)
          MakeRow("\xf0\x9f\x98\x80")};         // U+1F600 (emoji)
}

std::vector<runtime::GalleryRow> ManyRows() {
  std::vector<runtime::GalleryRow> rows = ScriptRows();
  for (int i = 0; i < 36; ++i) {
    rows.push_back(MakeComponentRow("Card row " + std::to_string(i),
                                    "Button"));
  }
  return rows;
}

// First ShapedText bounds submitted for a label probe (text paint site).
bool FirstTextBounds(const FakePixelWindow& window, runtime::Rect* out) {
  for (const auto& cmd : window.submitted()) {
    if (cmd.kind == runtime::PrimitiveKind::ShapedText &&
        cmd.payload.glyphRunId != 0) {
      *out = cmd.bounds;
      return true;
    }
  }
  return false;
}

bool PixelsEqual(const std::vector<std::uint32_t>& a,
                 const std::vector<std::uint32_t>& b) {
  return a == b;
}

// (a) TC-TEXT-045/046 (T12-hardened): a Latin/CJK/emoji label presents with
// BOTH ink and background pixels inside its text bounds (glyph shapes
// modulate pixels, not a uniform solid fill), while background outside runs
// stays untouched. Hardening (T12 round-two residual: uniform ~10x28px
// per-glyph cells, exactly 2 gray values interior): a per-cell-constant
// CoverageMask MUST fail this case. Ink-and-background-somewhere-in-bounds
// is necessary but not sufficient — the case additionally requires >2
// distinct blended values inside a SINGLE glyph-cell interior window (edge
// modulation, not just ink-vs-background). A per-cell-constant mask yields
// at most 2 distinct values in EVERY cell-sized window, so it fails here;
// true per-pixel coverage yields >=3 distinct values in at least one cell
// window (edge gradient between full ink and background).
static bool check_text_bounds_have_ink_and_background_variance() {
  runtime::Renderer renderer(baseline_device());
  constexpr int kViewportH = 800;
  FakePixelWindow window(720, kViewportH, kClear);
  std::string error;
  const bool ok = runtime::PresentDeviceGalleryPage(
      ScriptRows(), 0, renderer, window, kViewportH, &error);
  if (!ok) return false;
  if (!error.empty()) return false;
  runtime::Rect bounds;
  if (!FirstTextBounds(window, &bounds)) return false;
  const int x0 = static_cast<int>(std::round(bounds.x));
  const int y0 = static_cast<int>(std::round(bounds.y));
  const int w = static_cast<int>(std::round(bounds.w));
  const int h = static_cast<int>(std::round(bounds.h));
  if (w <= 2 || h <= 2) return false;  // Degenerate bounds prove nothing.
  bool sawInk = false;
  bool sawBackground = false;
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) {
      if (x < 0 || y < 0 || x >= window.w() || y >= window.h()) continue;
      const std::uint32_t px =
          window.pixels()[static_cast<std::size_t>(y * window.w() + x)];
      if (px == kClear) {
        sawBackground = true;
      } else {
        sawInk = true;
      }
    }
  }
  if (!sawInk || !sawBackground) return false;  // Uniform fill: defect 1.
  // T12 cell-interior modulation: slide a glyph-cell-sized window over the
  // bounds interior and require >=3 distinct blended values in at least one
  // window. Cell footprint comes from the round-two residual (~10x28px
  // uniform cells); clamped so small bounds still probe a meaningful
  // interior. A per-cell-constant mask can show at most 2 distinct values
  // (its flat blend + clear) in any such window, so it fails here.
  int cw = 10;
  int ch = 28;
  if (cw > w / 2) cw = w / 2;
  if (ch > h / 2) ch = h / 2;
  if (cw < 4 || ch < 4) return false;  // Degenerate cell proves nothing.
  bool sawModulatedCell = false;
  for (int wy = y0; wy + ch <= y0 + h && !sawModulatedCell; wy += 2) {
    for (int wx = x0; wx + cw <= x0 + w && !sawModulatedCell; wx += 2) {
      std::uint32_t distinct[3];
      int nDistinct = 0;
      for (int y = wy; y < wy + ch; ++y) {
        for (int x = wx; x < wx + cw; ++x) {
          if (x < 0 || y < 0 || x >= window.w() || y >= window.h()) continue;
          const std::uint32_t px =
              window.pixels()[static_cast<std::size_t>(y * window.w() + x)];
          bool known = false;
          for (int i = 0; i < nDistinct; ++i) {
            if (distinct[i] == px) {
              known = true;
              break;
            }
          }
          if (!known) {
            if (nDistinct == 2) {
              // Third distinct value inside ONE cell: edge modulation,
              // not a per-cell constant. No need to scan further.
              sawModulatedCell = true;
              break;
            }
            distinct[nDistinct++] = px;
          }
        }
      }
    }
  }
  if (!sawModulatedCell) return false;  // Uniform per-glyph cells: T12 F1.
  // Background far outside any run stays the clear color.
  const std::uint32_t corner =
      window.pixels()[static_cast<std::size_t>(window.h() - 1) *
                      static_cast<std::size_t>(window.w())];
  return corner == kClear;
}

// (b) TC-TEXT-045: two consecutive pages present pixel-different frames
// (the page index drives emission; no frozen full-list frame).
static bool check_consecutive_pages_present_different_frames() {
  runtime::Renderer renderer(baseline_device());
  constexpr int kViewportH = 400;
  FakePixelWindow first(720, kViewportH, kClear);
  FakePixelWindow second(720, kViewportH, kClear);
  std::string e1;
  std::string e2;
  if (!runtime::PresentDeviceGalleryPage(ManyRows(), 0, renderer, first,
                                         kViewportH, &e1)) {
    return false;
  }
  if (!runtime::PresentDeviceGalleryPage(ManyRows(), 1, renderer, second,
                                         kViewportH, &e2)) {
    return false;
  }
  if (!e1.empty() || !e2.empty()) return false;
  // Identical buffers across an advance are the frozen-frame defect.
  return !PixelsEqual(first.pixels(), second.pixels());
}

// (c) RULE-RUNTIME-001: an out-of-range page fails closed with nothing
// posted — the buffer is bit-identical to its pre-call content (sentinel,
// not cleared-white).
static bool check_out_of_range_page_leaves_buffer_untouched() {
  runtime::Renderer renderer(baseline_device());
  constexpr int kViewportH = 800;
  FakePixelWindow window(720, kViewportH, kSentinel);
  const std::vector<std::uint32_t> before = window.pixels();
  std::string error;
  const bool ok = runtime::PresentDeviceGalleryPage(
      ScriptRows(), 1000000, renderer, window, kViewportH, &error);
  if (ok) return false;
  if (error.empty()) return false;
  if (!PixelsEqual(window.pixels(), before)) return false;
  for (std::uint32_t px : window.pixels()) {
    if (px == kWhite || px == kClear) return false;  // A post happened.
  }
  return window.submitted().empty();
}

// (d) TC-TEXT-045: a card/shadowed row presents pixels distinct from the
// clear color (cards visible, not background-on-background).
static bool check_card_row_pixels_differ_from_background() {
  runtime::Renderer renderer(baseline_device());
  constexpr int kViewportH = 800;
  FakePixelWindow window(720, kViewportH, kClear);
  std::string error;
  const std::vector<runtime::GalleryRow> rows = {
      MakeComponentRow("Elevated card", "Card")};
  const bool ok = runtime::PresentDeviceGalleryPage(rows, 0, renderer, window,
                                                    kViewportH, &error);
  if (!ok) return false;
  if (!error.empty()) return false;
  bool sawCardKind = false;
  for (const auto& cmd : window.submitted()) {
    if (cmd.kind == runtime::PrimitiveKind::Rect ||
        cmd.kind == runtime::PrimitiveKind::RoundedRect ||
        cmd.kind == runtime::PrimitiveKind::Image ||
        cmd.kind == runtime::PrimitiveKind::ElevationShadow) {
      sawCardKind = true;
      break;
    }
  }
  if (!sawCardKind) return false;
  for (std::uint32_t px : window.pixels()) {
    if (px != kClear) return true;
  }
  return false;  // Near-white cards: defect 3.
}

// (e) T13 (round-three residual: stems-only, no bowls/curves/diagonals):
// a curved glyph with an enclosed counter ('O') presents outline-driven
// coverage — the counter hole stays background while the outline paints
// ink. A stem-span stylization fills each glyph cell with a solid vertical
// stem (no hole) and deterministically FAILS both legs below; true outline
// coverage passes. Observed only through blended pixels on the public
// PresentDeviceGalleryPage boundary (no internal-hook assertions), over
// every submitted ShapedText cmd so header/footer cmds cannot mask the
// label under test.
static bool check_curved_glyph_counter_hole_stays_background() {
  runtime::Renderer renderer(baseline_device());
  constexpr int kViewportH = 800;
  FakePixelWindow window(720, kViewportH, kClear);
  std::string error;
  const std::vector<runtime::GalleryRow> rows = {MakeRow("O")};
  const bool ok = runtime::PresentDeviceGalleryPage(rows, 0, renderer, window,
                                                    kViewportH, &error);
  if (!ok) return false;
  if (!error.empty()) return false;
  const auto at = [&](int x, int y) -> std::uint32_t {
    return window.pixels()[static_cast<std::size_t>(y * window.w() + x)];
  };
  const auto inWindow = [&](int x, int y) -> bool {
    return x >= 0 && y >= 0 && x < window.w() && y < window.h();
  };
  // A counter hole is a background pixel strictly enclosed by ink along a
  // scanline: ink on both sides. A single stem-span bar (clear-ink-clear)
  // never encloses background on its long axis, and a solid cell fill or a
  // blank presents no background at all — both fail. A true 'O' ring
  // encloses its hole on BOTH the mid-row and the mid-column, so both legs
  // are required: bar pairs (two stems, top/bottom bars) pass at most one.
  for (const auto& cmd : window.submitted()) {
    if (cmd.kind != runtime::PrimitiveKind::ShapedText) continue;
    if (cmd.payload.glyphRunId == 0) continue;
    const int x0 = static_cast<int>(std::round(cmd.bounds.x));
    const int y0 = static_cast<int>(std::round(cmd.bounds.y));
    const int w = static_cast<int>(std::round(cmd.bounds.w));
    const int h = static_cast<int>(std::round(cmd.bounds.h));
    if (w < 7 || h < 7) continue;  // Too small to enclose a counter.
    bool holeOnRow = false;
    bool holeOnCol = false;
    for (int y = y0; y < y0 + h && !holeOnRow; ++y) {
      for (int x = x0; x < x0 + w && !holeOnRow; ++x) {
        if (!inWindow(x, y) || at(x, y) != kClear) continue;
        bool inkLeft = false;
        bool inkRight = false;
        for (int lx = x0; lx < x; ++lx) {
          if (inWindow(lx, y) && at(lx, y) != kClear) {
            inkLeft = true;
            break;
          }
        }
        for (int rx = x + 1; rx < x0 + w; ++rx) {
          if (inWindow(rx, y) && at(rx, y) != kClear) {
            inkRight = true;
            break;
          }
        }
        holeOnRow = inkLeft && inkRight;
      }
    }
    for (int x = x0; x < x0 + w && !holeOnCol; ++x) {
      for (int y = y0; y < y0 + h && !holeOnCol; ++y) {
        if (!inWindow(x, y) || at(x, y) != kClear) continue;
        bool inkAbove = false;
        bool inkBelow = false;
        for (int uy = y0; uy < y; ++uy) {
          if (inWindow(x, uy) && at(x, uy) != kClear) {
            inkAbove = true;
            break;
          }
        }
        for (int dy = y + 1; dy < y0 + h; ++dy) {
          if (inWindow(x, dy) && at(x, dy) != kClear) {
            inkBelow = true;
            break;
          }
        }
        holeOnCol = inkAbove && inkBelow;
      }
    }
    if (holeOnRow && holeOnCol) return true;
  }
  return false;  // Stems-only coverage: no enclosed counter anywhere.
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
  RUN_CASE(check_text_bounds_have_ink_and_background_variance);
  RUN_CASE(check_consecutive_pages_present_different_frames);
  RUN_CASE(check_out_of_range_page_leaves_buffer_untouched);
  RUN_CASE(check_card_row_pixels_differ_from_background);
  RUN_CASE(check_curved_glyph_counter_hole_stays_background);
  if (failures > 0) {
    std::printf("present_window_text_pixels_test: %d failing\n", failures);
    return 1;
  }
  std::printf("present_window_text_pixels_test: all passing\n");
  return 0;
}
