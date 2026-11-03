// T7d S-spine pin (BUG-TEXT-008 Batch 6, FR-TEXT-016 / AC-TEXT-045).
// Vector-glyph correctness: `S`/`s` render a single open S-spine — two
// stacked left-vented bowls (same HatchRingLeftGap precedent as F1 `3`)
// joined by S's OWN diagonal spine (top-right -> bottom-left stroke across
// the inter-bowl joint, reaching into both bowls as dead-end spurs) —
// never two closed rings (`8`-like, pre-T7b) and never the vented-but-
// spineless `3`-clone (T7b state, caught on device in T7c).
//
// Public boundary: PresentDeviceGalleryPage -> first ShapedText run ->
// RasterizeShapedText (the executed device lane; BuildGlyphOutline runs
// inside it, classified by label-decoded codepoint). No private seam,
// no TileSegs/RasterizeOutlineBlock externals (both TU-local to
// renderer.cpp), no atlas, no device, no GPU, no network, no config,
// no filesystem, no live services. Zero JNI: this TU touches no JVM/JNI
// type, mirroring NFR-TEXT-001.
//
// Shaping lane (ASSUMPTION, documented): the host stub face covers only
// A/B, so post-T9 fail-closed shaping (kMissingGlyph for uncovered input)
// cannot shape S/s/3/8 through the real backend on ANY host lane. The test
// registers the committed NOTAFONT garbage bytes through the public
// test-only RegisterTestOnlyMemoryFont seam (same convention as
// harfbuzz_parity_test.cpp kGarbage), which fails the FT open and routes
// shaping through the hermetic salted-fallback stub (nonzero advances,
// codepoints decoded from the label bytes). Shaping is the faked
// collaborator here; the unit under test — outline topology + coverage —
// runs for real, and every expectation below is drawn from the fixed
// BuildGlyphOutline geometry, never from the stub (stub ids never reach
// the outline stage; CodepointsForRun decodes the label bytes).
//
// RED-state reasoning (no revert performed): pre-T7b `S` shared the two-
// closed-rings branch (`8`-like), so the spur probes below land on
// background and the bowls stay enclosed — cases (1) and (2) fail. The
// T7b no-spine state shares the `3` outline verbatim, so the spur probe
// lands on background and S==3 — cases (1) and (4) fail. The `3` guard
// anchors the F1 spine-right fix against regressions of either kind.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "muse/text/font_discovery.h"
#include "muse/text/shaper.h"
#include "runtime/gallery_present.h"
#include "runtime/renderer.h"

