#pragma once

// AC-GRAPHICS-041 T2: renderer-preference resolve gate.
//
// Resolves the requested renderer ("cpu"|"egl"|"skia") against host
// availability into the effective frame path. Returns true always (a frame
// path is always resolved, never blank); on downgrade the trigger names the
// unavailable lane (`renderer-downgrade:<lane>-unavailable`) and the caller
// takes the proven CPU push. When the requested path is available the
// trigger is empty. Null `requested` means "cpu"; null
// `effectiveOut`/`fallbackTrigger` are tolerated, never a crash; writes are
// bounded by `effectiveLen` (`effectiveLen == 0` writes nothing but still
// returns true with the trigger set).
//
// Link note: header-inline definition (mirrors the `egl_blit.h` header-only
// project pattern); the landed `renderer_preference_fallback_test`
// executable + `add_test` hunk in `engine/runtime/CMakeLists.txt` links the
// `runtime` lib for its include path and compiles this TU directly.
// Trigger vocab (intentional split): the CLI `renderer-preference.js` emits
// human-readable `"<lane> unavailable, downgraded to cpu"` while this header
// emits machine-tag `renderer-downgrade:<lane>-unavailable` in the
// `GFX-BLIT-FALLBACK` style; the two layers never exact-match each other so
// no convergence is needed.

#include <cstddef>
#include <cstring>
#include <string>

inline bool ResolveRendererFallback(const char* requested, bool eglAvailable,
                                    bool skiaAvailable, char* effectiveOut,
                                    size_t effectiveLen,
                                    std::string* fallbackTrigger) {
  // A frame path is always resolved: every return below is true, including
  // null requested, unknown requested, null out-params, and zero-length
  // buffers (all tolerated, never a crash). Ported from the
  // `PresentFrameWithFallback` fallback-branch shape in
  // `engine/runtime/src/egl_blit.cpp` (succeed/fallBack lambdas).
  auto succeed = [&](const char* effective, const std::string& trigger) {
    if (effectiveOut != nullptr && effectiveLen > 0) {
      std::strncpy(effectiveOut, effective, effectiveLen - 1);
      effectiveOut[effectiveLen - 1] = '\0';
    }
    if (fallbackTrigger != nullptr) {
      *fallbackTrigger = trigger;
    }
    return true;
  };
  auto fallBack = [&](const char* trigger) {
    // Proven CPU push, same process: reaching here only records the trigger;
    // the caller posts via the proven CPU path (never blank).
    return succeed("cpu", trigger);
  };

  if (requested == nullptr || std::strcmp(requested, "cpu") == 0) {
    return succeed("cpu", std::string());
  }
  if (std::strcmp(requested, "egl") == 0) {
    if (eglAvailable) {
      return succeed("egl", std::string());
    }
    return fallBack("renderer-downgrade:egl-unavailable");
  }
  if (std::strcmp(requested, "skia") == 0) {
    if (skiaAvailable) {
      return succeed("skia", std::string());
    }
    return fallBack("renderer-downgrade:skia-unavailable");
  }
  // Fail closed: never echo an unknown value as a path; resolve the proven
  // CPU push with a non-empty trigger.
  return fallBack("renderer-downgrade:unknown-requested");
}
