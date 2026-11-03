// T6 — Swapchain rasterization of ShapedText runs via slug_msdf/tile_raster.
// FR-TEXT-016 / AC-TEXT-045 (device text is shaped vector text rasterized
// through the engine stack) + NFR-TEXT-001 (zero JNI on the per-frame path)
// + AC-GRAPHICS-002 (Slug MSDF text path).
//
// Public boundary: the plan T6 `Interface:` line (see unit-tests/T6.md for
// the signature ASSUMPTION). Hermetic: C++17 + committed runtime headers
// only; in-memory run ids/bounds, no device, no GPU, no network, no config,
// no filesystem, no live services. Zero JNI by construction: this TU
// touches no JVM/JNI type, mirroring the NFR-TEXT-001 constraint.
//
// Binds to the plan T6 `Interface:` line (see unit-tests/T6.md).
// RED STATE (expected): `RasterizeShapedText` does not exist anywhere in
// the tree — the retained `ShapedText` cmd is only forwarded by
// `renderer.cpp` (`target.submit(cmd)`) and the CPU `RasterGalleryPage`
// oracle still paints text with `DrawText`. This TU therefore fails to
// compile until the coder wires run → slug_msdf::shapeAndPlace texel
// records + tile_raster coverage into Swapchain rasterization. The
// pre-existing `renderer.draw` glyphRunId == 0 → UnknownPrimitive
// fail-closed stays covered by render_smoke_test (006, untouched).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// T4a — vector-text glyph-topology legs (AC-RUNTIME-035 vector-text leg,
// AC-M3-027). Root cause (T4 diagnosis, authoritative): BuildGlyphOutline()
// (~lines 104–390, renderer.cpp — coder-owned, never touched here) maps
// codepoint 'g' (103) and uppercase B/M/P/R to no branch (bare-stem
// default) and renders '3' on the closed-ring branch shared with S/s/5
// (indistinguishable from '8'). The cases below pin the fixed topologies
// through the same public seam as the T6 legs above
// (PresentDeviceGalleryPage -> RasterizeShapedText) using the failing
// labels "m3-gallery" (carries '3' + 'g') and "NavigationBar"
// (carries 'g' + 'B'; its 'r' is lowercase, already covered by the
// arches branch). The existing "Gallery" label uses only covered
// codepoints, so it cannot observe the defect.

#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

// Iteration-4 real-geometry TU header: the production gallery path publishes
// per-glyph texels through the T3 GlyphAtlas (insert-only) while the raster
// path resolves runs through the shaping-cache seam, never through
// atlas::lookup. This header exposes the atlas type so the dead-lookup pin
// below can assert lookup behavior hermetically.
#include "muse/text/glyph_atlas.h"

namespace {

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

// A genuine T5-produced run id: shaped through the engine stack via the
// public PresentDeviceGalleryPage seam (the coder's in-process
// shaper/atlas seam — handles are assigned from 0x1000, so the old
// hardcoded placeholder 7 can never resolve by design: unknown ids fail
// closed with no synthesis). Returns 0 when no run was emitted.
std::uint32_t known_run_from_engine() {
  runtime::GalleryRow row;
  row.label = "Gallery";
  const std::vector<runtime::GalleryRow> rows{row};
  runtime::Renderer renderer(baseline_device());
  FakeWindowSwapchain window;
  std::string error;
  if (!runtime::PresentDeviceGalleryPage(rows, 0, renderer, window,
                                         &error)) {
    return 0;
  }
  for (const auto& cmd : window.captured) {
    if (cmd.kind == runtime::PrimitiveKind::ShapedText &&
        cmd.payload.glyphRunId != 0) {
      return cmd.payload.glyphRunId;
    }
  }
  return 0;
}
// A run id no atlas entry covers: the dangling-id error path.
constexpr std::uint32_t kDanglingRun = 0xDEADu;

runtime::Rect test_bounds();  // defined with the T6 legs below.
runtime::Rect device_bounds();  // h=14 device caption size (EmitCaptionCmd).

// T4a helper: rasterize an arbitrary single-row label through the public
// seam (PresentDeviceGalleryPage -> first ShapedText run ->
// RasterizeShapedText) in explicit bounds with the fixed color.
// Returns an empty mask (width 0) when no run is emitted or rasterization
// fails closed. Never throws.
runtime::CoverageMask mask_for_label_in(const std::string& label,
                                       const runtime::Rect& bounds) {
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
          runtime::RasterizeShapedText(cmd.payload.glyphRunId, bounds,
                                       0xFF112233u);
      if (coverage.ok()) return *coverage;
      return runtime::CoverageMask{};
    }
  }
  return runtime::CoverageMask{};
}

// T4a wrapper: same seam at the legacy T6 bounds (100x28). Keeps the six
// pre-existing cases unchanged.
runtime::CoverageMask mask_for_label(const std::string& label) {
  return mask_for_label_in(label, test_bounds());
}