namespace {

// Hermetic shaping lane: unopenable in-memory bytes (committed convention:
// "NOTAFONT" + NUL + 0xFF, cf. harfbuzz_parity_test.cpp kGarbage) so the
// real-font open fails closed and labels shape through the deterministic
// salted-fallback stub. Process-global by design; this TU is standalone
// and shapes only S/s/3/8/empty through it.
const std::uint8_t kNoFont[] = {0x4E, 0x4F, 0x54, 0x41, 0x46,
                                0x4F, 0x4E, 0x54, 0x00, 0xFF};

// Window-backed fake swapchain: records every submitted cmd in order,
// like a presented surface would retain them. No GPU, no ANativeWindow.
// (Same hermetic pattern as present_window_text_test.)
class FakeWindowSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

// T4a device-size bounds: row captions ride EmitCaptionCmd at h=14, the
// size where the S defect showed. Single-glyph labels keep the ink bbox
// a pure function of one outline cell.
runtime::Rect device_bounds() {
  runtime::Rect r;
  r.x = 10.0f;
  r.y = 20.0f;
  r.w = 100.0f;
  r.h = 14.0f;
  return r;
}

// Rasterize one single-glyph label through the public seam
// (PresentDeviceGalleryPage -> first ShapedText run -> RasterizeShapedText)
// at device bounds. Returns an empty mask (width 0) when no run is emitted
// or rasterization fails closed. Never throws.
runtime::CoverageMask mask_for_label(const std::string& label) {
  runtime::GalleryRow row;
  row.label = label;
  const std::vector<runtime::GalleryRow> rows{row};
  runtime::Renderer renderer(baseline_device());
  FakeWindowSwapchain window;
  std::string error;
  if (!runtime::PresentDeviceGalleryPage(rows, 0, renderer, window,
                                         &error)) {
    return runtime::CoverageMask{};
  }
  for (const auto& cmd : window.captured) {
    if (cmd.kind == runtime::PrimitiveKind::ShapedText &&
        cmd.payload.glyphRunId != 0) {
      const muse::text::StatusOr<runtime::CoverageMask> coverage =
          runtime::RasterizeShapedText(cmd.payload.glyphRunId,
                                       device_bounds(), 0xFF112233u);
      if (coverage.ok()) return *coverage;
      return runtime::CoverageMask{};
    }
  }
  return runtime::CoverageMask{};
}

// Number of ink texels (alpha != 0). A blank mask (no run, fail-closed
// rasterization) has zero ink; every case below requires ink so a
// topology tie can never pass vacuously on empty masks. Never throws.
std::size_t ink_texels(const runtime::CoverageMask& mask) {
  std::size_t n = 0;
  for (float a : mask.alpha) {
    if (a != 0.0f) ++n;
  }
  return n;
}

// Tight ink bounding box; false when the mask carries no ink.
bool ink_bbox(const runtime::CoverageMask& mask, int* x0, int* y0, int* x1,
              int* y1) {
  if (mask.width <= 0 || mask.height <= 0) return false;
  int lo_x = mask.width, hi_x = -1, lo_y = mask.height, hi_y = -1;
  for (int y = 0; y < mask.height; ++y) {
    for (int x = 0; x < mask.width; ++x) {
      if (mask.alpha[static_cast<std::size_t>(y) * mask.width + x] !=
          0.0f) {
        if (x < lo_x) lo_x = x;
        if (x > hi_x) hi_x = x;
        if (y < lo_y) lo_y = y;
        if (y > hi_y) hi_y = y;
      }
    }
  }
  if (hi_x < 0) return false;
  *x0 = lo_x;
  *y0 = lo_y;
  *x1 = hi_x;
  *y1 = hi_y;
  return true;
}

bool ink_at(const runtime::CoverageMask& mask, int x, int y) {
  if (x < 0 || y < 0 || x >= mask.width || y >= mask.height) return false;
  return mask.alpha[static_cast<std::size_t>(y) * mask.width + x] != 0.0f;
}

// Background texels reachable from the mask border through background
// (4-connected). Reached interior background proves a vented (open) bowl;
// enclosed counters stay unreached. Never throws.
std::vector<char> border_reached_bg(const runtime::CoverageMask& mask) {
  const std::size_t area =
      static_cast<std::size_t>(mask.width) * mask.height;
  std::vector<char> reached(area, 0);
  if (mask.width <= 0 || mask.height <= 0) return reached;
  if (mask.alpha.size() != area) return reached;
  std::vector<std::size_t> stack;
  for (int x = 0; x < mask.width; ++x) {
    for (int y : {0, mask.height - 1}) {
      const std::size_t i = static_cast<std::size_t>(y) * mask.width + x;
      if (mask.alpha[i] == 0.0f && !reached[i]) {
        reached[i] = 1;
        stack.push_back(i);
      }
    }
  }
  for (int y = 0; y < mask.height; ++y) {
    for (int x : {0, mask.width - 1}) {
      const std::size_t i = static_cast<std::size_t>(y) * mask.width + x;
      if (mask.alpha[i] == 0.0f && !reached[i]) {
        reached[i] = 1;
        stack.push_back(i);
      }
    }
  }
  const int dx[4] = {1, -1, 0, 0};
  const int dy[4] = {0, 0, 1, -1};
  while (!stack.empty()) {
    const std::size_t i = stack.back();
    stack.pop_back();
    const int x = static_cast<int>(i % static_cast<std::size_t>(mask.width));
    const int y = static_cast<int>(i / static_cast<std::size_t>(mask.width));
    for (int d = 0; d < 4; ++d) {
      const int nx = x + dx[d];
      const int ny = y + dy[d];
      if (nx < 0 || ny < 0 || nx >= mask.width || ny >= mask.height) continue;
      const std::size_t j =
          static_cast<std::size_t>(ny) * mask.width + nx;
      if (mask.alpha[j] == 0.0f && !reached[j]) {
        reached[j] = 1;
        stack.push_back(j);
      }
    }
  }
  return reached;
}

// 4-connected ink components: component id per texel (-1 = background).
// Never throws.
std::vector<int> ink_components(const runtime::CoverageMask& mask) {
  const std::size_t area =
      static_cast<std::size_t>(mask.width) * mask.height;
  std::vector<int> comp(area, -1);
  if (mask.width <= 0 || mask.height <= 0) return comp;
  if (mask.alpha.size() != area) return comp;
  int next = 0;
  const int dx[4] = {1, -1, 0, 0};
  const int dy[4] = {0, 0, 1, -1};
  std::vector<std::size_t> stack;
  for (std::size_t s = 0; s < area; ++s) {
    if (mask.alpha[s] == 0.0f || comp[s] != -1) continue;
    comp[s] = next;
    stack.push_back(s);
    while (!stack.empty()) {
      const std::size_t i = stack.back();
      stack.pop_back();
      const int x =
          static_cast<int>(i % static_cast<std::size_t>(mask.width));
      const int y =
          static_cast<int>(i / static_cast<std::size_t>(mask.width));
      for (int d = 0; d < 4; ++d) {
        const int nx = x + dx[d];
        const int ny = y + dy[d];
        if (nx < 0 || ny < 0 || nx >= mask.width || ny >= mask.height)
          continue;
        const std::size_t j =
            static_cast<std::size_t>(ny) * mask.width + nx;
        if (mask.alpha[j] != 0.0f && comp[j] == -1) {
          comp[j] = next;
          stack.push_back(j);
        }
      }
    }
    ++next;
  }
  return comp;
}

// (1) Diagonal spine, upper spur probe: bbox-relative ink inside the top
// bowl along the top-right -> bottom-left diagonal. T7d geometry emits the
// upper dead-end spur from the joint into the top bowl interior
// (HatchStroke cx+rx*0.55 -> cx-rx*0.55 across the joint rows), so this
// texel is ink. Pre-T7b two-closed-rings: bowl interiors are background
// (the `8` topology) -> background -> fail. T7b no-spine clone: identical
// outline to `3`, whose top-bowl interior right-of-center is background
// (left-vented rings stay open at caption size) -> fail. Offsets derived
// from the fixed code (spur endpoint upper-right of joint) and verified
// against the dumped device-size masks (S:(+5,+2), s:(+5,+2)).
bool spur_probe_ink(const runtime::CoverageMask& mask, int dx, int dy,
                    const char* tag) {
  if (ink_texels(mask) == 0) {
    std::printf("  [%s] blank mask\n", tag);
    return false;
  }
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!ink_bbox(mask, &x0, &y0, &x1, &y1)) {
    std::printf("  [%s] no ink bbox\n", tag);
    return false;
  }
  const int px = x0 + dx;
  const int py = y0 + dy;
  if (!ink_at(mask, px, py)) {
    std::printf("  [%s] spur probe (%d,%d) background (bbox x=[%d,%d] "
                "y=[%d,%d])\n",
                tag, px, py, x0, x1, y0, y1);
    return false;
  }
  return true;
}

