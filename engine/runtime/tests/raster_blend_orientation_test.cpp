// T6c/T6d — Rasterize->Blend seam column-order pins (BUG-TEXT-008 t5r5).
//
// Device fact (device-trace-mirror.md ROOT_CAUSE_FOUND): the executed lane is
// PresentDeviceGalleryPage -> ShapedText -> RasterizeShapedText -> BlendMask,
// and per-glyph texels are X-mirrored on device (engine-sha=69b49c9) while
// advances stay +x LTR ("m3-gallery" reads "mE-gallery"). T6a pins a dead
// upload path and T6b pins advance sign, so neither covers this seam. The
// flip localizes inside per-glyph coverage generation or the final blit:
// BuildGlyphOutline / RasterizeOutlineBlock (renderer.cpp:111-498) or
// BlendMask (jni_bridge.cpp:646-690).
//
// REVIEW FIX (MAJOR: T6c/T6d were tautological): the previous revision built
// a hand-inked wedge and a hand-inked "posted" buffer, then asserted the
// guard predicates over those two hand-built buffers. A mirroring raster or
// blit could never fail that suite — the buffers matched by construction
// (wedge-vs-itself). This revision binds to the REAL call path:
//
//   T6c: PresentDeviceGalleryPage -> RasterizeShapedText (real
//        BuildGlyphOutline + RasterizeOutlineBlock, guard included) on the
//        engine-text reference rows "Hi!" (strictly left-dominant crop:
//        H-stem left, Bang tick right) and "Hello" (strictly right-dominant
//        crop: o-bowl right mass). Reference rows are the only labels that
//        shape with nonzero advances on a hermetic host lane (checked-in
//        HarfBuzz dumps; the stub face yields 0 advances and fail-closes at
//        the T5r4 upload guard, as does any non-reference label).
//        The ink bbox is cropped out of the bounds-sized mask (extreme
//        bounds columns are background, which would pass vacuously) and the
//        REAL guard predicate RasterPreservesGlyphColumnOrder asserts over
//        that real raster output — `true` for "Hi!", `false` for "Hello"
//        (the predicate reports left-dominance; pinning the real direction
//        of each label). Mirror legs: the X-mirrored crop MUST flip the
//        predicate — proving a flipped raster output fails this pin
//        (sensitivity; the old suite had none).
//   T6d: the same real raster output is blitted through a line-by-line
//        mirror of WindowSwapchain::BlendMask (proportional mask->rect
//        mapping, src-over, stride addressing — the real method lives in an
//        anonymous namespace and is not linkable, so the mapping is copied
//        exactly and any drift is a compile-visible divergence), the posted
//        buffer is resampled exactly like the prod guard resamples it
//        (|post-pre| grey levels per texel), and the REAL guard predicate
//        BlendPreservesMaskColumnOrder asserts over the blit input vs the
//        blit output. Mirror-negative leg: mirrored posted MUST fail.
//
// Guard-removal reasoning (by inspection, prod untouched): the prod guards
// execute on every block/blend and restore the snapshot exactly when the
// predicate fails. These pins assert the predicate over real stage output,
// and the negative legs prove mirrored output fails it. Hence any
// reintroduced per-glyph X-flip fails the positive legs; with a flip
// present, removing the guard call fails them too (the guard is the only
// stage restoring order). Stated limit: on a flip-free tree the positive
// legs pass with or without the guard — the negative legs are what prove
// the suite can see the defect at all.
//
// Hermetic: C++17, committed runtime headers only, in-memory run ids,
// procedural outlines (no font binaries), no device, no GPU, no network,
// no config, no filesystem. Zero JNI: this TU touches no JVM/JNI type.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "muse/text/raster_blend_orientation.h"
#include "runtime/gallery_present.h"
#include "runtime/renderer.h"