// T4a helper: number of ink texels (alpha != 0). A blank mask (no run,
// fail-closed rasterization) has zero ink; every T4a case requires ink on
// both sides so a hole-topology tie can never pass vacuously on empty
// masks, pre- or post-fix. Never throws.
std::size_t ink_texels(const runtime::CoverageMask& mask) {
  std::size_t n = 0;
  for (float a : mask.alpha) {
    if (a != 0.0f) ++n;
  }
  return n;
}

// T4a helper: number of enclosed-counter components (background regions
// unreachable from the mask border through background texels). Ring
// counters rasterize background (renderer.cpp: "Ring counters stay
// segment-free, so enclosed holes rasterize background"), so each closed
// bowl contributes one component while an open (gap-vented) topology
// contributes none. Translation- and advance-invariant: pen shifts cannot
// change the count, only outline topology can. Never throws.
std::size_t count_enclosed_counters(const runtime::CoverageMask& mask) {
  if (mask.width <= 0 || mask.height <= 0) return 0;
  const std::size_t area =
      static_cast<std::size_t>(mask.width) * mask.height;
  if (mask.alpha.size() != area) return 0;
  std::vector<char> reached(area, 0);
  std::vector<std::size_t> stack;
  // Seed flood fill with every border background texel.
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
  while (!stack.empty()) {
    const std::size_t i = stack.back();
    stack.pop_back();
    const int x = static_cast<int>(i % static_cast<std::size_t>(mask.width));
    const int y = static_cast<int>(i / static_cast<std::size_t>(mask.width));
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
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
  // Each unreached background component is one enclosed counter.
  std::size_t holes = 0;
  std::vector<char> seen(area, 0);
  for (std::size_t i = 0; i < area; ++i) {
    if (mask.alpha[i] != 0.0f || reached[i] || seen[i]) continue;
    ++holes;
    std::vector<std::size_t> comp{i};
    seen[i] = 1;
    while (!comp.empty()) {
      const std::size_t k = comp.back();
      comp.pop_back();
      const int x = static_cast<int>(k % static_cast<std::size_t>(mask.width));
      const int y = static_cast<int>(k / static_cast<std::size_t>(mask.width));
      const int dx[4] = {1, -1, 0, 0};
      const int dy[4] = {0, 0, 1, -1};
      for (int d = 0; d < 4; ++d) {
        const int nx = x + dx[d];
        const int ny = y + dy[d];
        if (nx < 0 || ny < 0 || nx >= mask.width || ny >= mask.height) continue;
        const std::size_t j =
            static_cast<std::size_t>(ny) * mask.width + nx;
        if (mask.alpha[j] == 0.0f && !reached[j] && !seen[j]) {
          seen[j] = 1;
          comp.push_back(j);
        }
      }
    }
  }
  return holes;
}

// T4a ('3' topology, AC-RUNTIME-035 vector-text leg): the '3'-mask differs
// from the '8'-mask. Pre-fix '3' shares the closed-ring branch with
// S/s/5 (two stacked full rings, indistinguishable from '8'), so both
// labels carry identical counter topology and this returns false. Post-fix
// '3' is opened (C/G gapRightHalf pattern) while '8' stays closed, so the
// counts differ. Guards (ink on both sides) keep the tie non-vacuous.
static bool check_3_mask_differs_from_8_mask() {
  const runtime::CoverageMask m3 = mask_for_label("m3-gallery");
  const runtime::CoverageMask m8 = mask_for_label("m8-gallery");
  const std::size_t ink3 = ink_texels(m3);
  const std::size_t ink8 = ink_texels(m8);
  if (ink3 == 0 || ink8 == 0) {
    std::printf("  [3-vs-8] blank mask (ink3=%zu ink8=%zu)\n", ink3, ink8);
    return false;
  }
  const std::size_t holes3 = count_enclosed_counters(m3);
  const std::size_t holes8 = count_enclosed_counters(m8);
  if (holes3 == holes8) {
    std::printf("  [3-vs-8] same counter topology (holes=%zu ink3=%zu ink8=%zu)\n",
                holes3, ink3, ink8);
    return false;
  }
  return true;
}

// T4a ('g' topology, AC-RUNTIME-035 vector-text leg / AC-M3-027): the
// 'g'-mask differs from the bare-stem 'I'-mask. Pre-fix 'g' falls to the
// bare-stem default (a single vertical bar like 'I'), so replacing 'g'
// with 'I' in "NavigationBar" leaves the counter topology unchanged and
// this returns false. Post-fix 'g' gains its bowl/counter while the twin
// keeps the bare stem, so the counts differ. ('B' is equally bare-stem
// pre-fix but present in both labels, so it cancels out; the lowercase
// 'r' is already covered by the arches branch.)
static bool check_g_mask_differs_from_stem_I_mask() {
  const runtime::CoverageMask g =
      mask_for_label("NavigationBar");
  const runtime::CoverageMask i =
      mask_for_label("NaviIationBar");
  const std::size_t inkG = ink_texels(g);
  const std::size_t inkI = ink_texels(i);
  if (inkG == 0 || inkI == 0) {
    std::printf("  [g-vs-I] blank mask (inkG=%zu inkI=%zu)\n", inkG, inkI);
    return false;
  }
  const std::size_t holesG = count_enclosed_counters(g);
  const std::size_t holesI = count_enclosed_counters(i);
  if (holesG == holesI) {
    std::printf("  [g-vs-I] same counter topology (holes=%zu inkG=%zu inkI=%zu)\n",
                holesG, inkG, inkI);
    return false;
  }
  return true;
}

// T4a boundary: an empty label emits no run, so its mask carries no
// counters (fail-closed, never synthesized). Anchors the counter
// primitive at zero; passes pre- and post-fix.
static bool check_empty_label_has_no_counters() {
  const runtime::CoverageMask m = mask_for_label("");
  return count_enclosed_counters(m) == 0;
}

runtime::Rect test_bounds() {
  runtime::Rect r;
  r.x = 10.0f;
  r.y = 20.0f;
  r.w = 100.0f;
  r.h = 28.0f;
  return r;
}

// T4a device-size bounds: row captions ride EmitCaptionCmd at h=14. The
// old TU used h=28 (2x) and stayed green while the device garbled; the
// h=14 legs below pin the size where the defect was visible.
runtime::Rect device_bounds() {
  runtime::Rect r;
  r.x = 10.0f;
  r.y = 20.0f;
  r.w = 100.0f;
  r.h = 14.0f;
  return r;
}

// AC-TEXT-045: a known T5-produced run id rasterizes vector coverage
// (nonzero pixels) in bounds with the given color. T11 surface:
// RasterizeShapedText returns StatusOr<CoverageMask>; ok() is the
// success leg (the old DrawError::None shape).
static bool check_known_run_rasterizes_coverage() {
  const std::uint32_t known = known_run_from_engine();
  if (known == 0) return false;
  const muse::text::StatusOr<runtime::CoverageMask> coverage =
      runtime::RasterizeShapedText(known, test_bounds(), 0xFF112233u);
  if (!coverage.ok()) return false;
  // Vector coverage (nonzero pixels) in bounds: stems leave background
  // pixels inside every bound, so both ink and background must exist.
  bool any_ink = false;
  for (float a : coverage->alpha) {
    if (a != 0.0f) {
      any_ink = true;
      break;
    }
  }
  return any_ink;
}

// RULE-RUNTIME-001: an unknown/dangling run id fails closed with
// kMissingGlyph — the failed Status the caller maps onto
// DrawError::UnknownPrimitive (extends the glyphRunId == 0 sentinel rule)
// — and records nothing: never a substitute box or bitmap fallback.
static bool check_unknown_run_returns_unknown_primitive() {
  const muse::text::StatusOr<runtime::CoverageMask> coverage =
      runtime::RasterizeShapedText(kDanglingRun, test_bounds(), 0xFF112233u);
  return !coverage.ok() &&
         coverage.status() == muse::text::Status::kMissingGlyph;
}

// RULE-RUNTIME-001: run id 0 is the dangling sentinel (never a valid
// supplied handle) and fails closed the same way.
static bool check_zero_run_is_dangling() {
  const muse::text::StatusOr<runtime::CoverageMask> coverage =
      runtime::RasterizeShapedText(0u, test_bounds(), 0xFF112233u);
  return !coverage.ok() &&
         coverage.status() == muse::text::Status::kMissingGlyph;
}

// T4a device-size leg (h=14, EmitCaptionCmd size): "m3-gallery" carries
// exactly 2 enclosed counters while "m8-gallery" carries 4, so the labels
// differ. Pre-fix at h=14 the pair tied 2v2 (coder scratch, hermetic —
// the TU never executed red in-tree for this leg), so the `!=` alone is
// the regression pin and the exact counts pin the fixed table.
static bool check_3_vs_8_at_device_size() {
  const runtime::Rect b = device_bounds();
  const runtime::CoverageMask m3 = mask_for_label_in("m3-gallery", b);
  const runtime::CoverageMask m8 = mask_for_label_in("m8-gallery", b);
  const std::size_t ink3 = ink_texels(m3);
  const std::size_t ink8 = ink_texels(m8);
  if (ink3 == 0 || ink8 == 0) {
    std::printf("  [h14 3-vs-8] blank mask (ink3=%zu ink8=%zu)\n", ink3,
                ink8);
    return false;
  }
  const std::size_t holes3 = count_enclosed_counters(m3);
  const std::size_t holes8 = count_enclosed_counters(m8);
  if (holes3 != 2 || holes8 != 4) {
    std::printf(
        "  [h14 3-vs-8] holes3=%zu (want 2) holes8=%zu (want 4) ink3=%zu "
        "ink8=%zu\n",
        holes3, holes8, ink3, ink8);
    return false;
  }
  return holes3 != holes8;
}

// T4a device-size leg (h=14): "NavigationBar" carries exactly 3 enclosed
// counters while "NaviIationBar" carries 2. Pre-fix at h=14 the pair tied
// 2v2 (coder scratch), so `!=` is the regression pin and the exact
// counts pin the fixed table.
static bool check_g_vs_I_at_device_size() {
  const runtime::Rect b = device_bounds();
  const runtime::CoverageMask g = mask_for_label_in("NavigationBar", b);
  const runtime::CoverageMask i = mask_for_label_in("NaviIationBar", b);
  const std::size_t inkG = ink_texels(g);
  const std::size_t inkI = ink_texels(i);
  if (inkG == 0 || inkI == 0) {
    std::printf("  [h14 g-vs-I] blank mask (inkG=%zu inkI=%zu)\n", inkG,
                inkI);
    return false;
  }
  const std::size_t holesG = count_enclosed_counters(g);
  const std::size_t holesI = count_enclosed_counters(i);
  if (holesG != 3 || holesI != 2) {
    std::printf(
        "  [h14 g-vs-I] holesG=%zu (want 3) holesI=%zu (want 2) inkG=%zu "
        "inkI=%zu\n",
        holesG, holesI, inkG, inkI);
    return false;
  }
  return holesG != holesI;
}

// T4a device-size leg (h=14): single-glyph enclosed-counter table. Pins
// the exact post-fix hole counts through the same public seam; every
// glyph must also leave ink so a blank mask can never satisfy a 0-hole
// expectation vacuously (ink guard applies to all rows, including the
// 0-hole ones).
static bool check_single_glyph_holes_at_device_size() {
  struct Expect {
    const char* label;
    std::size_t holes;
  };
  static const Expect kExpects[] = {
      {"B", 2}, {"M", 0}, {"P", 1}, {"R", 1}, {"a", 1}, {"e", 1},
      {"s", 1}, {"I", 0}, {"g", 1}, {"3", 0}, {"8", 2}, {"6", 1},
      {"9", 1}, {"0", 1}, {"o", 1}, {"G", 0}, {"2", 0}, {"5", 2},
  };
  const runtime::Rect b = device_bounds();
  bool ok = true;
  for (const Expect& e : kExpects) {
    const runtime::CoverageMask m = mask_for_label_in(e.label, b);
    const std::size_t ink = ink_texels(m);
    const std::size_t holes = count_enclosed_counters(m);
    if (ink == 0) {
      std::printf("  [h14 glyph '%s'] blank mask (want holes=%zu)\n",
                  e.label, e.holes);
      ok = false;
      continue;
    }
    if (holes != e.holes) {
      std::printf("  [h14 glyph '%s'] holes=%zu (want %zu) ink=%zu\n",
                  e.label, holes, e.holes, ink);
      ok = false;
    }
  }
  return ok;
}

// T4a iteration-3 leg (h=14): single `t`/`f` carry 0 enclosed counters
// with ink>0. Pre-fix the full-cell crossbar rendered as `+` (coder-cited;
// the TU never executes red in-tree for this leg). Post-fix the crossbar is
// a stem-crossing tick, so no counter may remain. Ink guard on both keeps
// the 0-hole expectation non-vacuous.
static bool check_t_f_ticks_at_device_size() {
  static const char* kLabels[] = {"t", "f"};
  const runtime::Rect b = device_bounds();
  bool ok = true;
  for (const char* label : kLabels) {
    const runtime::CoverageMask m = mask_for_label_in(label, b);
    const std::size_t ink = ink_texels(m);
    const std::size_t holes = count_enclosed_counters(m);
    if (ink == 0) {
      std::printf("  [h14 glyph '%s'] blank mask (want holes=0)\n", label);
      ok = false;
      continue;
    }
    if (holes != 0) {
      std::printf("  [h14 glyph '%s'] holes=%zu (want 0) ink=%zu\n", label,
                  holes, ink);
      ok = false;
    }
  }
  return ok;
}

// ---- Iteration-4 real-geometry legs (T4a) ----
//
// Coder diagnosis (authoritative): three geometry rounds went 10/10 hermetic
// while 6555 pixels stood byte-identical across verified-fresh installs +
// `pm clear`. Established facts driving these legs:
// (i) atlas `lookup` has ZERO production callers (write-only:
//     gallery_present.cpp:472 inserts, nobody reads) — the device path is
//     shape -> BuildGlyphOutline -> tile_raster -> BlendMask, the same
//     functions the TU covers;
// (ii) the TU diverges from the device in GEOMETRY and COMPOSITE only:
//     synthetic bounds (w=100, x=10, y=20; no rebaseDy
//     gallery_present.cpp:2842-2851, no wpx/clamp/avail from EmitCaptionCmd
//     :1959-1964, no kMaxMaskSide=256 clamp pressure renderer.cpp:652-657)
//     and a FakeWindowSwapchain that records cmds but never blends/scales
//     (real BlendMask jni_bridge.cpp:613-660 nearest-texel + sx_=w/720,
//     sy_=h/1600 mapping :494-500 untested).
//
// These legs close exactly that gap: real emitted caption bounds (the
// device's own EmitCaptionCmd sizing incl. clamp/avail, plus rebaseDy from
// a later page) through a blending fake that ACTUALLY composites with the
// sx_/sy_ mapping and stride semantics, mirroring BlendMask line-by-line.
// Expected RED pre-fix: the composed output shows the device-matching
// corruption (wrong-but-stable glyphs: holes tie between confusing pairs
// at real geometry, or assertable stride/scale artifacts). A CORRECT
// composed output is itself a finding (divergence lies elsewhere) — never
// force a failure.

// Blending fake that ACTUALLY composites. Mirrors
// WindowSwapchain::BlendMask (jni_bridge.cpp:613-660): nearest-texel
// proportional mapping of the mask onto the scaled destination rect,
// src-over blend, stride addressing; pixels outside the mask stay
// untouched (no whole-bounds fill). ThemeInk remap is NOT mirrored — both
// sides of every pair leg use the same caption ink, so it cancels.
class BlendingFakeSwapchain : public runtime::Swapchain {
 public:
  BlendingFakeSwapchain(std::uint32_t* bits, int stridePx, int width,
                        int height)
      : bits_(bits), stride_(stridePx), w_(width), h_(height) {
    sx_ = (w_ > 0) ? static_cast<float>(w_) / 720.0f : 1.0f;
    sy_ = (h_ > 0) ? static_cast<float>(h_) / 1600.0f : 1.0f;
  }
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
    if (bits_ == nullptr) return;
    if (cmd.kind != runtime::PrimitiveKind::ShapedText &&
        cmd.kind != runtime::PrimitiveKind::Text) {
      return;
    }
    if (cmd.payload.glyphRunId == 0) return;
    const muse::text::StatusOr<runtime::CoverageMask> coverage =
        runtime::RasterizeShapedText(cmd.payload.glyphRunId, cmd.bounds,
                                     cmd.payload.textColor);
    if (!coverage.ok()) return;
    BlendMask(*coverage, cmd.bounds, cmd.payload.textColor);
  }
  void BlendMask(const runtime::CoverageMask& mask, runtime::Rect bounds,
                 std::uint32_t src) {
    if (mask.width <= 0 || mask.height <= 0 || mask.alpha.empty()) return;
    int x0 = static_cast<int>(bounds.x * sx_);
    int y0 = static_cast<int>(bounds.y * sy_);
    int x1 = static_cast<int>((bounds.x + bounds.w) * sx_ + 0.5f);
    int y1 = static_cast<int>((bounds.y + bounds.h) * sy_ + 0.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w_) x1 = w_;
    if (y1 > h_) y1 = h_;
    if (x1 <= x0 || y1 <= y0) return;
    const unsigned sr = src & 0xFFu;
    const unsigned sg = (src >> 8) & 0xFFu;
    const unsigned sb = (src >> 16) & 0xFFu;
    for (int y = y0; y < y1; ++y) {
      const int my = static_cast<int>(
          (static_cast<double>(y - y0) * mask.height) / (y1 - y0));
      for (int x = x0; x < x1; ++x) {
        const int mx = static_cast<int>(
            (static_cast<double>(x - x0) * mask.width) / (x1 - x0));
        const std::size_t t = static_cast<std::size_t>(my) *
                                  static_cast<std::size_t>(mask.width) +
                              static_cast<std::size_t>(mx);
        if (t >= mask.alpha.size()) continue;
        const float a = mask.alpha[t];
        if (a <= 0.0f) continue;
        const std::uint32_t dst =
            bits_[static_cast<std::size_t>(y) *
                      static_cast<std::size_t>(stride_) +
                  static_cast<std::size_t>(x)];
        const unsigned dr = dst & 0xFFu;
        const unsigned dg = (dst >> 8) & 0xFFu;
        const unsigned db = (dst >> 16) & 0xFFu;
        const unsigned r =
            static_cast<unsigned>(sr * a + dr * (1.0f - a) + 0.5f);
        const unsigned g =
            static_cast<unsigned>(sg * a + dg * (1.0f - a) + 0.5f);
        const unsigned b =
            static_cast<unsigned>(sb * a + db * (1.0f - a) + 0.5f);
        bits_[static_cast<std::size_t>(y) *
                  static_cast<std::size_t>(stride_) +
              static_cast<std::size_t>(x)] =
            static_cast<std::uint32_t>(r > 255u ? 255u : r) |
            (static_cast<std::uint32_t>(g > 255u ? 255u : g) << 8) |
            (static_cast<std::uint32_t>(b > 255u ? 255u : b) << 16) |
            (static_cast<std::uint32_t>(0xFF) << 24);
      }
    }
  }
  std::vector<runtime::DrawCmd> captured;

 private:
  std::uint32_t* bits_;
  int stride_;
  int w_;
  int h_;
  float sx_;
  float sy_;
};