// (1) `S`: the upper spur probe is ink (fails on rings and on the
// spineless clone, per above).
static bool check_S_spur_probe() {
  return spur_probe_ink(mask_for_label("S"), 5, 2, "S-spur");
}

// (1) `s` (x-height cell, same spine at smaller scale): the upper spur
// probe is ink.
static bool check_s_spur_probe() {
  return spur_probe_ink(mask_for_label("s"), 5, 2, "s-spur");
}

// (1) The spur is a CONNECTED diagonal run, not floating pixels: the
// upper-spur texel, a top-right wall seed, and a bottom-left wall seed all
// lie in one 4-connected ink component (spur -> joint -> walls). A
// touching-rings joint also connects walls, but its bowl interiors are
// background, so the spur probe already excludes it; this leg pins the
// run structure of the fix.
static bool check_S_spine_connected_run() {
  const runtime::CoverageMask m = mask_for_label("S");
  if (ink_texels(m) == 0) {
    std::printf("  [S-run] blank mask\n");
    return false;
  }
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!ink_bbox(m, &x0, &y0, &x1, &y1)) {
    std::printf("  [S-run] no ink bbox\n");
    return false;
  }
  const int probe_x = x0 + 5;
  const int probe_y = y0 + 2;
  const int tr_x = x1;
  const int tr_y = y0 + 1;
  const int bl_x = x0 + 1;
  const int bl_y = y1 - 2;
  if (!ink_at(m, probe_x, probe_y) || !ink_at(m, tr_x, tr_y) ||
      !ink_at(m, bl_x, bl_y)) {
    std::printf("  [S-run] seed background (spur=%d tr=%d bl=%d)\n",
                (int)ink_at(m, probe_x, probe_y),
                (int)ink_at(m, tr_x, tr_y), (int)ink_at(m, bl_x, bl_y));
    return false;
  }
  const std::vector<int> comp = ink_components(m);
  const std::size_t area =
      static_cast<std::size_t>(m.width) * m.height;
  if (comp.size() != area || m.alpha.size() != area) {
    std::printf("  [S-run] mask shape mismatch\n");
    return false;
  }
  const int c_probe =
      comp[static_cast<std::size_t>(probe_y) * m.width + probe_x];
  const int c_tr = comp[static_cast<std::size_t>(tr_y) * m.width + tr_x];
  const int c_bl = comp[static_cast<std::size_t>(bl_y) * m.width + bl_x];
  if (c_probe < 0 || c_probe != c_tr || c_probe != c_bl) {
    std::printf("  [S-run] split run (spur=%d tr=%d bl=%d)\n", c_probe,
                c_tr, c_bl);
    return false;
  }
  return true;
}

