#pragma once

// FR-GRAPHICS-012 T2: EGL blit presenter (consume-only pixels lane).
//
// Uploads the finished CPU pixel buffer as a texture, draws one fullscreen
// quad, and calls eglSwapBuffers exactly once per frame. GLES never sees a
// component, only finished pixels.
//
// Host build uses a stub EGL (no real EGL on host); the device lane (Android
// EGL on 127.0.0.1:6562) provides the real calls behind the same boundary.
// One presenter per ANativeWindow: EGL and Vulkan never co-present.
//
// NOTE (link shape): engine/runtime/CMakeLists.txt registers only the
// `egl_blit_test` executable (contract block) and does not add this TU to
// the `runtime` static lib, so the full implementation lives inline in this
// header; `src/egl_blit.cpp` is a thin including TU that keeps the pair in
// sync. Device lane may replace the stub bodies without touching the tests.

#include <cstdint>
#include <string>

enum class BlitCode {
  kOk,
  kInitFailed,
  kContextLost,
  kSwapFailed,
  kNoSurface,
};

struct BlitError {
  BlitCode code;
  std::string trigger;
};

bool EglBlitInit(void* opaque_window, BlitError* error);
bool EglBlitPresent(const uint32_t* rgba, int w, int h, BlitError* error);
void EglBlitShutdown();

// Test-only fault-injection seam (host stub-EGL; device lane may no-op it).
// nullptr clears all faults. Minimal surface for the plan's "stub EGL
// (swap-fail injection -> kSwapFailed)" requirement.
namespace egl_blit_test {
struct FaultHooks {
  bool fail_init = false;       // EGL init path reports failure
  bool lose_context = false;    // context reports lost
  bool fail_next_swap = false;  // next eglSwapBuffers reports failure
  bool no_surface = false;      // backgrounded/destroyed surface
};
void SetFaultHooks(const FaultHooks* hooks);  // nullptr clears
}  // namespace egl_blit_test

// --- inline stub-EGL implementation ----------------------------------------
// Single presenter state: at most one live window at a time.
namespace egl_blit_detail {
struct State {
  bool initialized = false;
  void* window = nullptr;
  egl_blit_test::FaultHooks hooks;
};
inline State& BlitState() {
  static State s;
  return s;
}
inline void SetError(BlitError* error, BlitCode code, const char* trigger) {
  if (error != nullptr) {
    error->code = code;
    error->trigger = trigger != nullptr ? trigger : "";
  }
}
// Stub EGL calls: each names the real EGL entry it stands in for so fault
// triggers carry the failing call name.
inline bool StubCreateSurface() {
  return !BlitState().hooks.no_surface;
}
inline bool StubMakeCurrent() {
  return !BlitState().hooks.lose_context;
}
inline bool StubSwapOnce() {
  // Exactly one eglSwapBuffers per presented frame; one-shot fault.
  if (BlitState().hooks.fail_next_swap) {
    BlitState().hooks.fail_next_swap = false;
    return false;
  }
  return true;
}
}  // namespace egl_blit_detail

inline void egl_blit_test::SetFaultHooks(const FaultHooks* hooks) {
  if (hooks == nullptr) {
    egl_blit_detail::BlitState().hooks = FaultHooks{};
  } else {
    egl_blit_detail::BlitState().hooks = *hooks;
  }
}

inline bool EglBlitInit(void* opaque_window, BlitError* error) {
  egl_blit_detail::State& s = egl_blit_detail::BlitState();
  if (opaque_window == nullptr) {
    // Fail closed: no window means no surface to create.
    egl_blit_detail::SetError(error, BlitCode::kNoSurface,
                              "eglCreateWindowSurface: null window");
    return false;
  }
  if (s.hooks.fail_init) {
    egl_blit_detail::SetError(error, BlitCode::kInitFailed,
                              "eglInitialize: injected failure");
    return false;
  }
  if (!egl_blit_detail::StubCreateSurface()) {
    egl_blit_detail::SetError(error, BlitCode::kNoSurface,
                              "eglCreateWindowSurface: no surface");
    return false;
  }
  if (s.initialized) {
    // One presenter per ANativeWindow: a second init while live fails closed.
    egl_blit_detail::SetError(
        error, BlitCode::kInitFailed,
        "eglCreateWindowSurface: already initialized (one presenter per "
        "window)");
    return false;
  }
  s.initialized = true;
  s.window = opaque_window;
  egl_blit_detail::SetError(error, BlitCode::kOk, "");
  return true;
}

inline bool EglBlitPresent(const uint32_t* rgba, int w, int h,
                           BlitError* error) {
  egl_blit_detail::State& s = egl_blit_detail::BlitState();
  if (!s.initialized) {
    egl_blit_detail::SetError(error, BlitCode::kNoSurface,
                              "eglMakeCurrent: no surface (present without "
                              "init)");
    return false;
  }
  if (rgba == nullptr) {
    egl_blit_detail::SetError(error, BlitCode::kNoSurface,
                              "eglTexImage2D: null pixels (nothing to "
                              "upload)");
    return false;
  }
  if (w <= 0 || h <= 0) {
    egl_blit_detail::SetError(error, BlitCode::kNoSurface,
                              "eglTexImage2D: invalid dimensions");
    return false;
  }
  if (s.hooks.no_surface) {
    egl_blit_detail::SetError(error, BlitCode::kNoSurface,
                              "eglCreateWindowSurface: no surface");
    return false;
  }
  if (!egl_blit_detail::StubMakeCurrent()) {
    egl_blit_detail::SetError(error, BlitCode::kContextLost,
                              "eglMakeCurrent: context lost");
    return false;
  }
  // Upload finished CPU pixels as a texture and draw one fullscreen quad
  // (stubbed on host: GLES sees only finished pixels, never a component).
  (void)rgba;
  (void)w;
  (void)h;
  // Exactly one swap per frame.
  if (!egl_blit_detail::StubSwapOnce()) {
    egl_blit_detail::SetError(error, BlitCode::kSwapFailed,
                              "eglSwapBuffers: injected swap failure");
    return false;
  }
  egl_blit_detail::SetError(error, BlitCode::kOk, "");
  return true;
}

inline void EglBlitShutdown() {
  egl_blit_detail::State& s = egl_blit_detail::BlitState();
  s.initialized = false;
  s.window = nullptr;
}

// FR-GRAPHICS-012 T3: automatic in-process CPU fallback (see plan item T3).
bool PresentFrameWithFallback(const uint32_t* rgba, int w, int h, std::string* fallback_trigger);
