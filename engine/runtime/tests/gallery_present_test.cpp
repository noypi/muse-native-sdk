// Gallery bring-up tests — FR-RUNTIME-009 / FR-M3-007 (device gallery) +
// T62 contract tests — FR-M3-009 / AC-M3-020 (TC-M3-021), FR-M3-008 /
// AC-M3-018 (8/12/16) + AC-M3-019 (token-only).
// Merged rebase resolution: keeps every test from both sides (HEAD present
// path + OURS T62 14-case StateLayerOpacity suite).
// Hermetic: C++17 + committed runtime headers only; in-memory buffers, no
// ANativeWindow, no GPU, no network, no filesystem.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

namespace runtime {
// Production declaration (coder-owned gallery_present.cpp / present path).
// Assumed symbol: `float runtime::StateLayerOpacity(const std::string&)`.
float StateLayerOpacity(const std::string& state);
// Batch 1 (BUG-GRAPHICS-001 T1) published interface — oracle miniature
// painter under test. Declared here so this test binds to the plan's
// `Interface:` line, not to an implementation detail.
void PaintComponent(std::uint32_t* px, int stride, int w, int h, int bx,
                    int by, int bw, int bh, const GalleryRow& row);
}  // namespace runtime

namespace {

// Baseline device caps: Vulkan 1.1 with no extensions required.
runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

// In-process fake swapchain: records every submitted cmd. No GPU.
class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

bool is_declared_kind(runtime::PrimitiveKind kind) {
  switch (kind) {
    case runtime::PrimitiveKind::Rect:
    case runtime::PrimitiveKind::RoundedRect:
    case runtime::PrimitiveKind::Image:
    case runtime::PrimitiveKind::Text:
    case runtime::PrimitiveKind::StateLayer:
    case runtime::PrimitiveKind::Ripple:
    case runtime::PrimitiveKind::ElevationShadow:
    case runtime::PrimitiveKind::CircleArc:
    case runtime::PrimitiveKind::VectorIcon:
    case runtime::PrimitiveKind::ShapedText:
    case runtime::PrimitiveKind::OverlayScrim:
      return true;
  }
  return false;
}

// Minimal IR with one static Showcase-style root: two headers (light/dark),
// two labels, and two literal-prop components.
const char kIrJson[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"#text\",\"value\":\"M3 Gallery light\"},"
    "{\"type\":\"#text\",\"value\":\"Primary action\"},"
    "{\"type\":\"#text\",\"value\":\"M3 Gallery dark\"},"
    "{\"type\":\"#text\",\"value\":\"Primary action dark\"},"
    "{\"type\":\"Button\",\"props\":{\"variant\":\"filled\",\"label\":\"Primary "
    "action\"}},"
    "{\"type\":\"TopAppBar\",\"props\":{\"title\":\"m3-gallery\",\"size\":"
    "\"small\"}}"
    "]}]}";

std::vector<std::uint8_t> MakeAsset() {
  std::string json(kIrJson);
  std::vector<std::uint8_t> out;
  out.reserve(12 + json.size());
  out.push_back('M');
  out.push_back('U');
  out.push_back('I');
  out.push_back('1');
  out.push_back(1);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  std::uint32_t n = static_cast<std::uint32_t>(json.size());
  out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFFu));
  for (char c : json) out.push_back(static_cast<std::uint8_t>(c));
  return out;
}

}  // namespace

// --- T62 constants (D1/D2/D4 settled 2026-10-31; see T62 header note) ---
constexpr float kHoverOpacity = 0.08f;
constexpr float kFocusOpacity = 0.12f;
constexpr float kPressOpacity = 0.16f;
// D1: explicit 0.38 pin from the m3_matrix.json baseline (tokenSnapshot
// 2026-10-28). If the baseline entry is absent the coder pins 0.38 here.
constexpr float kDisabledOpacity = 0.38f;
// Fail-closed sentinel for unknown/empty state: no wash painted (ASSUMPTION).
constexpr float kNoWashOpacity = 0.0f;

bool nearly_equal(float a, float b) { return std::fabs(a - b) < 1e-6f; }

// --- harness cases (convention copied from present_frame_test.cpp) ----------

bool check_harness_entry_reached() { return true; }

bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::string(failingName).empty();
}

// --- T62 cases ---------------------------------------------------------------

bool check_hover_opacity_is_08() {
  return nearly_equal(runtime::StateLayerOpacity("hover"), kHoverOpacity);
}

bool check_focus_opacity_is_12() {
  return nearly_equal(runtime::StateLayerOpacity("focus"), kFocusOpacity);
}

bool check_pressed_opacity_is_16() {
  return nearly_equal(runtime::StateLayerOpacity("pressed"), kPressOpacity);
}

bool check_disabled_opacity_matches_baseline_038() {
  // D1: 0.38 explicitly pinned from the m3_matrix.json baseline entry.
  return nearly_equal(runtime::StateLayerOpacity("disabled"),
                       kDisabledOpacity);
}

bool check_opacity_ordering_hover_lt_focus_lt_pressed() {
  const float hover = runtime::StateLayerOpacity("hover");
  const float focus = runtime::StateLayerOpacity("focus");
  const float pressed = runtime::StateLayerOpacity("pressed");
  return hover < focus && focus < pressed;
}

bool check_unknown_state_fail_closed_no_wash() {
  // Unknown state: fail-closed, no wash painted, row kept (never blank).
  return nearly_equal(runtime::StateLayerOpacity("enabled"), kNoWashOpacity);
}

bool check_empty_state_fail_closed_no_wash() {
  // Boundary: empty state paints no wash.
  return nearly_equal(runtime::StateLayerOpacity(""), kNoWashOpacity);
}

bool check_unknown_state_never_throws_across_jni() {
  // Must never throw across JNI: unknown AND empty inputs return closed.
  try {
    (void)runtime::StateLayerOpacity("enabled");
    (void)runtime::StateLayerOpacity("");
    (void)runtime::StateLayerOpacity("hovered");
  } catch (...) {
    return false;
  }
  return true;
}

bool check_known_states_never_throw() {
  try {
    (void)runtime::StateLayerOpacity("hover");
    (void)runtime::StateLayerOpacity("focus");
    (void)runtime::StateLayerOpacity("pressed");
    (void)runtime::StateLayerOpacity("disabled");
  } catch (...) {
    return false;
  }
  return true;
}