// (2) Both bowls open: the two vent background patches inside the S cell
// are reachable from the mask border (left vents), so the bowls never
// form closed rings. Pre-T7b closed rings enclose both interiors
// (unreached) -> fail. Upper patch: bbox-relative (+1,+2); lower patch:
// (+5,+7) (verified against the dumped device-size S mask).
static bool check_S_bowls_open() {
  const runtime::CoverageMask m = mask_for_label("S");
  if (ink_texels(m) == 0) {
    std::printf("  [S-open] blank mask\n");
    return false;
  }
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!ink_bbox(m, &x0, &y0, &x1, &y1)) {
    std::printf("  [S-open] no ink bbox\n");
    return false;
  }
  const int up_x = x0 + 1;
  const int up_y = y0 + 2;
  const int lo_x = x0 + 5;
  const int lo_y = y0 + 7;
  if (ink_at(m, up_x, up_y) || ink_at(m, lo_x, lo_y)) {
    std::printf("  [S-open] vent patch inked (up=%d lo=%d)\n",
                (int)ink_at(m, up_x, up_y), (int)ink_at(m, lo_x, lo_y));
    return false;
  }
  const std::vector<char> reached = border_reached_bg(m);
  const std::size_t area =
      static_cast<std::size_t>(m.width) * m.height;
  if (reached.size() != area) {
    std::printf("  [S-open] reachability shape mismatch\n");
    return false;
  }
  const bool up_open =
      reached[static_cast<std::size_t>(up_y) * m.width + up_x] != 0;
  const bool lo_open =
      reached[static_cast<std::size_t>(lo_y) * m.width + lo_x] != 0;
  if (!up_open || !lo_open) {
    std::printf("  [S-open] vent patch enclosed (up=%d lo=%d)\n",
                (int)up_open, (int)lo_open);
    return false;
  }
  return true;
}