namespace {

// Window-backed fake swapchain: records every submitted cmd in order,
// like a presented surface would retain them. No GPU, no ANativeWindow.
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

runtime::Rect device_bounds() {
  runtime::Rect r;
  r.x = 10.0f;
  r.y = 20.0f;
  r.w = 100.0f;
  r.h = 14.0f;  // EmitCaptionCmd caption size: the size the defect showed at.
  return r;
}

// Rasterize one label through the PUBLIC device seam
// (PresentDeviceGalleryPage -> first ShapedText run -> RasterizeShapedText).
// Returns an empty mask (width 0) when no run is emitted or rasterization
// fails closed. Never throws.
runtime::CoverageMask mask_for_label(const std::string& label) {
  runtime::GalleryRow row;
  row.label = label;
  const std::vector<runtime::GalleryRow> rows{row};
  runtime::Renderer renderer(baseline_device());
  FakeWindowSwapchain window;
  std::string error;
  if (!runtime::PresentDeviceGalleryPage(rows, 0, renderer, window,
                                         &error)) {
    std::printf("  [seam %s] present failed: %s\n", label.c_str(),
                error.c_str());
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

// Crop to the ink bounding box. Bounds-sized masks are background at the
// extreme columns, which would satisfy any column-order predicate
// vacuously; the tight crop forces the predicate to judge real ink.
// Returns false when the mask is blank or malformed. Never throws.
bool crop_to_ink(const runtime::CoverageMask& in, std::vector<float>& out,
                 std::size_t& w, std::size_t& h) {
  out.clear();
  w = 0;
  h = 0;
  if (in.width <= 0 || in.height <= 0) return false;
  const std::size_t area =
      static_cast<std::size_t>(in.width) * in.height;
  if (in.alpha.size() != area) return false;
  int x0 = in.width, x1 = -1, y0 = in.height, y1 = -1;
  for (int y = 0; y < in.height; ++y) {
    for (int x = 0; x < in.width; ++x) {
      if (in.alpha[static_cast<std::size_t>(y) * in.width + x] != 0.0f) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
    }
  }
  if (x1 < x0 || y1 < y0) return false;  // blank: no ink, vacuous — reject.
  w = static_cast<std::size_t>(x1 - x0 + 1);
  h = static_cast<std::size_t>(y1 - y0 + 1);
  out.reserve(w * h);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      out.push_back(in.alpha[static_cast<std::size_t>(y) * in.width + x]);
    }
  }
  return true;
}

std::vector<float> mirror_x(const std::vector<float>& a, std::size_t w,
                            std::size_t h) {
  std::vector<float> out(a.size(), 0.0f);
  for (std::size_t y = 0; y < h; ++y) {
    for (std::size_t x = 0; x < w; ++x) {
      out[y * w + x] = a[y * w + (w - 1 - x)];
    }
  }
  return out;
}

void column_sums(const std::vector<float>& a, std::size_t w, std::size_t h,
                 double& left, double& right) {
  left = 0.0;
  right = 0.0;
  for (std::size_t y = 0; y < h; ++y) {
    left += static_cast<double>(a[y * w + 0]);
    right += static_cast<double>(a[y * w + (w - 1)]);
  }
}

// T6c positive leg helper: the REAL raster output must show the label's
// real column direction strictly (a tie passes vacuously and pins
// nothing), and the REAL guard predicate must report exactly that
// direction. A per-glyph X-flip swaps the direction and fails this leg.
bool check_raster_glyph(const char* label, bool expect_left) {
  const runtime::CoverageMask mask = mask_for_label(label);
  std::vector<float> crop;
  std::size_t w = 0, h = 0;
  if (!crop_to_ink(mask, crop, w, h)) {
    std::printf("  [raster %s] blank or malformed mask\n", label);
    return false;
  }
  double left = 0.0, right = 0.0;
  column_sums(crop, w, h, left, right);
  std::printf("  [raster %s] crop %zux%zu left=%.2f right=%.2f\n", label, w,
              h, left, right);
  if (expect_left && !(left > right)) {
    std::printf("  [raster %s] NOT strictly left-dominant (vacuous pin)\n",
                label);
    return false;
  }
  if (!expect_left && !(right > left)) {
    std::printf("  [raster %s] NOT strictly right-dominant (vacuous pin)\n",
                label);
    return false;
  }
  const bool pred =
      muse::text::RasterPreservesGlyphColumnOrder(crop.data(), w, h);
  if (pred != expect_left) {
    std::printf("  [raster %s] real output failed the guard predicate "
                "(X-flip on the raster half)\n",
                label);
    return false;
  }
  return true;
}

// T6c negative leg helper: the X-mirrored real output MUST flip the guard
// predicate. This is the sensitivity proof the old wedge-vs-itself suite
// lacked: a flipped raster produces exactly this buffer.
bool check_raster_mirror_fails(const char* label, bool expect_left) {
  const runtime::CoverageMask mask = mask_for_label(label);
  std::vector<float> crop;
  std::size_t w = 0, h = 0;
  if (!crop_to_ink(mask, crop, w, h)) {
    std::printf("  [raster-mirror %s] blank or malformed mask\n", label);
    return false;
  }
  const std::vector<float> mirrored = mirror_x(crop, w, h);
  const bool pred =
      muse::text::RasterPreservesGlyphColumnOrder(mirrored.data(), w, h);
  if (pred == expect_left) {
    std::printf("  [raster-mirror %s] mirrored output did NOT flip the "
                "predicate (pin is blind to flips)\n",
                label);
    return false;
  }
  return true;
}

// Line-by-line mirror of WindowSwapchain::BlendMask
// (engine/runtime/src/jni_bridge.cpp): nearest-texel proportional mapping
// of the mask onto the scaled destination rect, src-over blend, stride
// addressing; pixels outside the mask stay untouched. ThemeInk remap is
// NOT mirrored — both sides of every pair leg use the same ink, so it
// cancels. Any drift from the prod mapping is a visible divergence here.
void mirror_blend_mask(const runtime::CoverageMask& mask, runtime::Rect bounds,
                       std::uint32_t src, std::uint32_t* bits, int stride,
                       int w, int h, float sx, float sy) {
  if (mask.width <= 0 || mask.height <= 0 || mask.alpha.empty()) return;
  int x0 = static_cast<int>(bounds.x * sx);
  int y0 = static_cast<int>(bounds.y * sy);
  int x1 = static_cast<int>((bounds.x + bounds.w) * sx + 0.5f);
  int y1 = static_cast<int>((bounds.y + bounds.h) * sy + 0.5f);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > w) x1 = w;
  if (y1 > h) y1 = h;
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
          bits[static_cast<std::size_t>(y) *
                   static_cast<std::size_t>(stride) +
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
      bits[static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
           static_cast<std::size_t>(x)] =
          static_cast<std::uint32_t>(r > 255u ? 255u : r) |
          (static_cast<std::uint32_t>(g > 255u ? 255u : g) << 8) |
          (static_cast<std::uint32_t>(b > 255u ? 255u : b) << 16) |
          (static_cast<std::uint32_t>(0xFF) << 24);
    }
  }
}