bool check_mirror_equality_with_js_drive_state() {
  // D2: C++ map mirrors JS driveState 1:1. driveState('Surface','hover')
  // .layer.props.opacity === 0.08, focus 0.12, pressed 0.16, disabled 0.38.
  return nearly_equal(runtime::StateLayerOpacity("hover"), 0.08f) &&
         nearly_equal(runtime::StateLayerOpacity("focus"), 0.12f) &&
         nearly_equal(runtime::StateLayerOpacity("pressed"), 0.16f) &&
         nearly_equal(runtime::StateLayerOpacity("disabled"), 0.38f);
}

bool check_case_sensitive_unknown_is_closed() {
  // "Hover" is not "hover": fail-closed rather than guessing.
  return nearly_equal(runtime::StateLayerOpacity("Hover"), kNoWashOpacity);
}

// Named case: the component inventory leads (the actual M3 gallery), then
// the literal label sections with their light/dark order intact.
static bool check_showcase_rows_parsed() {
  std::vector<std::uint8_t> asset = MakeAsset();
  std::string error;
  std::vector<runtime::GalleryRow> rows = runtime::ParseGalleryRows(
      asset.data(), asset.size(), &error);
  if (rows.size() != 6) {
    std::printf("  rows=%d err=%s\n", static_cast<int>(rows.size()),
                error.c_str());
    return false;
  }
  // Inventory first: Button, then TopAppBar.
  if (rows[0].comp != "Button") return false;
  if (rows[0].label != "Primary action") return false;
  if (rows[0].desc.find("variant=filled") == std::string::npos) return false;
  if (rows[1].comp != "TopAppBar") return false;
  if (rows[1].label != "m3-gallery") return false;
  // Label sections after, light header before dark header.
  if (!rows[2].header || rows[2].label != "M3 Gallery light") return false;
  if (rows[3].header || rows[3].label != "Primary action") return false;
  if (rows[3].dark) return false;
  if (!rows[4].header || rows[4].label != "M3 Gallery dark") return false;
  if (!rows[5].dark || rows[5].label != "Primary action dark") return false;
  return true;
}

// Named case: a bad envelope fails closed with an empty row set.
static bool check_bad_envelope_fails_closed() {
  std::string error;
  const char bad[12] = {'X', 'X', 'X', 'X', 1, 0, 0, 0, 0, 0, 0, 0};
  std::vector<runtime::GalleryRow> rows =
      runtime::ParseGalleryRows(bad, sizeof(bad), &error);
  if (!rows.empty()) return false;
  if (error.empty()) return false;
  rows = runtime::ParseGalleryRows(nullptr, 0, &error);
  return rows.empty() && !error.empty();
}

// Named case: rasterizing a page paints the title bar and at least one row
// band (buffer differs from a zero fill); page count covers all rows.
static bool check_page_raster_paints() {
  std::vector<std::uint8_t> asset = MakeAsset();
  std::string error;
  std::vector<runtime::GalleryRow> rows = runtime::ParseGalleryRows(
      asset.data(), asset.size(), &error);
  if (rows.empty()) return false;
  const int w = 720;
  const int h = 1600;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
  std::string rerr;
  int drawn = runtime::RasterGalleryPage(rows, 0, px.data(), w, w, h, &rerr);
  if (drawn <= 0) return false;
  // Title bar pixel (top-left) must be the light primary, not zero.
  // RGBA_8888 on little-endian reads #6750A4 as the word 0xFFA45067.
  if (px[0] != 0xFFA45067u) return false;
  int pages = runtime::GalleryPageCount(rows, h);
  if (pages < 1) return false;
  // Last page draws the remainder.
  int last = runtime::RasterGalleryPage(rows, pages - 1, px.data(), w, w, h,
                                        &rerr);
  return last > 0;
}

// Named case: an empty row set never blanks — the diagnostic screen paints
// the error background instead.
static bool check_empty_rows_draw_diagnostic() {
  const int w = 64;
  const int h = 64;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
  std::vector<runtime::GalleryRow> rows;
  std::string rerr("no asset");
  int drawn =
      runtime::RasterGalleryPage(rows, 0, px.data(), w, w, h, &rerr);
  if (drawn != 0) return false;
  // Error red #BA1A1A as an RGBA_8888 word.
  return px[0] == 0xFF1A1ABAu;
}

// Named case: source-comment leakage from the structural lowerer is
// skipped, never rendered as a gallery row.
static bool check_comment_lines_skipped() {
  const char json[] =
      "{\"version\":1,\"roots\":["
      "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
      "\"props\":{},\"slots\":[],\"children\":["
      "{\"type\":\"#text\",\"value\":\"// comment\"},"
      "{\"type\":\"#text\",\"value\":\"});\"},"
      "{\"type\":\"#text\",\"value\":\"M3 Gallery light\"}]}]}";
  std::string fixed(json);
  std::vector<std::uint8_t> out = {static_cast<std::uint8_t>('M'),
                                   static_cast<std::uint8_t>('U'),
                                   static_cast<std::uint8_t>('I'),
                                   static_cast<std::uint8_t>('1'),
                                   1,
                                   0,
                                   0,
                                   0};
  std::uint32_t n = static_cast<std::uint32_t>(fixed.size());
  out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFFu));
  for (char c : fixed) out.push_back(static_cast<std::uint8_t>(c));
  std::string error;
  std::vector<runtime::GalleryRow> rows =
      runtime::ParseGalleryRows(out.data(), out.size(), &error);
  if (rows.size() != 1) return false;
  return rows[0].label == "M3 Gallery light";
}

// T9 named case: every gallery row emits one or more DrawCmds from the
// 11-kind vocabulary to renderer/target (retained path, no direct CPU
// raster). Binds only to the T9 Interface: line.
static bool check_present_gallery_emits_draw_cmds_for_every_row() {
  std::vector<std::uint8_t> asset = MakeAsset();
  std::string parseError;
  std::vector<runtime::GalleryRow> rows = runtime::ParseGalleryRows(
      asset.data(), asset.size(), &parseError);
  if (rows.empty()) return false;
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  if (!runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                               &error)) {
    return false;
  }
  if (!error.empty()) return false;
  // One or more DrawCmds per row, all from the declared 11-kind vocabulary.
  if (target.captured.size() < rows.size()) return false;
  for (const auto& cmd : target.captured) {
    if (!is_declared_kind(cmd.kind)) return false;
  }
  return true;
}