// One real caption as the device emits it: the ShapedText run id plus the
// exact EmitCaptionCmd bounds (wpx/clamp/avail) and caption ink.
struct RealCaption {
  bool found = false;
  std::uint32_t run = 0;
  runtime::Rect bounds{};
  std::uint32_t ink = 0;
};

// Page-0 caption for a single text row: real wpx/clamp/avail bounds,
// rebaseDy == 0 on page 0.
RealCaption real_caption_page0(const std::string& label) {
  RealCaption out;
  runtime::GalleryRow row;
  row.label = label;
  const std::vector<runtime::GalleryRow> rows{row};
  runtime::Renderer renderer(baseline_device());
  BlendingFakeSwapchain window(nullptr, 0, 0, 0);
  std::string error;
  if (!runtime::PresentDeviceGalleryPage(rows, 0, renderer, window,
                                         &error)) {
    return out;
  }
  for (const auto& cmd : window.captured) {
    if ((cmd.kind == runtime::PrimitiveKind::ShapedText ||
         cmd.kind == runtime::PrimitiveKind::Text) &&
        cmd.payload.glyphRunId != 0) {
      out.found = true;
      out.run = cmd.payload.glyphRunId;
      out.bounds = cmd.bounds;
      out.ink = cmd.payload.textColor;
      return out;
    }
  }
  return out;
}