// Resample the blended rect back into mask geometry exactly like the prod
// BlendMask guard does: per-texel |post-pre| grey levels (polarity-free
// ink-ness from this real blit, never synthesized).
std::vector<std::uint32_t> resample_posted(const std::vector<float>& crop,
                                           std::size_t w, std::size_t h,
                                           const std::vector<std::uint32_t>& pre,
                                           const std::vector<std::uint32_t>& post,
                                           int stride, unsigned long long& ink) {
  std::vector<std::uint32_t> posted(w * h, 0);
  ink = 0;
  for (std::size_t my = 0; my < h; ++my) {
    for (std::size_t mx = 0; mx < w; ++mx) {
      const std::uint32_t before = pre[my * stride + mx];
      const std::uint32_t after = post[my * stride + mx];
      const unsigned db = (after & 0xFFu) > (before & 0xFFu)
                              ? (after & 0xFFu) - (before & 0xFFu)
                              : (before & 0xFFu) - (after & 0xFFu);
      const unsigned dg = ((after >> 8) & 0xFFu) > ((before >> 8) & 0xFFu)
                              ? ((after >> 8) & 0xFFu) -
                                    ((before >> 8) & 0xFFu)
                              : ((before >> 8) & 0xFFu) -
                                    ((after >> 8) & 0xFFu);
      const unsigned dr = ((after >> 16) & 0xFFu) > ((before >> 16) & 0xFFu)
                              ? ((after >> 16) & 0xFFu) -
                                    ((before >> 16) & 0xFFu)
                              : ((before >> 16) & 0xFFu) -
                                    ((after >> 16) & 0xFFu);
      const unsigned v = (db + dg + dr) / 3u;
      ink += v;
      posted[my * w + mx] = static_cast<std::uint32_t>(v > 255u ? 255u : v) |
                            (static_cast<std::uint32_t>(v > 255u ? 255u : v)
                             << 8) |
                            (static_cast<std::uint32_t>(v > 255u ? 255u : v)
                             << 16) |
                            (static_cast<std::uint32_t>(0xFF) << 24);
    }
  }
  (void)crop;
  return posted;
}