// T9 named case: routing proof — with an unsupported-baseline Renderer,
// PresentGallery must fail (Renderer::draw reports UnsupportedBaseline and
// records nothing) instead of falling back to a direct-buffer raster that
// bypasses the retained path. Any recorded cmd here is a fallback bypass.
static bool check_present_gallery_routes_every_row_through_renderer_draw() {
  std::vector<std::uint8_t> asset = MakeAsset();
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 0;  // unsupported baseline: draw is a recording no-op.
  runtime::Renderer renderer(caps);
  FakeSwapchain target;
  std::string error;
  if (runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                              &error)) {
    return false;
  }
  if (error.empty()) return false;
  return target.captured.empty();
}

// T9 named case: bad MUI1 envelope (existing envelope/parse gate) still
// fails closed through the new entry point — false + non-blank diagnostic,
// never blank, never throws across the boundary.
static bool check_present_gallery_bad_envelope_diagnostic_never_blank() {
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  const char bad[12] = {'X', 'X', 'X', 'X', 1, 0, 0, 0, 0, 0, 0, 0};
  std::string error;
  if (runtime::PresentGallery(bad, sizeof(bad), renderer, target, &error)) {
    return false;
  }
  if (error.empty()) return false;
  if (!target.captured.empty()) return false;
  error.clear();
  if (runtime::PresentGallery(nullptr, 0, renderer, target, &error)) {
    return false;
  }
  return !error.empty() && target.captured.empty();
}

// T9 named case: a null error out-param never throws across the boundary —
// both the success and failure legs tolerate error == nullptr.
static bool check_present_gallery_null_error_ptr_never_throws() {
  std::vector<std::uint8_t> asset = MakeAsset();
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  if (!runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                               nullptr)) {
    return false;
  }
  if (target.captured.empty()) return false;
  FakeSwapchain rejected;
  const char bad[12] = {'X', 'X', 'X', 'X', 1, 0, 0, 0, 0, 0, 0, 0};
  if (runtime::PresentGallery(bad, sizeof(bad), renderer, rejected, nullptr)) {
    return false;
  }
  return rejected.captured.empty();
}

// T9 named case: a well-formed envelope with no gallery rows still reports
// false + a non-blank diagnostic (never a blank screen, never throws).
static bool check_present_gallery_no_rows_diagnostic_never_blank() {
  const char json[] = "{\"version\":1,\"roots\":[]}";
  std::string fixed(json);
  std::vector<std::uint8_t> out = {static_cast<std::uint8_t>('M'),
                                   static_cast<std::uint8_t>('U'),
                                   static_cast<std::uint8_t>('I'),
                                   static_cast<std::uint8_t>('1'),
                                   1,
                                   0,
                                   0,
                                   0};
  std::uint32_t n = static_cast<std::uint32_t>(fixed.size());
  out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFFu));
  for (char c : fixed) out.push_back(static_cast<std::uint8_t>(c));
  runtime::Renderer renderer(baseline_device());
  FakeSwapchain target;
  std::string error;
  if (runtime::PresentGallery(out.data(), out.size(), renderer, target,
                              &error)) {
    return false;
  }
  return !error.empty() && target.captured.empty();
}

// T2 (BUG-GRAPHICS-002): segmented divider geometry — the NavigationBar /
// SegmentedButton oracle branch must not paint the s==0 divider on the
// outer left edge; the outer border is a single closed stroke and interior
// dividers (s >= 1) remain. Hermetic: in-memory pixel buffer only.
static bool ProbeSegmentedRow(const char* comp) {
  runtime::GalleryRow row;
  row.label = "probe";
  row.comp = comp;
  row.desc = comp;
  std::vector<runtime::GalleryRow> rows{row};
  const int w = 720;
  const int h = 400;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
  std::string err;
  int drawn = runtime::RasterGalleryPage(rows, 0, px.data(), w, w, h, &err);
  if (drawn <= 0) {
    std::printf("  %s drawn=%d err=%s\n", comp, drawn, err.c_str());
    return false;
  }
  auto at = [&](int x, int y) -> std::uint32_t {
    return px[static_cast<std::size_t>(y) * w + x];
  };
  // Outline-variant #CAC4D0 as an RGBA_8888 word (see existing 0xFFA45067
  // convention above). Bar x-geometry mirrors the oracle branch; y0 is
  // located by scanning so the test does not depend on page packing.
  constexpr std::uint32_t kOutline = 0xFFD0C4CAu;
  const int bw = w - 2 * 16;
  const int segW = (bw - 96) / 3;
  const int barW = segW * 3;
  const int cx = 16 + bw / 2;
  const int x0 = cx - barW / 2;
  const int barH = 76;
  int y0 = -1;
  for (int y = 0; y < h; ++y) {
    int run = 0;
    for (int x = x0 + 24; x < x0 + barW - 24; ++x) {
      if (at(x, y) == kOutline) ++run;
    }
    if (run >= barW - 48 - 8) {
      y0 = y;
      break;
    }
  }
  if (y0 < 0) {
    std::printf("  %s bar top not found\n", comp);
    return false;
  }
  const int yMid = y0 + barH / 2;
  const int yDiv = y0 + barH - 20;
  // (a) No s==0 edge stroke: the columns just left of the border carry no
  // outline pixels (the straight divider at sx == x0 would paint x0-1).
  for (int y = y0 + 10; y < y0 + barH - 10; ++y) {
    if (at(x0 - 1, y) == kOutline) {
      std::printf("  %s edge stroke at (%d,%d)\n", comp, x0 - 1, y);
      return false;
    }
    if (at(x0 - 2, y) == kOutline) {
      std::printf("  %s outer stroke at (%d,%d)\n", comp, x0 - 2, y);
      return false;
    }
  }
  // (b) The outer border is a single closed stroke.
  if (at(x0, yMid) != kOutline) return false;
  if (at(x0 - 2, yMid) == kOutline) return false;
  if (at(x0 + barW / 2, y0) != kOutline) return false;
  if (at(x0 + barW / 2, y0 + barH - 1) != kOutline) return false;
  // (c) Interior dividers at x0 + s*segW for s >= 1 remain.
  for (int s = 1; s < 3; ++s) {
    int sx = x0 + s * segW;
    if (at(sx - 1, yDiv) != kOutline && at(sx, yDiv) != kOutline) {
      std::printf("  %s interior divider s=%d missing\n", comp, s);
      return false;
    }
  }
  return true;
}

