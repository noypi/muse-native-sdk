// TC-RUNTIME-056 RED — FR-RUNTIME-002 / AC-RUNTIME-047.
// Every retained DrawCmd.bounds and every raster row frame consumed by
// gallery_present must equal the solved LayoutEngine::boxOf(node) for that
// node; manual `cursor += rh + kGap` packing is forbidden as the geometry
// authority and the `catch (...) -> laid=false` manual-cursor fallback in
// the raster path is removed (fail closed instead).
//
// Hermetic: C++17 + committed runtime headers only; in-process buffers and
// an in-process recording Swapchain. No network, no env/config reads, no
// filesystem, no live services, no device.
// Binds ONLY to public headers: runtime/layout.h, runtime/tree.h,
// runtime/gallery_present.h, runtime/renderer.h.
// CONFLICT-RUNTIME-009 scope: runtime owns the binding; layout solver and
// graphics blit are consumed, never modified.
//
// RED STATE (expected on current tree): retained emission and raster row
// frames are produced by manual cursor packing with the M3 chrome offset
// (kHeaderH) applied outside layout, so literal bounds-vs-boxOf equality
// FAILS (off by the chrome constant). The cases below pin the spec-literal
// contract; the coder binds emission to LayoutEngine::boxOf and removes
// the fallback (see handoff), then these cases turn green.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/layout.h"
#include "runtime/renderer.h"
#include "runtime/tree.h"

namespace {

// M3 file-scope metrics mirrored from gallery_present.cpp (test-local
// copies; production owns the originals).
constexpr float kPad = 16.0f;
constexpr float kGap = 8.0f;
constexpr int kPresentW = 720;

constexpr std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                             std::uint8_t a) {
  return static_cast<std::uint32_t>(r) |
         (static_cast<std::uint32_t>(g) << 8) |
         (static_cast<std::uint32_t>(b) << 16) |
         (static_cast<std::uint32_t>(a) << 24);
}
// kDarkSurface band fill vs kLightPrimary title bar vs kLightSurface wash.
constexpr std::uint32_t kDarkBand = rgba(0x14, 0x12, 0x18, 0xFF);
constexpr std::uint32_t kTitleBar = rgba(0x67, 0x50, 0xA4, 0xFF);
constexpr std::uint32_t kPageWash = rgba(0xFE, 0xF7, 0xFF, 0xFF);

// Dark text rows: RowHeight 64, empty label (shaping gate skipped), empty
// desc (theme gate resolves: empty means light), dark band fill distinct
// from both the title bar and the page wash so the raster probe can locate
// row frames by pixel.
std::vector<runtime::GalleryRow> make_dark_text_rows(int n) {
  std::vector<runtime::GalleryRow> rows;
  for (int i = 0; i < n; ++i) {
    runtime::GalleryRow row;
    row.label = "";
    row.comp = "";
    row.desc = "";
    row.dark = true;
    row.header = false;
    rows.push_back(row);
  }
  return rows;
}

// Reference layout for n 64px rows: column root at (0,0), padding kPad,
// gap kGap, width kPresentW, exact-fit height. boxOf(i+1) is the spec
// authority for row i's frame.
bool build_reference_layout(int n, runtime::RuntimeTree& tree,
                            runtime::LayoutEngine& engine) {
  std::vector<runtime::CompiledNode> nodes;
  nodes.reserve(static_cast<std::size_t>(n) + 1);
  runtime::CompiledNode root;
  root.parent = -1;
  root.style.direction = runtime::Direction::Column;
  root.style.justify = runtime::Justify::Start;
  root.style.align = runtime::Align::Start;
  root.style.padding = kPad;
  root.style.gap = kGap;
  root.style.width = static_cast<float>(kPresentW);
  root.style.height =
      2.0f * kPad + static_cast<float>(n) * 64.0f +
      (n > 1 ? static_cast<float>(n - 1) * kGap : 0.0f);
  nodes.push_back(root);
  for (int i = 0; i < n; ++i) {
    runtime::CompiledNode child;
    child.parent = 0;
    child.style.width = static_cast<float>(kPresentW) - 2.0f * kPad;
    child.style.height = 64.0f;
    nodes.push_back(child);
  }
  if (runtime::instantiate(nodes.data(), nodes.size(), tree) != 0) return false;
  if (tree.size() != static_cast<std::size_t>(n) + 1) return false;
  engine.compute(tree, 0);
  return true;
}

bool same(float a, float b) { return a == b; }

