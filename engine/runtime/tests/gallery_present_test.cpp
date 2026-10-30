// Gallery bring-up tests — FR-RUNTIME-009 / FR-M3-007 (device gallery).
// The JNI host entry carries the MUI1 tree asset; gallery_present extracts
// the static Showcase rows and rasterizes pages into an RGBA_8888 buffer.
// Hermetic: C++17 + committed runtime headers only; in-memory buffers, no
// ANativeWindow, no GPU, no network, no filesystem.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

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
  if (failures > 0) {
    std::printf("gallery_present_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_present_test: all passing\n");
  return 0;
}