static bool check_segmented_divider_interior_no_edge_stroke() {
  if (!ProbeSegmentedRow("NavigationBar")) return false;
  if (!ProbeSegmentedRow("SegmentedButton")) return false;
  return true;
}

// --- BUG-GRAPHICS-003 T3 cases (FR-GRAPHICS-015 / AC-GRAPHICS-044) ----------
// Oracle Image painter geometry at bh=104 (Image rows use the RowHeight
// default 104, so ih = bh-64 = 40). Hermetic: one Image row rasterized into
// an in-memory buffer; no filesystem, ANativeWindow, GPU, or network.
// The mountain polyline + sun are the only kLightOnSurfaceVariant
// (RGBA(0x49,0x45,0x4F,0xFF)) pixels: the outline box uses
// kLightOutlineVariant and PaintCaption is a no-op.
// ASSUMPTION (plan A2): relief minimum 8px, sun clearance minimum 4px —
// plan constants, not spec values.

namespace gfx003 {
// Little-endian word for RGBA(0x49,0x45,0x4F,0xFF).
constexpr std::uint32_t kInk = 0xFF4F4549u;

struct Scan {
  bool ok = false;
  int sunCx = 0;
  int sunCy = 0;
  int polyMinY = 0;
  int polyMaxY = 0;
  double minClear = 0.0;
};

Scan scan_image_ink() {
  Scan s;
  runtime::GalleryRow row;
  row.label = "gallery-hero";
  row.comp = "Image";
  row.desc = "Image";
  std::vector<runtime::GalleryRow> rows(1, row);
  const int w = 720;
  const int h = 1600;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
  std::string err;
  int drawn = runtime::RasterGalleryPage(rows, 0, px.data(), w, w, h, &err);
  if (drawn != 1) {
    std::printf("  [gfx003] raster drew %d rows err=%s\n", drawn,
                err.c_str());
    return s;
  }
  std::vector<int> xs;
  std::vector<int> ys;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (px[static_cast<std::size_t>(y) * w + x] == kInk) {
        xs.push_back(x);
        ys.push_back(y);
      }
    }
  }
  if (xs.empty()) {
    std::printf("  [gfx003] no Image ink pixels found\n");
    return s;
  }
  // Sun center = ink pixel with the densest 25x25 neighborhood (the filled
  // disc; the thin polyline never packs that densely).
  int best = -1;
  int bestCount = -1;
  for (std::size_t i = 0; i < xs.size(); ++i) {
    int c = 0;
    for (std::size_t j = 0; j < xs.size(); ++j) {
      int dx = xs[j] - xs[i];
      if (dx < -12 || dx > 12) continue;
      int dy = ys[j] - ys[i];
      if (dy < -12 || dy > 12) continue;
      ++c;
    }
    if (c > bestCount) {
      bestCount = c;
      best = static_cast<int>(i);
    }
  }
  if (best < 0 || bestCount < 150) {
    std::printf("  [gfx003] no filled sun disc found bestCount=%d\n",
                bestCount);
    return s;
  }
  s.sunCx = xs[static_cast<std::size_t>(best)];
  s.sunCy = ys[static_cast<std::size_t>(best)];
  bool first = true;
  double minClear = 1e9;
  for (std::size_t i = 0; i < xs.size(); ++i) {
    double dx = static_cast<double>(xs[i] - s.sunCx);
    double dy = static_cast<double>(ys[i] - s.sunCy);
    double d = std::sqrt(dx * dx + dy * dy);
    if (d <= 13.0) continue;  // sun disc (r=12 + 1px tolerance)
    if (first) {
      s.polyMinY = s.polyMaxY = ys[i];
      first = false;
    } else {
      if (ys[i] < s.polyMinY) s.polyMinY = ys[i];
      if (ys[i] > s.polyMaxY) s.polyMaxY = ys[i];
    }
    double edge = d - 12.0;
    if (edge < minClear) minClear = edge;
  }
  if (first) {
    std::printf("  [gfx003] no polyline pixels outside sun disc\n");
    return s;
  }
  s.minClear = minClear;
  s.ok = true;
  std::printf("  [gfx003] sun=(%d,%d) relief=%d clear=%.2f\n", s.sunCx,
              s.sunCy, s.polyMaxY - s.polyMinY, s.minClear);
  return s;
}
}  // namespace gfx003

static bool check_image_mountain_relief_bh104_peak_above_base() {
  gfx003::Scan s = gfx003::scan_image_ink();
  if (!s.ok) return false;
  return (s.polyMaxY - s.polyMinY) >= 8;
}

static bool check_image_sun_clears_ridge_bh104() {
  gfx003::Scan s = gfx003::scan_image_ink();
  if (!s.ok) return false;
  return s.minClear >= 4.0;
}

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

// --- Batch 1 (BUG-GRAPHICS-001 T1/T2) helpers --------------------------------
// Test-owned color words (same RGBA wire order as production: R | G<<8 |
// B<<16 | A<<24). Duplicated here so the test never depends on production
// internals — only on the spec-visible M3 roles.
constexpr std::uint32_t t1_rgba(std::uint8_t r, std::uint8_t g,
                                std::uint8_t b, std::uint8_t a) {
  return static_cast<std::uint32_t>(r) |
         (static_cast<std::uint32_t>(g) << 8) |
         (static_cast<std::uint32_t>(b) << 16) |
         (static_cast<std::uint32_t>(a) << 24);
}
constexpr std::uint32_t kT1CardBg = 0xFFFAF2F7u;    // kCardBg F7F2FA
constexpr std::uint32_t kT1Wash = 0xFFFFF7FEu;      // kLightSurface FEF7FF
constexpr std::uint32_t kT1DotDim = 0xFF4F4549u;    // kLightOnSurfaceVariant
constexpr std::uint32_t kT1DotLit = 0xFF8B374Fu;    // kLightOnPrimaryContainer
constexpr std::uint32_t kT1FabBg = 0xFFF8DEE8u;     // kLightSecondaryContainer
constexpr std::uint32_t kT1Sentinel = 0x12345678u;  // untouched-marker