bool box_equals(const runtime::LayoutBox& box, float x, float y, float w,
                float h) {
  return same(box.x, x) && same(box.y, y) && same(box.width, w) &&
         same(box.height, h);
}

bool bounds_equal_box(const runtime::Rect& bounds,
                      const runtime::LayoutBox& box) {
  return same(bounds.x, box.x) && same(bounds.y, box.y) &&
         same(bounds.w, box.width) && same(bounds.h, box.height);
}

}  // namespace

// AC-RUNTIME-047 (retained): every retained DrawCmd.bounds equals the solved
// LayoutEngine::boxOf(node). Full-window emission; the per-row band cmd
// (RoundedRect, nodeId i+1) must match boxOf(i+1) literally.
static bool check_retained_bounds_equal_layout_box() {
  auto rows = make_dark_text_rows(3);
  runtime::RuntimeTree tree;
  runtime::LayoutEngine engine;
  if (!build_reference_layout(3, tree, engine)) return false;
  // Sanity on the authority itself.
  if (!box_equals(engine.boxOf(1), 16.0f, 16.0f, 688.0f, 64.0f)) return false;
  if (!box_equals(engine.boxOf(2), 16.0f, 88.0f, 688.0f, 64.0f)) return false;
  if (!box_equals(engine.boxOf(3), 16.0f, 160.0f, 688.0f, 64.0f)) return false;

  runtime::RealizedWindow window;
  window.firstVisible = 0;
  window.lastVisible = 2;
  window.prefetchAhead = 0;
  std::vector<runtime::DrawCmd> out;
  runtime::EmitGalleryWindowCmds(rows, window, out);
  // Per text row: band (RoundedRect) + caption (Text).
  for (int i = 0; i < 3; ++i) {
    const std::uint32_t node = static_cast<std::uint32_t>(i + 1);
    const runtime::DrawCmd* band = nullptr;
    for (const auto& cmd : out) {
      if (cmd.nodeId == node &&
          cmd.kind == runtime::PrimitiveKind::RoundedRect) {
        band = &cmd;
        break;
      }
    }
    if (band == nullptr) return false;
    if (!bounds_equal_box(band->bounds,
                           engine.boxOf(static_cast<runtime::NodeId>(node)))) {
      std::printf("  node %u retained bounds {%.1f,%.1f,%.1f,%.1f} != "
                  "boxOf {%.1f,%.1f,%.1f,%.1f}\n",
                  node, band->bounds.x, band->bounds.y, band->bounds.w,
                  band->bounds.h, engine.boxOf(node).x, engine.boxOf(node).y,
                  engine.boxOf(node).width, engine.boxOf(node).height);
      return false;
    }
  }
  return true;
}

// AC-RUNTIME-047 (retained, windowed): the same equality holds for a windowed
// slice; cursor-prefix accumulation over skipped rows must not move frames
// off boxOf.
static bool check_windowed_bounds_equal_layout_box() {
  auto rows = make_dark_text_rows(3);
  runtime::RuntimeTree tree;
  runtime::LayoutEngine engine;
  if (!build_reference_layout(3, tree, engine)) return false;

  runtime::RealizedWindow window;
  window.firstVisible = 1;
  window.lastVisible = 1;
  window.prefetchAhead = 0;
  std::vector<runtime::DrawCmd> out;
  runtime::EmitGalleryWindowCmds(rows, window, out);
  const runtime::DrawCmd* band = nullptr;
  for (const auto& cmd : out) {
    if (cmd.nodeId == 2u && cmd.kind == runtime::PrimitiveKind::RoundedRect) {
      band = &cmd;
      break;
    }
  }
  if (band == nullptr) return false;
  if (!bounds_equal_box(band->bounds, engine.boxOf(2))) {
    std::printf("  windowed node 2 bounds {%.1f,%.1f,%.1f,%.1f} != boxOf "
                "{%.1f,%.1f,%.1f,%.1f}\n",
                band->bounds.x, band->bounds.y, band->bounds.w,
                band->bounds.h, engine.boxOf(2).x, engine.boxOf(2).y,
                engine.boxOf(2).width, engine.boxOf(2).height);
    return false;
  }
  return true;
}