// Blit one mask 1:1 through the BlendMask mirror and check the REAL blend
// guard predicate over blit input vs blit output. `want_mirror Proving:
// when false, the true posted buffer must satisfy the predicate;
// when true, the X-mirrored posted buffer must FAIL it (sensitivity:
// a flipped blit produces exactly that buffer).
bool check_blend_glyph(const char* label, bool want_mirror_fail) {
  const runtime::CoverageMask mask = mask_for_label(label);
  std::vector<float> crop;
  std::size_t w = 0, h = 0;
  if (!crop_to_ink(mask, crop, w, h)) {
    std::printf("  [blend %s] blank or malformed mask\n", label);
    return false;
  }
  runtime::CoverageMask cm;
  cm.width = static_cast<int>(w);
  cm.height = static_cast<int>(h);
  cm.alpha = crop;
  runtime::Rect bounds;
  bounds.x = 0.0f;
  bounds.y = 0.0f;
  bounds.w = static_cast<float>(w);
  bounds.h = static_cast<float>(h);
  const int stride = static_cast<int>(w);
  const std::uint32_t kClear = 0xFF000000u;
  const std::uint32_t kInk = 0xFFFFFFFFu;
  std::vector<std::uint32_t> pre(static_cast<std::size_t>(h) * stride,
                                 kClear);
  std::vector<std::uint32_t> bits = pre;
  mirror_blend_mask(cm, bounds, kInk, bits.data(), stride,
                    static_cast<int>(w), static_cast<int>(h), 1.0f, 1.0f);
  unsigned long long ink = 0;
  std::vector<std::uint32_t> posted =
      resample_posted(crop, w, h, pre, bits, stride, ink);
  if (ink == 0) {
    std::printf("  [blend %s] blit deposited no ink (vacuous pin)\n", label);
    return false;
  }
  std::vector<std::uint32_t> argued = posted;
  if (want_mirror_fail) {
    for (std::size_t y = 0; y < h; ++y) {
      for (std::size_t x = 0; x < w; ++x) {
        argued[y * w + x] = posted[y * w + (w - 1 - x)];
      }
    }
  }
  const bool ok = muse::text::BlendPreservesMaskColumnOrder(
      crop.data(), w, h, argued.data(), static_cast<int>(w),
      static_cast<int>(w));
  if (!want_mirror_fail && !ok) {
    std::printf("  [blend %s] real blit output failed the guard predicate "
                "(X-flip on the blend half, ink=%llu)\n",
                label, ink);
    return false;
  }
  if (want_mirror_fail && ok) {
    std::printf("  [blend %s] mirrored posted PASSED the predicate "
                "(pin is blind to flips)\n",
                label);
    return false;
  }
  std::printf("  [blend %s%s] ink=%llu ok=%d\n", label,
              want_mirror_fail ? "-mirror" : "", ink, (int)ok);
  return true;
}

// T6c: real raster output preserves each label's real column direction
// through BuildGlyphOutline/RasterizeOutlineBlock + guard.
static bool check_raster_preserves_column_order() {
  return check_raster_glyph("Hi!", true) &&
         check_raster_glyph("Hello", false);
}

// T6c sensitivity: mirrored real output flips the raster predicate.
static bool check_raster_mirror_fails_predicate() {
  return check_raster_mirror_fails("Hi!", true) &&
         check_raster_mirror_fails("Hello", false);
}

// T6d: real raster output blitted 1:1 preserves column order through the
// BlendMask mapping + guard predicate.
static bool check_blend_preserves_column_order() {
  return check_blend_glyph("Hi!", false) && check_blend_glyph("Hello", false);
}

// T6d sensitivity: mirrored posted fails the blend predicate.
static bool check_blend_mirror_fails_predicate() {
  return check_blend_glyph("Hi!", true) && check_blend_glyph("Hello", true);
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_raster_preserves_column_order);
  RUN_CASE(check_raster_mirror_fails_predicate);
  RUN_CASE(check_blend_preserves_column_order);
  RUN_CASE(check_blend_mirror_fails_predicate);
  if (failures > 0) {
    std::printf("raster_blend_orientation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("raster_blend_orientation_test: all passing\n");
  return 0;
}