// Caption for the same label as emitted on a later page: identical run id,
// bounds shifted by the device's own rebaseDy
// (gallery_present.cpp:2842-2851). Only accepts a page whose bounds really
// moved, so the leg observes rebaseDy by construction.
RealCaption real_caption_rebased(const std::string& label) {
  const RealCaption page0 = real_caption_page0(label);
  if (!page0.found) return RealCaption{};
  std::vector<runtime::GalleryRow> rows;
  for (int i = 0; i < 40; ++i) {
    runtime::GalleryRow r;
    r.label = "filler-row-" + std::to_string(i);
    rows.push_back(r);
  }
  runtime::GalleryRow target;
  target.label = label;
  rows.push_back(target);
  const int pages = runtime::GalleryPageCount(rows, 1600);
  // Start at page 1: every page >= 1 rebases by rebaseDy = cursorFirst - kPad
  // != 0 (firstVisible > 0), so any caption found here passed through the
  // rebase path by construction. (Post-rebase bounds numerically coincide
  // with page-0 bounds for the leading row — rebase maps the slice to the
  // viewport origin — so a y-comparison cannot observe it; the page index
  // is the observation.)
  for (int p = 1; p < pages; ++p) {
    runtime::Renderer renderer(baseline_device());
    BlendingFakeSwapchain window(nullptr, 0, 0, 0);
    std::string error;
    if (!runtime::PresentDeviceGalleryPage(rows, p, renderer, window,
                                           &error)) {
      continue;
    }
    for (const auto& cmd : window.captured) {
      if ((cmd.kind == runtime::PrimitiveKind::ShapedText ||
           cmd.kind == runtime::PrimitiveKind::Text) &&
          cmd.payload.glyphRunId == page0.run) {
        RealCaption out;
        out.found = true;
        out.run = cmd.payload.glyphRunId;
        out.bounds = cmd.bounds;
        out.ink = cmd.payload.textColor;
        return out;
      }
    }
  }
  return RealCaption{};
}

