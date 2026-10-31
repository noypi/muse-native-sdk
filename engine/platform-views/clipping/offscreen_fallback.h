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