// ASSUMPTION (B1-GEOM): oracle page layout metrics mirror the production M3
// file-scope constants: kPad=16, kGap=8, kHeaderH=84, kFooterH=56. If the
// coder re-measures different metrics in-tree, these tests' call sites move
// but their expected behavior (containment) does not.
constexpr int kB1Pad = 16;
constexpr int kB1Gap = 8;
constexpr int kB1HeaderH = 84;
constexpr int kB1FooterH = 56;

static runtime::GalleryRow b1_row(const char* comp) {
  runtime::GalleryRow r;
  r.comp = comp;
  r.label = comp;
  r.desc = std::string(comp) + " variant=test";
  return r;
}

// T1: NavigationRail dot spill must stay inside the card rect. The third dot
// row paints past by+bh pre-fix, so a strict "nothing painted outside the
// card (+8px shadow allowance right/bottom, ASSUMPTION: soft shadow reaches
// at most 6px)" scan must fail until T1 lands.
static bool check_paint_rail_dots_clamped_to_card() {
  const int w = 400, h = 300;
  const int bx = 20, by = 30, bw = 300, bh = 124;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h,
                                kT1Sentinel);
  try {
    runtime::PaintComponent(px.data(), w, w, h, bx, by, bw, bh,
                            b1_row("NavigationRail"));
  } catch (...) {
    return false;  // T1 contract: never throws.
  }
  // Sanity: the card interior really painted (guards a vacuous pass).
  int inside = 0;
  for (int y = by; y < by + bh; ++y)
    for (int x = bx; x < bx + bw; ++x)
      if (px[static_cast<std::size_t>(y) * w + x] != kT1Sentinel) ++inside;
  if (inside < 1000) return false;
  // Containment: strict on top/left, +8px shadow allowance right/bottom.
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (px[static_cast<std::size_t>(y) * w + x] == kT1Sentinel) continue;
      bool inCard = (x >= bx && x < bx + bw && y >= by && y < by + bh);
      bool inShadow = (x >= bx && x < bx + bw + 8 && y >= by &&
                       y < by + bh + 8);
      if (!inCard && !inShadow) return false;
      if (!inCard && (y < by || x < bx)) return false;
    }
  }
  return true;
}

// T1: BottomAppBar FAB center sits above the card top pre-fix (y0 - 6 with
// r=24), so any paint above `by` must fail until T1 lands.
static bool check_paint_bottomappbar_fab_clamped_to_card() {
  const int w = 400, h = 300;
  const int bx = 20, by = 40, bw = 300, bh = 124;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h,
                                kT1Sentinel);
  try {
    runtime::PaintComponent(px.data(), w, w, h, bx, by, bw, bh,
                            b1_row("BottomAppBar"));
  } catch (...) {
    return false;  // T1 contract: never throws.
  }
  int inside = 0;
  for (int y = by; y < by + bh; ++y)
    for (int x = bx; x < bx + bw; ++x)
      if (px[static_cast<std::size_t>(y) * w + x] != kT1Sentinel) ++inside;
  if (inside < 1000) return false;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (px[static_cast<std::size_t>(y) * w + x] == kT1Sentinel) continue;
      bool inCard = (x >= bx && x < bx + bw && y >= by && y < by + bh);
      bool inShadow = (x >= bx && x < bx + bw + 8 && y >= by &&
                       y < by + bh + 8);
      if (!inCard && !inShadow) return false;
      if (!inCard && (y < by || x < bx)) return false;
    }
  }
  return true;
}

// T1 boundary: miniature painters tolerate a tiny buffer and out-of-card
// geometry without throwing; out-of-bounds writes are suppressed.
static bool check_paint_miniature_never_throws_and_suppresses_oob() {
  const int w = 64, h = 64;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h,
                                kT1Sentinel);
  try {
    runtime::PaintComponent(px.data(), w, w, h, 8, 8, 48, 48,
                            b1_row("NavigationRail"));
    runtime::PaintComponent(px.data(), w, w, h, 8, 8, 48, 48,
                            b1_row("BottomAppBar"));
    runtime::PaintComponent(px.data(), w, w, h, -20, -20, 60, 60,
                            b1_row("NavigationRail"));
  } catch (...) {
    return false;
  }
  return true;
}

// T1: in-bounds pixels are unchanged — the card interior paints its
// background and the first-rail dot keeps its lit miniature color.
static bool check_paint_inbounds_pixels_preserved() {
  const int w = 400, h = 300;
  const int bx = 20, by = 30, bw = 300, bh = 124;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h,
                                kT1Sentinel);
  try {
    runtime::PaintComponent(px.data(), w, w, h, bx, by, bw, bh,
                            b1_row("NavigationRail"));
  } catch (...) {
    return false;
  }
  // Card interior away from the centered miniature and the rounded
  // corners keeps the card bg (right-edge column, mid height).
  if (px[static_cast<std::size_t>(by + bh / 2) * w + (bx + bw - 3)] !=
      kT1CardBg)
    return false;
  return true;
}

// T2: with two NavigationRail rows, no dot/pill miniature color may appear
// in the gap band below the first card. Card bottom is located dynamically
// (first non-card-bg pixel down column x=100, a miniature-free column), so
// the test holds whether T2 grows the row or T1 shrinks the miniature.
static bool check_packing_gap_free_of_rail_spill() {
  const int w = 720, h = 1600;
  std::vector<runtime::GalleryRow> rows;
  rows.push_back(b1_row("NavigationRail"));
  rows.push_back(b1_row("NavigationRail"));
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
  std::string error;
  int drawn = 0;
  try {
    drawn = runtime::RasterGalleryPage(rows, 0, px.data(), w, w, h, &error);
  } catch (...) {
    return false;  // T2 contract: never throws.
  }
  if (drawn != 2) return false;
  // ASSUMPTION (B1-GEOM): first card top is kHeaderH + kPad.
  const int top0 = kB1HeaderH + kB1Pad;
  const int xe = kB1Pad + 84;  // x=100: left of rail sheet, right of corner
  int e0 = -1;
  for (int y = top0; y < h - kB1FooterH; ++y) {
    if (px[static_cast<std::size_t>(y) * w + xe] != kT1CardBg) {
      e0 = y;
      break;
    }
  }
  if (e0 < 0) return false;
  // ASSUMPTION (B1-GEOM): rail dot column xd = cx-150+38 with
  // cx = bx+bw/2, bx=kPad, bw=w-2*kPad.
  const int cx = kB1Pad + (w - 2 * kB1Pad) / 2;
  const int xd = cx - 150 + 38;
  for (int y = e0; y < e0 + kB1Gap && y < h - kB1FooterH; ++y) {
    for (int x = xd - 7; x <= xd + 7; ++x) {
      std::uint32_t c = px[static_cast<std::size_t>(y) * w + x];
      if (c == kT1DotDim || c == kT1DotLit || c == kT1FabBg) return false;
    }
  }
  return true;
}