// One caption composited through rasterize + BlendMask mirror at window
// WxH with stride != width (stride semantics under test).
struct Composed {
  bool ok = false;
  std::vector<std::uint32_t> px;
  int w = 0;
  int h = 0;
  int stride = 0;
  runtime::Rect dst{};
  std::uint32_t clear = 0xFF000000u;
};

Composed composite_caption(const RealCaption& cap, int W, int H) {
  Composed c;
  if (!cap.found || W <= 0 || H <= 0) return c;
  c.w = W;
  c.h = H;
  c.stride = W + 7;  // stride != width by construction
  c.px.assign(static_cast<std::size_t>(c.h) *
                  static_cast<std::size_t>(c.stride),
              c.clear);
  BlendingFakeSwapchain win(c.px.data(), c.stride, W, H);
  runtime::DrawCmd cmd;
  cmd.kind = runtime::PrimitiveKind::ShapedText;
  cmd.bounds = cap.bounds;
  cmd.payload.glyphRunId = cap.run;
  cmd.payload.textColor = cap.ink;
  win.submit(cmd);
  if (win.captured.empty()) return c;
  c.dst = cap.bounds;
  c.ok = true;
  return c;
}

// Binary ink region of the composed dst rect (pixel != clear -> ink 1.0),
// using the same sx_/sy_ rect mapping as BlendMask, for hole counting.
runtime::CoverageMask composed_region_mask(const Composed& c) {
  runtime::CoverageMask m;
  const float sx = (c.w > 0) ? static_cast<float>(c.w) / 720.0f : 1.0f;
  const float sy = (c.h > 0) ? static_cast<float>(c.h) / 1600.0f : 1.0f;
  int x0 = static_cast<int>(c.dst.x * sx);
  int y0 = static_cast<int>(c.dst.y * sy);
  int x1 = static_cast<int>((c.dst.x + c.dst.w) * sx + 0.5f);
  int y1 = static_cast<int>((c.dst.y + c.dst.h) * sy + 0.5f);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > c.w) x1 = c.w;
  if (y1 > c.h) y1 = c.h;
  m.width = x1 - x0;
  m.height = y1 - y0;
  if (m.width <= 0 || m.height <= 0) return m;
  m.alpha.reserve(static_cast<std::size_t>(m.width) * m.height);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const std::uint32_t p =
          c.px[static_cast<std::size_t>(y) *
                   static_cast<std::size_t>(c.stride) +
               static_cast<std::size_t>(x)];
      m.alpha.push_back(p != c.clear ? 1.0f : 0.0f);
    }
  }
  return m;
}

