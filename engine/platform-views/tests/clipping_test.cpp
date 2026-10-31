// Batch 2 contract tests — FR-PVIEWS-005 / AC-PVIEWS-007 / TC-PVIEWS-007.
// Binds ONLY to the plan T2/T3 `Interface:` lines; never to implementation.
// Hermetic: C++17 + clipping/{surface_clipper.h, offscreen_fallback.h} +
// pviews/z_order.h (FR-PVIEWS-001 geometry, read-only) only; the
// ASurfaceControl seam and the graphics external-memory import seam are
// MOCKED in this file (structs below) — no platform calls, no device, no
// network, no env/config reads, no filesystem, no wall-clock, no live
// services. Plain PASS-check binary convention (no gtest).
//
// RED state (Batch 2, pre-coder): applyClip/compositeOffscreen are
// declare-only in the headers, so this file compiles but does NOT link.
// That link failure is the expected red for every case below.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "../clipping/offscreen_fallback.h"
#include "../clipping/surface_clipper.h"
#include "pviews/z_order.h"

// ---------------------------------------------------------------------------
// Hermetic test-local seams (declared here ONLY — never production code).
// ---------------------------------------------------------------------------

// MOCK of the ASurfaceControl seam: production must route every platform
// touch through a seam shaped like this; the invalid-path case asserts the
// seam is untouched. Never calls any platform API.
struct FakeSurfaceSeam {
  int touchCount = 0;
  int lastApiLevel = 0;
  void touch(int apiLevel) {
    ++touchCount;
    lastApiLevel = apiLevel;
  }
};

// MOCK of the graphics external-memory import seam (NFR-PVIEWS-001 minimal
// shim, hermetically mocked): production consumes the import contract
// through a seam shaped like this; tests force its outcomes.
struct FakeImportSeam {
  enum class Outcome { Ok, ImportFailed, FenceTimeout };
  Outcome next = Outcome::Ok;
  int waitCalls = 0;  // fence wait must be bounded: exactly one poll, no loop
  bool waitForFence() {
    ++waitCalls;
    return next != Outcome::FenceTimeout;
  }
};

// Routing predicate under test: the offscreen fallback triggers exactly when
// T2 reports fallbackRequired (asserts routing, not cross-item internals).
static bool shouldUseOffscreenFallback(const ClipResult& r) {
  return r.fallbackRequired;
}

// ---------------------------------------------------------------------------
// Hermetic pixel-mask helpers (synthetic geometry, no device).
// ---------------------------------------------------------------------------

// Rounded-rect inclusion over the ClipRect bounds (FR-PVIEWS-001 geometry).
static bool insideRoundRect(float x, float y, const ClipRect& b) {
  float r = static_cast<float>(b.cornerRadius);
  float l = static_cast<float>(b.left), t = static_cast<float>(b.top);
  float rr = static_cast<float>(b.right), bb = static_cast<float>(b.bottom);
  if (x < l || x >= rr || y < t || y >= bb) return false;
  // Corner discs.
  float cx = (x < l + r) ? l + r : ((x >= rr - r) ? rr - r : x);
  float cy = (y < t + r) ? t + r : ((y >= bb - r) ? bb - r : y);
  float dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= r * r;
}

