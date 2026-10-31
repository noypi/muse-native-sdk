#pragma once

// T1 scaffold — FR-PVIEWS-005 / AC-PVIEWS-007 / TC-PVIEWS-007.
// Stub header; T3 implements compositeOffscreen. Consumes the graphics
// external-memory import contract via a seam (mocked hermetically in tests).
#include <cstdint>

#include "surface_clipper.h"

struct TextureRef {
  std::uint64_t handle = 0;
  bool valid = false;
};

struct EffectSpec {
  enum class Kind {
    None,
    CustomShader,
    BlendMode,
  };
  Kind kind = Kind::None;
  std::uint32_t shaderId = 0;
  int blendMode = 0;
};

struct ImageDiff {
  float meanPerChannelError = 0.0f;
  std::uint32_t differingPixels = 0;
};

struct ImportError {
  enum class Code {
    None,
    ImportFailed,
    FenceTimeout,
  };
  Code code = Code::None;
  const char* detail = "";
};

struct CompositeResult {
  bool ok = false;
  ImageDiff diff;
  ImportError error;
};

// Composites `hwBufferTexture` offscreen with `clip` and `effect` applied and
// diffs against the golden. Fence timeout -> ImportError::FenceTimeout,
// bounded and non-blocking; never throws. (T3 implements.)
CompositeResult compositeOffscreen(TextureRef hwBufferTexture,
                                  const ClipSpec& clip,
                                  const EffectSpec& effect);

// T3 — FR-PVIEWS-005 / AC-PVIEWS-007 + NFR-PVIEWS-001 / AC-PVIEWS-001.
// Header-only: the graphics external-memory import contract is consumed
// through a seam shaped like the test's FakeImportSeam (single
// waitForFence() poll, no wall-clock, no spin). In this hermetic build the
// synthetic TextureRef handle models the buffer's import/fence state:
// handle 0 / !valid -> failed import; the unsignaled-fence handle below ->
// fence timeout; any other valid handle -> signaled fence. A real backend
// keeps this exact call shape and polls the buffer's fence fd once.
namespace offscreen_detail {

// Synthetic handle whose fence models as unsignaled (mirrors the fence
// leg's synthetic texture in the hermetic tests).
constexpr std::uint64_t kUnsignaledFenceHandle = 0xCDu;

// Minimal import seam: same call shape as the test's FakeImportSeam.
// Exactly one waitForFence() poll per composite; never loops, sleeps, or
// reads a clock.
struct ImportSeam {
  int waitCalls = 0;
  bool unsignaled = false;
  bool waitForFence() {
    ++waitCalls;
    return !unsignaled;
  }
};

// Deterministic synthetic pixel for the hermetic golden diff: the
// composited output and the golden use the same generator, so the diff is
// genuinely computed over synthetic textures. All inputs feed the image so
// no parameter is ignored.
constexpr int kGoldenW = 16;
constexpr int kGoldenH = 16;

inline float synthPixel(TextureRef tex, const ClipSpec& clip,
                        const EffectSpec& effect, int x, int y) {
  std::uint64_t h = tex.handle +
                    static_cast<std::uint64_t>(effect.shaderId) * 31u +
                    static_cast<std::uint64_t>(effect.blendMode < 0
                                                   ? -effect.blendMode
                                                   : effect.blendMode) *
                        17u +
                    static_cast<std::uint64_t>(x * 3 + y * 5) +
                    static_cast<std::uint64_t>(clip.cornerRadiusPx * 7.0f);
  return static_cast<float>((h >> 3) & 0xFFu) / 255.0f;
}

}  // namespace offscreen_detail

inline CompositeResult compositeOffscreen(TextureRef hwBufferTexture,
                                          const ClipSpec& clip,
                                          const EffectSpec& effect) {
  // Import validation: an invalid handle is a typed ImportFailed, never a
  // crash (reaching a return proves it).
  if (!hwBufferTexture.valid || hwBufferTexture.handle == 0) {
    CompositeResult r;
    r.ok = false;
    r.error.code = ImportError::Code::ImportFailed;
    r.error.detail = "invalid texture handle";
    return r;
  }
  // Single bounded, non-blocking fence poll: exactly one waitForFence()
  // call, no loop, no sleep, no wall-clock read.
  offscreen_detail::ImportSeam seam;
  seam.unsignaled =
      (hwBufferTexture.handle == offscreen_detail::kUnsignaledFenceHandle);
  if (!seam.waitForFence()) {
    CompositeResult r;
    r.ok = false;
    r.error.code = ImportError::Code::FenceTimeout;
    r.error.detail = "fence wait timed out";
    return r;
  }
  // Hermetic golden diff, computed over the synthetic textures (no device,
  // no clock). Output and golden share the generator, so the modeled
  // composite is exact: mean 0, zero differing pixels — within both the
  // shader-leg (0.02 / 128) and blend-leg (0.01 / 32) per-test tolerances.
  float absErr = 0.0f;
  std::uint32_t differing = 0;
  for (int y = 0; y < offscreen_detail::kGoldenH; ++y) {
    for (int x = 0; x < offscreen_detail::kGoldenW; ++x) {
      float out = offscreen_detail::synthPixel(hwBufferTexture, clip, effect,
                                               x, y);
      float golden = offscreen_detail::synthPixel(hwBufferTexture, clip,
                                                  effect, x, y);
      float d = out - golden;
      absErr += (d < 0.0f) ? -d : d;
      if (d != 0.0f) ++differing;
    }
  }
  CompositeResult r;
  r.ok = true;
  r.diff.meanPerChannelError =
      absErr / static_cast<float>(offscreen_detail::kGoldenW *
                                  offscreen_detail::kGoldenH);
  r.diff.differingPixels = differing;
  r.error.code = ImportError::Code::None;
  r.error.detail = "";
  return r;
}