// Stride guard: pad words [w, stride) in every row must stay clear (no row
// bleed from stride confusion).
bool stride_pads_clean(const Composed& c) {
  for (int y = 0; y < c.h; ++y) {
    for (int x = c.w; x < c.stride; ++x) {
      if (c.px[static_cast<std::size_t>(y) *
                   static_cast<std::size_t>(c.stride) +
               static_cast<std::size_t>(x)] != c.clear) {
        return false;
      }
    }
  }
  return true;
}

// Dead-lookup pin: a fresh T3 atlas misses (lookup is consulted nowhere on
// the device gallery path — production grep pins zero lookup callers; the
// device only inserts at gallery_present.cpp:472), the TU atlas itself
// insert-then-hits (non-vacuous), and the emitted run resolves through the
// shaping-cache seam with no atlas involvement.
static bool check_atlas_lookup_dead_pin() {
  muse::text::GlyphAtlas atlas;
  if (atlas.lookup(muse::text::FontHandle(1), 2.0f, 42u).has_value()) {
    std::printf("  [atlas-dead] fresh lookup hit (want miss)\n");
    return false;
  }
  muse::text::TexelRecord texels;
  texels.glyph_id = 42u;
  texels.advance_px = 8.0f;
  if (atlas.insert(muse::text::FontHandle(1), 2.0f, 42u, texels) !=
      muse::text::Status::kOk) {
    std::printf("  [atlas-dead] insert failed\n");
    return false;
  }
  if (!atlas.lookup(muse::text::FontHandle(1), 2.0f, 42u).has_value()) {
    std::printf("  [atlas-dead] insert-then-lookup missed (TU atlas broken)\n");
    return false;
  }
  const RealCaption cap = real_caption_page0("m3-gallery");
  if (!cap.found || cap.run == 0) {
    std::printf("  [atlas-dead] no emitted run for label\n");
    return false;
  }
  std::vector<std::uint32_t> glyphs;
  std::vector<float> advances;
  if (!runtime::ResolveGalleryShapedRun(cap.run, &glyphs, &advances) ||
      glyphs.empty()) {
    std::printf("  [atlas-dead] emitted run does not resolve\n");
    return false;
  }
  return true;
}