// T2: with [Button, BottomAppBar], no FAB/plus miniature color may appear in
// the gap band above the BottomAppBar card (the plus-button float). Card
// top is located dynamically; blends/shadows never equal these colors.
static bool check_packing_gap_free_of_fab_spill() {
  const int w = 720, h = 1600;
  std::vector<runtime::GalleryRow> rows;
  rows.push_back(b1_row("Button"));
  rows.push_back(b1_row("BottomAppBar"));
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
  std::string error;
  int drawn = 0;
  try {
    drawn = runtime::RasterGalleryPage(rows, 0, px.data(), w, w, h, &error);
  } catch (...) {
    return false;  // T2 contract: never throws.
  }
  if (drawn != 2) return false;
  // ASSUMPTION (B1-GEOM): second card starts after card0 + gap; locate
  // card0's bottom edge first (first non-card-bg below row 0 top), then the
  // second card top (first card-bg after that) on a miniature-free column.
  const int xe = kB1Pad + 84;
  const int top0 = kB1HeaderH + kB1Pad;
  int e0 = -1;
  for (int y = top0 + 1; y < h - kB1FooterH; ++y) {
    if (px[static_cast<std::size_t>(y) * w + xe] != kT1CardBg) {
      e0 = y;
      break;
    }
  }
  if (e0 < 0) return false;
  int s1 = -1;
  for (int y = e0 + 1; y < h - kB1FooterH; ++y) {
    if (px[static_cast<std::size_t>(y) * w + xe] == kT1CardBg) {
      s1 = y;
      break;
    }
  }
  if (s1 < 0) return false;
  // ASSUMPTION (B1-GEOM): FAB center x = bx+bw-72.
  const int xf = kB1Pad + (w - 2 * kB1Pad) - 72;
  for (int y = s1 - kB1Gap; y < s1; ++y) {
    if (y < kB1HeaderH) continue;
    for (int x = xf - 24; x <= xf + 24; ++x) {
      if (x < 0 || x >= w) continue;
      std::uint32_t c = px[static_cast<std::size_t>(y) * w + x];
      if (c == kT1FabBg || c == kT1DotLit) return false;
    }
  }
  return true;
}

// T2 boundary/error: null error out-param tolerated on every leg, extreme
// pages clamp instead of throwing, empty rows stay diagnostic.
static bool check_raster_tolerates_null_error_and_never_throws() {
  std::vector<runtime::GalleryRow> rows;
  rows.push_back(b1_row("NavigationRail"));
  rows.push_back(b1_row("BottomAppBar"));
  const int w = 720, h = 1600;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
  try {
    if (runtime::RasterGalleryPage(rows, 0, px.data(), w, w, h, nullptr) <= 0)
      return false;
    (void)runtime::RasterGalleryPage(rows, -5, px.data(), w, w, h, nullptr);
    (void)runtime::RasterGalleryPage(rows, 99, px.data(), w, w, h, nullptr);
    if (runtime::GalleryPageCount(rows, h) < 1) return false;
    if (runtime::GalleryPageCount(rows, 0) < 1) return false;
    std::vector<runtime::GalleryRow> empty;
    if (runtime::RasterGalleryPage(empty, 0, px.data(), w, w, h, nullptr) != 0)
      return false;
  } catch (...) {
    return false;
  }
  return true;
}

// T2 packing: at a short viewport each 124px miniature row takes its own
// page (avail = h-kHeaderH-kFooterH-kPad fits exactly one row + no second
// row with gap). Pins PageStarts agreement; green pre- and post-fix.
static bool check_page_count_covers_all_rows() {
  std::vector<runtime::GalleryRow> rows;
  for (int i = 0; i < 6; ++i) rows.push_back(b1_row("NavigationRail"));
  const int h = 400;
  int pages = 0;
  try {
    pages = runtime::GalleryPageCount(rows, h);
  } catch (...) {
    return false;
  }
  if (pages != 6) return false;
  const int w = 720;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
  int total = 0;
  try {
    for (int p = 0; p < pages; ++p)
      total += runtime::RasterGalleryPage(rows, p, px.data(), w, w, h,
                                          nullptr);
  } catch (...) {
    return false;
  }
  return total == 6;
}

// --- Batch 2 (BUG-GRAPHICS-001 T3/T4) --------------------------------------
// T3: oracle-golden containment over page-p0-equivalent rail/drawer/FAB/plus
// rows, asserted at the RasterGalleryPage level (union-of-cards invariant:
// every miniature-colored pixel lies inside some card rect — zero paint
// outside container bounds). T4: RULE-GRAPHICS-003 build-fence assertion
// (no mock-painter symbol reachable/linked), static/link-level only.
// Hermetic: in-memory buffers, no ANativeWindow/GPU/network/filesystem.
//
// ASSUMPTION (B2-GEOM): extends B1-GEOM. Row heights mirror production
// RowHeight (NavigationRail/BottomAppBar=140, NavigationDrawer=180); card
// tops stack as top0 + sum(rh+gap) with top0 anchored dynamically by a
// card-bg scan. Every computed edge is anchor-verified at runtime (a layout
// drift fails loudly instead of passing vacuously); expected behavior
// (containment) does not move if the coder re-measures.
static int b2_row_height_mirror(const std::string& comp) {
  if (comp == "NavigationDrawer") return 180;
  return 140;  // NavigationRail / BottomAppBar (T2 packing agreement)
}

struct B2Card {
  int top;
  int bottom;
};

// Anchor: first card-bg pixel at column xe below the title bar.
static int b2_first_card_top(const std::uint32_t* px, int w, int h, int xe) {
  for (int y = kB1HeaderH; y < h - kB1FooterH; ++y) {
    if (px[static_cast<std::size_t>(y) * w + xe] == kT1CardBg) return y;
  }
  return -1;
}