// AC-RUNTIME-047 (raster): every raster row frame equals boxOf(node). The
// row band interior is probed by pixel at the layout-derived frame center:
// with the binding in place the pixel is the dark band fill; under manual
// cursor packing the layout-derived frame center still shows title/wash.
// NOTE (TC-056 D1): the probe samples the band CENTER, not the frame
// origin — PaintTextRow fills a RoundedRect with r=18, so a +4 origin probe
// lands in the corner cutout (outside the band) even when the frame is
// exactly boxOf. The frame==boxOf assertion is unchanged; only the sample
// point moves into the band interior.
static bool check_raster_row_frames_equal_layout_box() {
  auto rows = make_dark_text_rows(2);
  runtime::RuntimeTree tree;
  runtime::LayoutEngine engine;
  if (!build_reference_layout(2, tree, engine)) return false;

  const int kW = 720;
  const int kH = 400;  // fits both rows: avail = 400-84-56-16 = 244.
  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kW * kH),
                                    kPageWash);
  std::string error;
  int drawn = runtime::RasterGalleryPage(rows, 0, pixels.data(), kW, kW, kH,
                                         &error);
  if (drawn != 2) {
    std::printf("  raster drew %d rows, want 2 (error: %s)\n", drawn,
                error.c_str());
    return false;
  }
  bool ok = true;
  for (int i = 0; i < 2; ++i) {
    const runtime::LayoutBox& box =
        engine.boxOf(static_cast<runtime::NodeId>(i + 1));
    // Band interior (center): clear of the r=18 rounded corners on all
    // sides (bw=688, bh=64), still strictly derived from the boxOf frame.
    const int px = static_cast<int>(box.x + box.width * 0.5f);
    const int py = static_cast<int>(box.y + box.height * 0.5f);
    const std::uint32_t got =
        pixels[static_cast<std::size_t>(py * kW + px)];
    if (got != kDarkBand) {
      std::printf("  row %d layout frame (%d,%d) pixel 0x%08X, want band "
                  "0x%08X (title 0x%08X, wash 0x%08X)\n",
                  i, px, py, got, kDarkBand, kTitleBar, kPageWash);
      ok = false;
    }
  }
  return ok;
}

// AC-RUNTIME-047 (fallback absence): the raster path owns no manual-cursor
// fallback — a single row's layout-derived frame is painted exactly there.
// The probe samples the band center (r=18 corners cut out the origin);
// under the `catch (...) -> laid=false` cursor fallback regime this probe
// observes title/wash instead of the band fill.
static bool check_raster_single_row_no_cursor_fallback() {
  auto rows = make_dark_text_rows(1);
  runtime::RuntimeTree tree;
  runtime::LayoutEngine engine;
  if (!build_reference_layout(1, tree, engine)) return false;

  const int kW = 720;
  const int kH = 300;
  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kW * kH),
                                    kPageWash);
  std::string error;
  int drawn = runtime::RasterGalleryPage(rows, 0, pixels.data(), kW, kW, kH,
                                         &error);
  if (drawn != 1) {
    std::printf("  raster drew %d rows, want 1 (error: %s)\n", drawn,
                error.c_str());
    return false;
  }
  const runtime::LayoutBox& box = engine.boxOf(1);
  const int px = static_cast<int>(box.x + box.width * 0.5f);
  const int py = static_cast<int>(box.y + box.height * 0.5f);
  const std::uint32_t got = pixels[static_cast<std::size_t>(py * kW + px)];
  if (got != kDarkBand) {
    std::printf("  single-row layout frame (%d,%d) pixel 0x%08X, want band "
                "0x%08X\n",
                px, py, got, kDarkBand);
    return false;
  }
  return true;
}

// Guard (already green): an empty window emits nothing — the bind/remove
// must not regress the fail-closed empty legs.
static bool check_empty_window_emits_nothing() {
  auto rows = make_dark_text_rows(2);
  runtime::RealizedWindow window;
  window.firstVisible = 0;
  window.lastVisible = -1;
  window.prefetchAhead = 0;
  std::vector<runtime::DrawCmd> out;
  runtime::EmitGalleryWindowCmds(rows, window, out);
  return out.empty();
}

#define RUN_CASE(name)                                   \
  do {                                                   \
    if (!(name)()) {                                     \
      std::printf("FAIL %s\n", #name);                   \
      ++failures;                                        \
    } else {                                             \
      std::printf("PASS %s\n", #name);                   \
    }                                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_retained_bounds_equal_layout_box);
  RUN_CASE(check_windowed_bounds_equal_layout_box);
  RUN_CASE(check_raster_row_frames_equal_layout_box);
  RUN_CASE(check_raster_single_row_no_cursor_fallback);
  RUN_CASE(check_empty_window_emits_nothing);
  if (failures > 0) {
    std::printf("layout_to_draw_test: %d failing\n", failures);
    return 1;
  }
  std::printf("layout_to_draw_test: all passing\n");
  return 0;
}