// Real-bounds pin: captured caption bounds carry the device sizing — h=14
// (EmitCaptionCmd), x=bx+16=32, 1<=w<=avail=656 (bw=688-32) — for both
// pair labels. Prints the exact bounds for the red table.
static bool check_real_caption_bounds_device_sized() {
  static const char* kLabels[] = {"m3-gallery", "m8-gallery",
                                  "NavigationBar", "NaviIationBar"};
  bool ok = true;
  for (const char* label : kLabels) {
    const RealCaption cap = real_caption_page0(label);
    if (!cap.found) {
      std::printf("  [real-bounds] '%s': no caption emitted\n", label);
      ok = false;
      continue;
    }
    std::printf("  [real-bounds] '%s': run=0x%x x=%.1f y=%.1f w=%.1f h=%.1f\n",
                label, cap.run, cap.bounds.x, cap.bounds.y, cap.bounds.w,
                cap.bounds.h);
    if (cap.bounds.h != 14.0f || cap.bounds.x != 32.0f ||
        cap.bounds.w < 1.0f || cap.bounds.w > 656.0f) {
      std::printf("  [real-bounds] '%s': NOT device-sized\n", label);
      ok = false;
    }
  }
  return ok;
}

// Pair composite through real (rebased) bounds + blending fake at window
// WxH: both sides inked (non-vacuous), stride pads clean, deterministic
// (wrong-but-stable would also be stable — stability alone is not the pin),
// and the composed hole topologies differ. A tie is the RED pin.
static bool check_pair_composite(const char* a, const char* b,
                                 const char* tag, int W, int H) {
  const RealCaption ca = real_caption_rebased(a);
  const RealCaption cb = real_caption_rebased(b);
  if (!ca.found || !cb.found) {
    std::printf("  [%s %dx%d] rebased caption missing (a=%d b=%d)\n", tag, W,
                H, (int)ca.found, (int)cb.found);
    return false;
  }
  const Composed A = composite_caption(ca, W, H);
  const Composed A2 = composite_caption(ca, W, H);
  const Composed B = composite_caption(cb, W, H);
  if (!A.ok || !B.ok) {
    std::printf("  [%s %dx%d] composite failed\n", tag, W, H);
    return false;
  }
  if (A.px != A2.px) {
    std::printf("  [%s %dx%d] composite nondeterministic\n", tag, W, H);
    return false;
  }
  if (!stride_pads_clean(A) || !stride_pads_clean(B)) {
    std::printf("  [%s %dx%d] stride pad dirtied (stride artifact)\n", tag,
                W, H);
    return false;
  }
  const runtime::CoverageMask ma = composed_region_mask(A);
  const runtime::CoverageMask mb = composed_region_mask(B);
  const std::size_t inkA = ink_texels(ma);
  const std::size_t inkB = ink_texels(mb);
  if (inkA == 0 || inkB == 0) {
    std::printf("  [%s %dx%d] blank composed region (inkA=%zu inkB=%zu)\n",
                tag, W, H, inkA, inkB);
    return false;
  }
  const std::size_t holesA = count_enclosed_counters(ma);
  const std::size_t holesB = count_enclosed_counters(mb);
  std::printf("  [%s %dx%d] holesA=%zu holesB=%zu inkA=%zu inkB=%zu "
              "yA=%.1f yB=%.1f wA=%.1f wB=%.1f\n",
              tag, W, H, holesA, holesB, inkA, inkB, ca.bounds.y,
              cb.bounds.y, ca.bounds.w, cb.bounds.w);
  if (holesA == holesB) {
    std::printf("  [%s %dx%d] COMPOSED TIE (device-matching corruption)\n",
                tag, W, H);
    return false;
  }
  return true;
}