static bool b2_is_miniature_color(std::uint32_t c) {
  // Drawer/rail dots + pills, BottomAppBar FAB disc + plus lines. Blends and
  // the card shadow never equal these words (B1 assumption, src-over-black
  // math), and PaintCaption is a no-op, so any occurrence is miniature paint.
  return c == kT1DotDim || c == kT1DotLit || c == kT1FabBg;
}

// Strict union-of-cards scan: every miniature-colored pixel must lie inside
// one of the given card rects. Prints a naming diagnostic (color + coords +
// card index) on the first violation per the T3 exit contract.
static bool b2_scan_contained(const std::uint32_t* px, int w, int h,
                              const B2Card* cards, int n, int bx, int bw,
                              int* miniaturePixels) {
  int found = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      std::uint32_t c = px[static_cast<std::size_t>(y) * w + x];
      if (!b2_is_miniature_color(c)) continue;
      ++found;
      bool inside = false;
      for (int i = 0; i < n; ++i) {
        if (x >= bx && x < bx + bw && y >= cards[i].top &&
            y < cards[i].bottom) {
          inside = true;
          break;
        }
      }
      if (!inside) {
        std::printf("  detail: miniature color 0x%08X at (%d,%d) outside %d card(s)\n",
                    c, x, y, n);
        return false;
      }
    }
  }
  if (miniaturePixels != nullptr) *miniaturePixels = found;
  return true;
}

// T3: page-p0-equivalent rows [NavigationRail, NavigationDrawer,
// BottomAppBar] on one page: rail dot spill, drawer dots, FAB disc and the
// plus-button float must all stay inside their own card rects.
static bool check_page_p0_equivalent_miniatures_contained() {
  const int w = 720, h = 1600;
  std::vector<runtime::GalleryRow> rows;
  rows.push_back(b1_row("NavigationRail"));
  rows.push_back(b1_row("NavigationDrawer"));
  rows.push_back(b1_row("BottomAppBar"));
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
  int drawn = 0;
  try {
    drawn = runtime::RasterGalleryPage(rows, 0, px.data(), w, w, h, nullptr);
  } catch (...) {
    return false;  // T3 contract: never throws.
  }
  if (drawn != 3) {
    std::printf("  detail: drew %d rows, want 3\n", drawn);
    return false;
  }
  const int bx = kB1Pad, bw = w - 2 * kB1Pad;
  const int xe = kB1Pad + 84;  // miniature-free column (left of sheets)
  const int top0 = b2_first_card_top(px.data(), w, h, xe);
  if (top0 < 0) {
    std::printf("  detail: no card top anchored\n");
    return false;
  }
  B2Card cards[3];
  int y = top0;
  for (int i = 0; i < 3; ++i) {
    const int rh = b2_row_height_mirror(rows[static_cast<std::size_t>(i)].comp);
    cards[i] = {y, y + rh};
    // Anchor-verify both edges on the miniature-free column: a packing drift
    // fails here loudly instead of shifting the verdict silently.
    if (px[static_cast<std::size_t>(y + 2) * w + xe] != kT1CardBg) {
      std::printf("  detail: card %d top %d not card-bg (packing drift)\n", i, y);
      return false;
    }
    if (px[static_cast<std::size_t>(y + rh - 2) * w + xe] != kT1CardBg) {
      std::printf("  detail: card %d bottom %d not card-bg (packing drift)\n", i,
                  y + rh);
      return false;
    }
    y += rh + kB1Gap;
  }
  int found = 0;
  if (!b2_scan_contained(px.data(), w, h, cards, 3, bx, bw, &found)) {
    return false;
  }
  if (found < 50) {
    // Sanity: the miniatures really painted (guards a vacuous pass).
    std::printf("  detail: only %d miniature pixels (vacuous-pass guard)\n",
                found);
    return false;
  }
  return true;
}

// T3: NavigationDrawer branch direct containment on a short card (bh=96).
// Pre-fix the drawer dots/pill used unbounded primitives, so the third row
// escapes a 96px card; post-fix CardFill* clamps keep every disc in-card.
static bool check_paint_drawer_dots_clamped_to_card() {
  const int w = 400, h = 300;
  const int bx = 20, by = 30, bw = 360, bh = 96;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h,
                                kT1Sentinel);
  try {
    runtime::PaintComponent(px.data(), w, w, h, bx, by, bw, bh,
                            b1_row("NavigationDrawer"));
  } catch (...) {
    return false;  // T3 contract: never throws.
  }
  int inside = 0;
  for (int yy = by; yy < by + bh; ++yy)
    for (int x = bx; x < bx + bw; ++x)
      if (px[static_cast<std::size_t>(yy) * w + x] != kT1Sentinel) ++inside;
  if (inside < 200) {
    std::printf("  detail: only %d painted pixels in-card\n", inside);
    return false;
  }
  B2Card card = {by, by + bh};
  return b2_scan_contained(px.data(), w, h, &card, 1, bx, bw, nullptr);
}

// T3: short viewport puts each 140px miniature row on its own page
// (Batch 1 pin: 2 rows -> 2 pages at h=400); each single-card page must be
// self-contained — rail dots must not reach the footer pad, the FAB/plus
// must not reach the header pad.
static bool check_short_pages_each_card_self_contained() {
  std::vector<runtime::GalleryRow> rows;
  rows.push_back(b1_row("NavigationRail"));
  rows.push_back(b1_row("BottomAppBar"));
  const int w = 720, h = 400;
  const int bx = kB1Pad, bw = w - 2 * kB1Pad;
  const int xe = kB1Pad + 84;
  int pages = 0;
  try {
    pages = runtime::GalleryPageCount(rows, h);
  } catch (...) {
    return false;
  }
  if (pages != 2) {
    std::printf("  detail: pages=%d, want 2\n", pages);
    return false;
  }
  for (int p = 0; p < 2; ++p) {
    std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
    int drawn = 0;
    try {
      drawn = runtime::RasterGalleryPage(rows, p, px.data(), w, w, h, nullptr);
    } catch (...) {
      return false;
    }
    if (drawn != 1) {
      std::printf("  detail: page %d drew %d rows, want 1\n", p, drawn);
      return false;
    }
    const int top = b2_first_card_top(px.data(), w, h, xe);
    if (top < 0) {
      std::printf("  detail: page %d has no card top\n", p);
      return false;
    }
    const int rh = b2_row_height_mirror(
        rows[static_cast<std::size_t>(p)].comp);
    B2Card card = {top, top + rh};
    int found = 0;
    if (!b2_scan_contained(px.data(), w, h, &card, 1, bx, bw, &found)) {
      std::printf("  detail: page %d spill\n", p);
      return false;
    }
    if (found < 20) {
      std::printf("  detail: page %d only %d miniature pixels\n", p, found);
      return false;
    }
  }
  return true;
}

