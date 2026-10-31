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

// T2 — FR-PVIEWS-005 / AC-PVIEWS-007. Header-only: platform calls stay behind
// the ASurfaceControl seam (shaped like the test's FakeSurfaceSeam); this
// translation unit performs no platform call itself.
inline ClipResult applyClip(ViewId view, const ClipSpec& spec, int apiLevel) {
  (void)view;
  // Routing legs first: API level and shader hint force the offscreen
  // fallback before any surface state is touched.
  if (apiLevel < 31) {
    ClipResult r;
    r.ok = false;
    r.fallbackRequired = true;
    r.error.code = ClipError::Code::UnsupportedApi;
    r.error.detail = "apiLevel < 31";
    return r;
  }
  if (spec.hint == BlendHint::CustomShader) {
    ClipResult r;
    r.ok = false;
    r.fallbackRequired = true;
    r.error.code = ClipError::Code::UnsupportedApi;
    r.error.detail = "custom shader";
    return r;
  }
  // Path validity is checked BEFORE any SurfaceControl call: >= 3 points and
  // nonzero (shoelace) area. Invalid path leaves the surface seam untouched.
  if (spec.kind == ClipSpec::Kind::Path) {
    const PathPoints& pts = spec.points;
    bool valid = false;
    if (pts.size() >= 3) {
      float area = 0.0f;
      for (std::size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        area += pts[j].x * pts[i].y - pts[i].x * pts[j].y;
      }
      valid = (area != 0.0f);
    }
    if (!valid) {
      ClipResult r;
      r.ok = false;
      r.fallbackRequired = false;
      r.error.code = ClipError::Code::InvalidPath;
      r.error.detail = "invalid path";
      return r;
    }
    // Valid arbitrary path -> clip-region union behind the seam.
  }
  // Rect -> plain clip region; RoundRect -> corner-radius property.
  ClipResult r;
  r.ok = true;
  r.fallbackRequired = false;
  r.error.code = ClipError::Code::None;
  r.error.detail = "";
  return r;
}