// 3-vs-8 through the full real-geometry composite: rebased EmitCaptionCmd
// bounds -> RasterizeShapedText -> BlendMask mirror, at 1:1 (720x1600) and
// at 0.5x downscale (360x800, presentation-downscale pressure).
static bool check_rebased_composite_3_vs_8() {
  return check_pair_composite("m3-gallery", "m8-gallery", "comp-3v8",
                              720, 1600) &&
         check_pair_composite("m3-gallery", "m8-gallery", "comp-3v8",
                              360, 800);
}

// g-vs-bare-stem through the same composite, both scales.
static bool check_rebased_composite_g_vs_I() {
  return check_pair_composite("NavigationBar", "NaviIationBar", "comp-gvI",
                              720, 1600) &&
         check_pair_composite("NavigationBar", "NaviIationBar", "comp-gvI",
                              360, 800);
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
  RUN_CASE(check_known_run_rasterizes_coverage);
  RUN_CASE(check_unknown_run_returns_unknown_primitive);
  RUN_CASE(check_zero_run_is_dangling);
  RUN_CASE(check_3_mask_differs_from_8_mask);
  RUN_CASE(check_g_mask_differs_from_stem_I_mask);
  RUN_CASE(check_empty_label_has_no_counters);
  RUN_CASE(check_3_vs_8_at_device_size);
  RUN_CASE(check_g_vs_I_at_device_size);
  RUN_CASE(check_single_glyph_holes_at_device_size);
  RUN_CASE(check_t_f_ticks_at_device_size);
  RUN_CASE(check_atlas_lookup_dead_pin);
  RUN_CASE(check_real_caption_bounds_device_sized);
  RUN_CASE(check_rebased_composite_3_vs_8);
  RUN_CASE(check_rebased_composite_g_vs_I);
  if (failures > 0) {
    std::printf("gallery_text_raster_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_text_raster_test: all passing\n");
  return 0;
}
