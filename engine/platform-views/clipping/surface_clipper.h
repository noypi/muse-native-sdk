#pragma once

// T1 scaffold — FR-PVIEWS-005 / AC-PVIEWS-007 / TC-PVIEWS-007.
// Declare-only header; T2 implements applyClip. No Vulkan/JNI calls here;
// platform calls stay behind seams mockable hermetically.
#include <cstdint>
#include <vector>

struct ViewId {
  std::uint64_t value = 0;
};

struct PathPoint {
  float x = 0.0f;
  float y = 0.0f;
};

using PathPoints = std::vector<PathPoint>;

enum class BlendHint {
  None,
  BlendMode,
  CustomShader,
};

struct ClipSpec {
  enum class Kind {
    Rect,
    RoundRect,
    Path,
  };
  Kind kind = Kind::Rect;
  float cornerRadiusPx = 0.0f;
  PathPoints points;
  BlendHint hint = BlendHint::None;
};

struct ClipError {
  enum class Code {
    None,
    UnsupportedApi,
    InvalidPath,
    SurfaceGone,
  };
  Code code = Code::None;
  const char* detail = "";
};

struct ClipResult {
  bool ok = false;
  bool fallbackRequired = false;
  ClipError error;
};

// Applies the ASurfaceControl hardware clip region / corner-radius property
// for `view` on API 31+. Returns {ok:false, fallbackRequired:true,
// error=UnsupportedApi} when `apiLevel < 31` or the blend hint requests a
// custom shader; never throws. (T2 implements.)
ClipResult applyClip(ViewId view, const ClipSpec& spec, int apiLevel);