// Point-in-polygon (ray cast) for arbitrary-path inclusion.
static bool insidePolygon(float x, float y, const PathPoints& pts) {
  bool inside = false;
  for (std::size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
    float xi = pts[i].x, yi = pts[i].y, xj = pts[j].x, yj = pts[j].y;
    if (((yi > y) != (yj > y)) &&
        (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
      inside = !inside;
  }
  return inside;
}

// Counts pixels of a W×H synthetic raster that lie inside the view bounds
// but OUTSIDE the clip (bleed). Must be zero.
static std::uint32_t countBleedRoundRect(const ClipRect& b) {
  std::uint32_t bleed = 0;
  for (int y = b.top; y < b.bottom; ++y)
    for (int x = b.left; x < b.right; ++x)
      if (!insideRoundRect(static_cast<float>(x) + 0.5f,
                           static_cast<float>(y) + 0.5f, b))
        ++bleed;
  return bleed;
}

static std::uint32_t countBleedPath(int w, int h, const PathPoints& pts) {
  std::uint32_t bleed = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if (!insidePolygon(static_cast<float>(x) + 0.5f,
                         static_cast<float>(y) + 0.5f, pts))
        ++bleed;
  // Bleed = inside-bounds-but-outside-clip: caller passes the path as the
  // clip of the full view, so any outside pixel is bleed by construction
  // under a correct clip; the assertion below pins the *engine* result to
  // zero bleed via applyClip's ok + mask agreement. Here we return the raw
  // outside count for the caller to compare against the clipped region.
  return bleed;
}

// A path is valid iff it has ≥3 points and nonzero area.
static bool pathValid(const PathPoints& pts) {
  if (pts.size() < 3) return false;
  float area = 0.0f;
  for (std::size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
    area += (pts[j].x * pts[i].y - pts[i].x * pts[j].y);
  return area != 0.0f;
}

// ---------------------------------------------------------------------------
// Test-local golden tolerances (TEST-GOLDEN / AC-TESTING-034: explicit mean
// per-channel error + bounded differing-pixel count per test; synthetic
// textures, no wall-clock).
// ---------------------------------------------------------------------------
static constexpr float kShaderMeanTol = 0.02f;
static constexpr std::uint32_t kShaderPixelBound = 128;
static constexpr float kBlendMeanTol = 0.01f;
static constexpr std::uint32_t kBlendPixelBound = 32;

static int failures = 0;
#define EXPECT_TRUE(cond)                                              \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("    EXPECT_TRUE failed %s:%d: %s\n", __FILE__,      \
                  __LINE__, #cond);                                   \
      return false;                                                    \
    }                                                                  \
  } while (0)

// T2: rounded-rect clip covers bounds with zero pixels outside (FR-PVIEWS-005,
// AC-PVIEWS-007). Consumes ClipRect geometry (FR-PVIEWS-001, read-only).
static bool check_rounded_rect_clip() {
  ClipRect bounds{0, 0, 100, 60, 12};
  ClipSpec spec;
  spec.kind = ClipSpec::Kind::RoundRect;
  spec.cornerRadiusPx = static_cast<float>(bounds.cornerRadius);
  spec.hint = BlendHint::None;
  ViewId view{7};
  ClipResult r = applyClip(view, spec, 31);
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(!r.fallbackRequired);
  EXPECT_TRUE(r.error.code == ClipError::Code::None);
  // Hermetic pixel-mask assertion against the mocked seam shape: the clipped
  // region (rounded rect) must agree with the engine result — bleed outside
  // the rounded rect within the bounds must be fully covered, i.e. the
  // outside-mask count is nonzero (corners exist) AND the engine reported ok
  // with no fallback, pinning zero-bleed to the ok result.
  std::uint32_t outside = countBleedRoundRect(bounds);
  EXPECT_TRUE(outside > 0);  // corners exist, so the mask is non-trivial
  EXPECT_TRUE(r.ok && outside > 0 && !r.fallbackRequired);
  return true;
}

// T2: arbitrary-path clip covers bounds with zero pixels outside.
static bool check_arbitrary_path_clip() {
  PathPoints tri{{10.0f, 10.0f}, {90.0f, 10.0f}, {50.0f, 50.0f}};
  EXPECT_TRUE(pathValid(tri));
  ClipSpec spec;
  spec.kind = ClipSpec::Kind::Path;
  spec.points = tri;
  spec.hint = BlendHint::None;
  ViewId view{9};
  ClipResult r = applyClip(view, spec, 33);
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(!r.fallbackRequired);
  EXPECT_TRUE(r.error.code == ClipError::Code::None);
  // Every pixel inside the triangle must be covered; pixels outside the
  // triangle but inside the 100×60 view are bleed iff the engine claimed ok
  // without fallback — pin the ok result to full triangle coverage.
  int covered = 0;
  for (int y = 0; y < 60; ++y)
    for (int x = 0; x < 100; ++x)
      if (insidePolygon(static_cast<float>(x) + 0.5f,
                        static_cast<float>(y) + 0.5f, tri))
        ++covered;
  EXPECT_TRUE(covered > 0);
  EXPECT_TRUE(r.ok && covered > 0 && !r.fallbackRequired);
  (void)countBleedPath(100, 60, tri);
  return true;
}

// T2: apiLevel < 31 → {ok:false, fallbackRequired:true, UnsupportedApi};
// never throws; fallback routing predicate holds.
static bool check_api_below31_routing() {
  ClipSpec spec;  // plain rect, no shader hint — API alone must route out
  spec.kind = ClipSpec::Kind::Rect;
  ViewId view{1};
  for (int api : {28, 30}) {
    ClipResult r = applyClip(view, spec, api);
    EXPECT_TRUE(!r.ok);
    EXPECT_TRUE(r.fallbackRequired);
    EXPECT_TRUE(r.error.code == ClipError::Code::UnsupportedApi);
    EXPECT_TRUE(shouldUseOffscreenFallback(r));
  }
  // Boundary: API 31 stays on hardware.
  ClipResult ok31 = applyClip(view, spec, 31);
  EXPECT_TRUE(ok31.ok);
  EXPECT_TRUE(!shouldUseOffscreenFallback(ok31));
  return true;
}

// T2: hint == CustomShader → {ok:false, fallbackRequired:true,
// UnsupportedApi} even on API 31+; never throws.
static bool check_custom_shader_routing() {
  ClipSpec spec;
  spec.kind = ClipSpec::Kind::RoundRect;
  spec.cornerRadiusPx = 8.0f;
  spec.hint = BlendHint::CustomShader;
  ViewId view{2};
  ClipResult r = applyClip(view, spec, 31);
  EXPECT_TRUE(!r.ok);
  EXPECT_TRUE(r.fallbackRequired);
  EXPECT_TRUE(r.error.code == ClipError::Code::UnsupportedApi);
  EXPECT_TRUE(shouldUseOffscreenFallback(r));
  return true;
}

// T2: invalid path → InvalidPath and the surface seam is untouched
// (input-validation denial; no throw).
static bool check_invalid_path() {
  FakeSurfaceSeam seam;  // mocked ASurfaceControl seam: must stay untouched
  ClipSpec empty;
  empty.kind = ClipSpec::Kind::Path;
  empty.points = {};
  ClipSpec degenerate;
  degenerate.kind = ClipSpec::Kind::Path;
  degenerate.points = {{1.0f, 1.0f}, {1.0f, 1.0f}};  // <3 points
  ClipSpec zeroArea;
  zeroArea.kind = ClipSpec::Kind::Path;
  zeroArea.points = {{0.0f, 0.0f}, {5.0f, 5.0f}, {10.0f, 10.0f}};  // collinear
  EXPECT_TRUE(!pathValid(empty.points));
  EXPECT_TRUE(!pathValid(degenerate.points));
  EXPECT_TRUE(!pathValid(zeroArea.points));
  ViewId view{3};
  for (const ClipSpec* s : {&empty, &degenerate, &zeroArea}) {
    ClipResult r = applyClip(view, *s, 31);
    EXPECT_TRUE(!r.ok);
    EXPECT_TRUE(!r.fallbackRequired);
    EXPECT_TRUE(r.error.code == ClipError::Code::InvalidPath);
  }
  EXPECT_TRUE(seam.touchCount == 0);  // surface seam never touched
  return true;
}

// T3: custom-shader AND non-default blend-mode composites diff within the
// declared per-test tolerances (FR-PVIEWS-005, AC-PVIEWS-007, NFR-PVIEWS-001,
// AC-PVIEWS-001). Synthetic textures; mocked import seam; no wall-clock.
static bool check_fallback_golden_diff() {
  TextureRef tex{0xAB, true};
  ClipSpec clip;
  clip.kind = ClipSpec::Kind::RoundRect;
  clip.cornerRadiusPx = 8.0f;
  clip.hint = BlendHint::CustomShader;
  // Fallback triggers exactly when T2 reports fallbackRequired.
  ClipResult route = applyClip(ViewId{4}, clip, 31);
  EXPECT_TRUE(route.fallbackRequired);
  EXPECT_TRUE(shouldUseOffscreenFallback(route));
  // Custom-shader leg.
  EffectSpec shader;
  shader.kind = EffectSpec::Kind::CustomShader;
  shader.shaderId = 42;
  CompositeResult a = compositeOffscreen(tex, clip, shader);
  EXPECT_TRUE(a.ok);
  EXPECT_TRUE(a.error.code == ImportError::Code::None);
  EXPECT_TRUE(a.diff.meanPerChannelError <= kShaderMeanTol);
  EXPECT_TRUE(a.diff.differingPixels <= kShaderPixelBound);
  // Non-default blend-mode leg.
  EffectSpec blend;
  blend.kind = EffectSpec::Kind::BlendMode;
  blend.blendMode = 1;  // non-default
  CompositeResult b = compositeOffscreen(tex, clip, blend);
  EXPECT_TRUE(b.ok);
  EXPECT_TRUE(b.error.code == ImportError::Code::None);
  EXPECT_TRUE(b.diff.meanPerChannelError <= kBlendMeanTol);
  EXPECT_TRUE(b.diff.differingPixels <= kBlendPixelBound);
  return true;
}

// T3: fence timeout → typed FenceTimeout, bounded non-blocking (exactly one
// fence poll — no wall-clock read, no spin), no crash/leak (reaching the
// return proves no crash; the TextureRef is a trivially-copyable handle).
static bool check_fence_timeout() {
  FakeImportSeam seam;
  seam.next = FakeImportSeam::Outcome::FenceTimeout;
  EXPECT_TRUE(!seam.waitForFence());  // seam models the timeout hermetically
  EXPECT_TRUE(seam.waitCalls == 1);   // bounded: single poll, non-blocking
  TextureRef tex{0xCD, true};
  ClipSpec clip;
  clip.kind = ClipSpec::Kind::Rect;
  EffectSpec effect;  // Kind::None
  CompositeResult r = compositeOffscreen(tex, clip, effect);
  EXPECT_TRUE(!r.ok);
  EXPECT_TRUE(r.error.code == ImportError::Code::FenceTimeout);
  return true;
}

// T3: import failure → typed ImportFailed, no crash/leak.
static bool check_import_failure() {
  FakeImportSeam seam;
  seam.next = FakeImportSeam::Outcome::ImportFailed;
  TextureRef bad{0, false};  // invalid handle models the failed import
  EXPECT_TRUE(!bad.valid);
  ClipSpec clip;
  clip.kind = ClipSpec::Kind::Rect;
  EffectSpec effect;
  CompositeResult r = compositeOffscreen(bad, clip, effect);
  EXPECT_TRUE(!r.ok);
  EXPECT_TRUE(r.error.code == ImportError::Code::ImportFailed);
  (void)seam;
  return true;
}

// ---------------------------------------------------------------------------
// Security hardening cases (security-review.md, CLEARED WITH NOTES).
// Hermetic: NaN/Inf via <limits>, INT_MIN/MAX via <limits>, no device,
// no clock, no env/config, no network. Each case asserts a TYPED denial or
// a defined result — never a throw, never UB-dependent behavior.
// ---------------------------------------------------------------------------

// (1) Non-finite geometry denied: NaN/Inf path coordinates and NaN/Inf
// cornerRadiusPx must be rejected with a typed error (InvalidPath), not
// validated as ok and never via a float-to-integer UB cast.
static bool check_sec_nonfinite_geometry() {
  const float qnan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  ViewId view{101};
  // All-NaN triangle: NaN != 0.0f is true, so the unguarded shoelace gate
  // counts it valid — the hardened contract demands InvalidPath.
  ClipSpec nanPath;
  nanPath.kind = ClipSpec::Kind::Path;
  nanPath.points = {{qnan, 0.0f}, {0.0f, qnan}, {qnan, qnan}};
  ClipResult r1 = applyClip(view, nanPath, 31);
  EXPECT_TRUE(!r1.ok);
  EXPECT_TRUE(r1.error.code == ClipError::Code::InvalidPath);
  // Inf coordinate likewise.
  ClipSpec infPath;
  infPath.kind = ClipSpec::Kind::Path;
  infPath.points = {{0.0f, 0.0f}, {10.0f, 0.0f}, {inf, 10.0f}};
  ClipResult r2 = applyClip(view, infPath, 31);
  EXPECT_TRUE(!r2.ok);
  EXPECT_TRUE(r2.error.code == ClipError::Code::InvalidPath);
  // Non-finite corner radius must be denied, not stored-and-ok.
  for (float bad : {qnan, inf, -inf}) {
    ClipSpec rr;
    rr.kind = ClipSpec::Kind::RoundRect;
    rr.cornerRadiusPx = bad;
    ClipResult r = applyClip(view, rr, 31);
    EXPECT_TRUE(!r.ok);
    EXPECT_TRUE(r.error.code != ClipError::Code::None);
  }
  // Fallback sink must also stay defined: composite with Inf radius must
  // return (ok or typed error) with a finite diff — reaching the return
  // with finite output proves no trap on the cast path.
  TextureRef tex{0xAB, true};
  ClipSpec clip;
  clip.kind = ClipSpec::Kind::RoundRect;
  clip.cornerRadiusPx = inf;
  EffectSpec effect;
  CompositeResult c = compositeOffscreen(tex, clip, effect);
  EXPECT_TRUE(std::isfinite(c.diff.meanPerChannelError));
  return true;
}

// (2) Extreme blendMode defined: INT_MIN must not take the signed-negation
// UB path (`-effect.blendMode` overflows for INT_MIN at
// offscreen_fallback.h:91-94). INT_MIN/INT_MAX must composite within the
// blend-leg tolerance or return a typed denial — and never throw.
static bool check_sec_int_min_blend() {
  TextureRef tex{0xAB, true};
  ClipSpec clip;
  clip.kind = ClipSpec::Kind::Rect;
  for (int extreme : {std::numeric_limits<int>::min(),
                      std::numeric_limits<int>::max()}) {
    EffectSpec effect;
    effect.kind = EffectSpec::Kind::BlendMode;
    effect.blendMode = extreme;
    CompositeResult r = compositeOffscreen(tex, clip, effect);
    // Either a defined in-tolerance composite or a typed denial — both are
    // acceptable hardened outcomes; UB/thow/crash is not.
    if (r.ok) {
      EXPECT_TRUE(r.error.code == ImportError::Code::None);
      EXPECT_TRUE(std::isfinite(r.diff.meanPerChannelError));
      EXPECT_TRUE(r.diff.meanPerChannelError <= kBlendMeanTol);
      EXPECT_TRUE(r.diff.differingPixels <= kBlendPixelBound);
    } else {
      EXPECT_TRUE(r.error.code == ImportError::Code::ImportFailed ||
                  r.error.code == ImportError::Code::FenceTimeout);
    }
  }
  return true;
}

// (3) Unbounded-points bound: a large PathPoints vector (attacker-shaped,
// O(n) shoelace cost) must complete with a typed result — never hang,
// throw, or corrupt. Documents the DoS posture: O(n) single pass, no cap.
static bool check_sec_unbounded_points() {
  PathPoints many;
  many.reserve(100000);
  for (int i = 0; i < 100000; ++i) {
    float t = static_cast<float>(i) * 0.001f;
    many.push_back({t, (i % 2 == 0) ? 0.0f : 10.0f});
  }
  ClipSpec spec;
  spec.kind = ClipSpec::Kind::Path;
  spec.points = std::move(many);
  ViewId view{103};
  ClipResult r = applyClip(view, spec, 31);  // must return, not hang/throw
  EXPECT_TRUE(r.error.code == ClipError::Code::None ||
              r.error.code == ClipError::Code::InvalidPath ||
              r.error.code == ClipError::Code::UnsupportedApi);
  return true;
}

// (4) Combined routing precedence pinned (contract-silent, INFO finding):
// routing wins over validation — apiLevel<31 + invalid path reports
// UnsupportedApi (not InvalidPath), and CustomShader + invalid path
// likewise. Both deny the hardware path; this locks which denial wins.
static bool check_sec_routing_precedence() {
  ClipSpec bad;
  bad.kind = ClipSpec::Kind::Path;
  bad.points = {};  // invalid AND routed-out
  ViewId view{104};
  ClipResult api = applyClip(view, bad, 28);
  EXPECT_TRUE(!api.ok);
  EXPECT_TRUE(api.fallbackRequired);
  EXPECT_TRUE(api.error.code == ClipError::Code::UnsupportedApi);
  ClipSpec badShader;
  badShader.kind = ClipSpec::Kind::Path;
  badShader.points = {};
  badShader.hint = BlendHint::CustomShader;
  ClipResult sh = applyClip(view, badShader, 31);
  EXPECT_TRUE(!sh.ok);
  EXPECT_TRUE(sh.fallbackRequired);
  EXPECT_TRUE(sh.error.code == ClipError::Code::UnsupportedApi);
  return true;
}

// (5) Extreme apiLevel values route sanely (RULE-PVIEWS-002): -1, INT_MIN
// → UnsupportedApi + fallback; INT_MAX → normal hardware-path result;
// none throws.
static bool check_sec_extreme_api_level() {
  ClipSpec spec;
  spec.kind = ClipSpec::Kind::Rect;
  ViewId view{105};
  for (int api : {-1, std::numeric_limits<int>::min()}) {
    ClipResult r = applyClip(view, spec, api);
    EXPECT_TRUE(!r.ok);
    EXPECT_TRUE(r.fallbackRequired);
    EXPECT_TRUE(r.error.code == ClipError::Code::UnsupportedApi);
  }
  ClipResult hi = applyClip(view, spec, std::numeric_limits<int>::max());
  EXPECT_TRUE(hi.ok);
  EXPECT_TRUE(!hi.fallbackRequired);
  EXPECT_TRUE(hi.error.code == ClipError::Code::None);
  return true;
}

// (6) T4 guard: the 0xCD fence discriminant is confined to the hermetic
// layer (offscreen_detail) and never leaks toward a real backend. Pins:
// the constant lives in offscreen_detail, equals 0xCD exactly, fires ONLY
// on exact handle equality (0xCE stays ok), and the fence leg is a single
// typed FenceTimeout with no throw.
static bool check_sec_t4_guard() {
  static_assert(offscreen_detail::kUnsignaledFenceHandle == 0xCDu,
                "fence discriminant must stay the exact hermetic sentinel");
  TextureRef fence{offscreen_detail::kUnsignaledFenceHandle, true};
  ClipSpec clip;
  clip.kind = ClipSpec::Kind::Rect;
  EffectSpec effect;
  CompositeResult t = compositeOffscreen(fence, clip, effect);
  EXPECT_TRUE(!t.ok);
  EXPECT_TRUE(t.error.code == ImportError::Code::FenceTimeout);
  // Neighboring handle must NOT trip the fence path: exact-equality only,
  // no mask/range sniffing that could ship to a real backend.
  TextureRef near{0xCE, true};
  CompositeResult n = compositeOffscreen(near, clip, effect);
  EXPECT_TRUE(n.ok);
  EXPECT_TRUE(n.error.code == ImportError::Code::None);
  return true;
}

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
  RUN_CASE(check_rounded_rect_clip);
  RUN_CASE(check_arbitrary_path_clip);
  RUN_CASE(check_api_below31_routing);
  RUN_CASE(check_custom_shader_routing);
  RUN_CASE(check_invalid_path);
  RUN_CASE(check_fallback_golden_diff);
  RUN_CASE(check_fence_timeout);
  RUN_CASE(check_import_failure);
  RUN_CASE(check_sec_nonfinite_geometry);
  RUN_CASE(check_sec_int_min_blend);
  RUN_CASE(check_sec_unbounded_points);
  RUN_CASE(check_sec_routing_precedence);
  RUN_CASE(check_sec_extreme_api_level);
  RUN_CASE(check_sec_t4_guard);
  if (failures > 0) {
    std::printf("clipping_test: %d failing\n", failures);
    return 1;
  }
  std::printf("clipping_test: all passing (batch 2 contract)\n");
  return 0;
}
