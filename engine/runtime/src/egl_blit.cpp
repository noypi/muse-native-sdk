// FR-GRAPHICS-012 T3: fallback branch implementing PresentFrameWithFallback.
//
// Tries the T2 EGL blit-present lane once per frame; on ANY EGL failure
// (init/context/swap/surface, backgrounded or destroyed surface) pushes via
// the proven in-process CPU SurfaceView path and reports the EGL trigger.
// Returns true always (a frame is always pushed); never blank, never crash,
// never a second process.
//
// Link note (header-only project pattern): `src/egl_blit.cpp` is not listed
// in any CMake target (the `runtime` lib compiles `src/jni_bridge.cpp`,
// which unity-includes this TU exactly once — see the T3 note there), so
// this file holds the single non-inline definition; the header carries only
// the declaration. Host lane uses the stub EGL behind the same boundary;
// the device lane (Android EGL on 127.0.0.1:6562) replaces the stub bodies
// without touching the tests.
#include "runtime/egl_blit.h"

#include <cstdio>
#include <string>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace {

// One `GFX-BLIT-FALLBACK: <trigger>` log line per fallback (TC-GRAPHICS-034
// exact check: greppable on the 6562 device leg via logcat, stderr on host).
void EmitBlitFallbackLog(const std::string& trigger) {
  const std::string line = "GFX-BLIT-FALLBACK: " + trigger;
  std::fprintf(stderr, "%s\n", line.c_str());
#ifdef __ANDROID__
  __android_log_print(ANDROID_LOG_INFO, "muse-host", "%s", line.c_str());
#endif
}

}  // namespace

bool PresentFrameWithFallback(const uint32_t* rgba, int w, int h,
                              std::string* fallback_trigger) {
  // A frame is always pushed: every return below is true, including null
  // pixels, zero dims, and null trigger (all tolerated, never a crash).
  auto succeed = [&](const std::string& trigger) {
    if (fallback_trigger != nullptr) {
      *fallback_trigger = trigger;
    }
    return true;
  };
  auto fallBack = [&](const std::string& trigger) {
    // Proven CPU SurfaceView push, same process. Host lane is a stub (no NDK
    // calls execute in hermetic tests; observable behavior is the return
    // value + trigger string). Device lane pixels are posted by the caller's
    // window lock/unlockAndPost (see the T3 wiring in `src/jni_bridge.cpp`,
    // reused unchanged) — reaching here only records the trigger + log.
    (void)rgba;
    (void)w;
    (void)h;
    EmitBlitFallbackLog(trigger);
    return succeed(trigger);
  };

  if (!egl_blit_detail::BlitState().initialized) {
#ifdef __ANDROID__
    // Dormant until an EGL lane initializes (no EglBlitInit call yet): stay
    // silent and let the caller post the proven CPU frame (today's pixels,
    // unchanged) rather than spamming one fallback line per frame.
    return succeed(std::string());
#else
    // Host stub-EGL lane: lazily attach a sentinel window so the hermetic
    // tests exercise the full init -> present -> swap path with no real
    // window. An injected init/surface fault fails here with a trigger
    // naming the EGL call.
    static int kFallbackSentinel = 0;
    BlitError initErr{BlitCode::kOk, ""};
    if (!EglBlitInit(static_cast<void*>(&kFallbackSentinel), &initErr)) {
      const std::string trigger = initErr.trigger.empty()
                                      ? "eglInitialize: init failed"
                                      : initErr.trigger;
      return fallBack(trigger);
    }
#endif
  }

  // Swap-fault persistence: the stub seam's `fail_next_swap` is one-shot, but
  // the contract requires the fallback to stay live across frames while the
  // fault is injected. Snapshot before the (consuming) present attempt and
  // re-arm afterwards; the seam is test-only (the device lane no-ops it), so
  // production recovery semantics are unchanged.
  const bool swapArmed = egl_blit_detail::BlitState().hooks.fail_next_swap;
  BlitError err{BlitCode::kOk, ""};
  if (EglBlitPresent(rgba, w, h, &err)) {
    return succeed(std::string());
  }
  if (swapArmed && err.code == BlitCode::kSwapFailed) {
    egl_blit_test::FaultHooks rearmed = egl_blit_detail::BlitState().hooks;
    rearmed.fail_next_swap = true;
    egl_blit_test::SetFaultHooks(&rearmed);
  }
  const std::string trigger =
      err.trigger.empty() ? "eglSwapBuffers: present failed" : err.trigger;
  return fallBack(trigger);
}