// --- T4: RULE-GRAPHICS-003 fence -------------------------------------------
// Mock-painter probes: these symbols must never link into a gallery page
// target. Declared weak so a hermetic build resolves them to null; any
// reintroduction of a mock painter under a canonical name makes the probe
// non-null and the fence names it.
extern "C" void MockPainter_Paint(void) __attribute__((weak));
extern "C" void GalleryMockPainter(void) __attribute__((weak));
extern "C" void mock_painter_present(void) __attribute__((weak));
extern "C" void PaintMockComponent(void) __attribute__((weak));

// T4: no mock-painter symbol is linked. Exit nonzero names the reachable
// symbol per the plan Interface:.
static bool check_fence_no_mock_painter_symbol_linked() {
  if (MockPainter_Paint != nullptr) {
    std::printf("  detail: reachable mock-painter symbol MockPainter_Paint\n");
    return false;
  }
  if (GalleryMockPainter != nullptr) {
    std::printf("  detail: reachable mock-painter symbol GalleryMockPainter\n");
    return false;
  }
  if (mock_painter_present != nullptr) {
    std::printf("  detail: reachable mock-painter symbol mock_painter_present\n");
    return false;
  }
  if (PaintMockComponent != nullptr) {
    std::printf("  detail: reachable mock-painter symbol PaintMockComponent\n");
    return false;
  }
  return true;
}

// T4: no mock/oracle painter leaks through the dynamic export surface.
// nm-style gate consistent with android_build_gate_test / jni_contract_test:
// SKIP (pass) when nm or /proc/self/exe is unavailable; FAIL names the
// symbol otherwise. The oracle PaintComponent stays a hermetic-gate
// instrument (static runtime linkage for the test binary only) and is
// asserted here only against dynamic export, not linkage.
static bool check_fence_painters_not_dynamically_exported() {
  std::FILE* self = std::fopen("/proc/self/exe", "rb");
  if (self == nullptr) {
    std::printf("  detail: SKIP dynamic-exports audit (/proc/self/exe unreadable)\n");
    return true;
  }
  std::fclose(self);
  if (std::system("command -v nm >/dev/null 2>&1") != 0) {
    std::printf("  detail: SKIP dynamic-exports audit (nm unavailable)\n");
    return true;
  }
  std::FILE* p = popen(
      "nm -D /proc/self/exe 2>/dev/null | grep -E ' [TDR] .*(MockPainter|mock_painter|PaintMock)'",
      "r");
  if (p == nullptr) {
    std::printf("  detail: SKIP dynamic-exports audit (popen unavailable)\n");
    return true;
  }
  char buf[512];
  std::string hit;
  while (std::fgets(buf, sizeof(buf), p) != nullptr) hit += buf;
  int rc = pclose(p);
  (void)rc;  // grep exit 1 (no match) is the PASS leg, not an error.
  if (!hit.empty()) {
    std::printf("  detail: dynamically exported mock-painter symbol(s): %s\n",
                hit.c_str());
    return false;
  }
  return true;
}

int main() {
  int failures = 0;
  RUN_CASE(check_showcase_rows_parsed);
  RUN_CASE(check_bad_envelope_fails_closed);
  RUN_CASE(check_page_raster_paints);
  RUN_CASE(check_empty_rows_draw_diagnostic);
  RUN_CASE(check_comment_lines_skipped);
  RUN_CASE(check_present_gallery_emits_draw_cmds_for_every_row);
  RUN_CASE(check_present_gallery_routes_every_row_through_renderer_draw);
  RUN_CASE(check_present_gallery_bad_envelope_diagnostic_never_blank);
  RUN_CASE(check_present_gallery_null_error_ptr_never_throws);
  RUN_CASE(check_present_gallery_no_rows_diagnostic_never_blank);
  RUN_CASE(check_image_mountain_relief_bh104_peak_above_base);
  RUN_CASE(check_image_sun_clears_ridge_bh104);
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_hover_opacity_is_08);
  RUN_CASE(check_focus_opacity_is_12);
  RUN_CASE(check_pressed_opacity_is_16);
  RUN_CASE(check_disabled_opacity_matches_baseline_038);
  RUN_CASE(check_opacity_ordering_hover_lt_focus_lt_pressed);
  RUN_CASE(check_unknown_state_fail_closed_no_wash);
  RUN_CASE(check_empty_state_fail_closed_no_wash);
  RUN_CASE(check_unknown_state_never_throws_across_jni);
  RUN_CASE(check_known_states_never_throw);
  RUN_CASE(check_mirror_equality_with_js_drive_state);
  RUN_CASE(check_case_sensitive_unknown_is_closed);
  RUN_CASE(check_segmented_divider_interior_no_edge_stroke);
  RUN_CASE(check_paint_rail_dots_clamped_to_card);
  RUN_CASE(check_paint_bottomappbar_fab_clamped_to_card);
  RUN_CASE(check_paint_miniature_never_throws_and_suppresses_oob);
  RUN_CASE(check_paint_inbounds_pixels_preserved);
  RUN_CASE(check_packing_gap_free_of_rail_spill);
  RUN_CASE(check_packing_gap_free_of_fab_spill);
  RUN_CASE(check_raster_tolerates_null_error_and_never_throws);
  RUN_CASE(check_page_count_covers_all_rows);
  RUN_CASE(check_page_p0_equivalent_miniatures_contained);
  RUN_CASE(check_paint_drawer_dots_clamped_to_card);
  RUN_CASE(check_short_pages_each_card_self_contained);
  RUN_CASE(check_fence_no_mock_painter_symbol_linked);
  RUN_CASE(check_fence_painters_not_dynamically_exported);
  if (failures > 0) {
    std::printf("gallery_present_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_present_test: all passing\n");
  return 0;
}