// (2) `s` upper bowl open: the top-bowl vent patch is background and
// border-reached (same left-vent precedent). The lower `s` cell is one
// row shorter, so only the upper vent is pinned here; the lower bowl is
// covered by the `S` legs above through the shared branch.
static bool check_s_upper_bowl_open() {
  const runtime::CoverageMask m = mask_for_label("s");
  if (ink_texels(m) == 0) {
    std::printf("  [s-open] blank mask\n");
    return false;
  }
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!ink_bbox(m, &x0, &y0, &x1, &y1)) {
    std::printf("  [s-open] no ink bbox\n");
    return false;
  }
  const int vx = x0 + 1;
  const int vy = y0 + 2;
  if (ink_at(m, vx, vy)) {
    std::printf("  [s-open] vent patch inked\n");
    return false;
  }
  const std::vector<char> reached = border_reached_bg(m);
  if (reached.size() != static_cast<std::size_t>(m.width) * m.height) {
    std::printf("  [s-open] reachability shape mismatch\n");
    return false;
  }
  if (reached[static_cast<std::size_t>(vy) * m.width + vx] == 0) {
    std::printf("  [s-open] vent patch enclosed\n");
    return false;
  }
  return true;
}

// (3) `3` guard anchor (F1 spine-right, retained): single-`3` keeps zero
// enclosed counters (left-vented, right spine stands), carries ink on the
// right spine in both bowl bands, and differs from `S` (the T7b no-spine
// clone was pixel-comparable to `3`; the T7d diagonal is what separates
// them — S ink extends past the `3` cell on the joint row).
static bool check_3_spine_right_guard() {
  const runtime::CoverageMask m3 = mask_for_label("3");
  const runtime::CoverageMask mS = mask_for_label("S");
  if (ink_texels(m3) == 0 || ink_texels(mS) == 0) {
    std::printf("  [3-guard] blank mask (3=%zu S=%zu)\n", ink_texels(m3),
                ink_texels(mS));
    return false;
  }
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!ink_bbox(m3, &x0, &y0, &x1, &y1)) {
    std::printf("  [3-guard] no ink bbox\n");
    return false;
  }
  // Right spine stands in both bowl bands (vent rows excepted by picking
  // band rows known inked in the fixed table).
  if (!ink_at(m3, x1, y0 + 2) || !ink_at(m3, x1, y0 + 8)) {
    std::printf("  [3-guard] right spine broken\n");
    return false;
  }
  // Zero enclosed counters: every background texel is border-reached.
  const std::vector<char> reached = border_reached_bg(m3);
  for (std::size_t i = 0; i < m3.alpha.size(); ++i) {
    if (m3.alpha[i] == 0.0f && reached[i] == 0) {
      std::printf("  [3-guard] enclosed counter present\n");
      return false;
    }
  }
  // S separates from 3: S ink extends past the `3` cell's right edge on
  // the joint row (spur/joint mass the spineless clone lacks).
  int sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;
  if (!ink_bbox(mS, &sx0, &sy0, &sx1, &sy1)) {
    std::printf("  [3-guard] no S ink bbox\n");
    return false;
  }
  if (sx1 <= x1) {
    std::printf("  [3-guard] S not wider than 3 (S.x1=%d 3.x1=%d)\n", sx1,
                x1);
    return false;
  }
  return true;
}

// Fail-closed anchor: an empty label emits no run, so its mask carries no
// ink. Passes pre- and post-fix; keeps every topology tie above
// non-vacuous. Never throws.
static bool check_empty_label_has_no_ink() {
  return ink_texels(mask_for_label("")) == 0;
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
  muse::text::RegisterTestOnlyMemoryFont(kNoFont, sizeof(kNoFont));
  int failures = 0;
  RUN_CASE(check_S_spur_probe);
  RUN_CASE(check_s_spur_probe);
  RUN_CASE(check_S_spine_connected_run);
  RUN_CASE(check_S_bowls_open);
  RUN_CASE(check_s_upper_bowl_open);
  RUN_CASE(check_3_spine_right_guard);
  RUN_CASE(check_empty_label_has_no_ink);
  if (failures > 0) {
    std::printf("s_spine_test: %d failing\n", failures);
    return 1;
  }
  std::printf("s_spine_test: all passing\n");
  return 0;
}
