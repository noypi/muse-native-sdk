// Skia gallery present leg (FR-003 / AC-GRAPHICS-039 T2, TC-GRAPHICS-040).
// Present-then-fallback structure reused from `engine/runtime/src/egl_blit.cpp`:
// Skia init -> context -> swap; any failure falls to the proven in-process
// CPU SurfaceView push (same process, no blank frame) and reports the Skia
// trigger as `GFX-SKIA-FALLBACK: <trigger>` on stderr.
//
// Link note: host/hermetic build needs no real Skia SDK or GPU. The stub seam
// below stands in for the real Skia calls (each names the entry it stands in
// for so triggers carry the failing call name); the device leg (T5) replaces
// the stub bodies without touching the tests or the public signature.

#include "present_skia.h"

#include <cstdio>

namespace {

// One `GFX-SKIA-FALLBACK: <trigger>` log line per failure (TC-GRAPHICS-040
// exact check: greppable on the device leg via logcat, stderr on host).
void EmitSkiaFallbackLog(const char* trigger) {
  std::fprintf(stderr, "GFX-SKIA-FALLBACK: %s\n",
               trigger != nullptr ? trigger : "unknown");
}

// --- host stub-Skia seam (device leg T5 provides the real swap) ------------
// Each stub names the real Skia entry it stands in for. All succeed on host;
// the fallback branch below stays linked for the device leg.
bool StubSkiaInit() { return true; }
bool StubSkiaMakeContext() { return true; }
bool StubSkiaSwapOnce() { return true; }

// Proven in-process CPU SurfaceView push, same process. Host lane is a stub
// (no NDK calls execute in hermetic tests; observable behavior is the return
// value + fallback log). Device lane posts the caller-owned pixels — reaching
// here only records the trigger + log, never a blank frame, never a second
// process.
void CpuSurfaceViewPush() {}

// Minimal 1x1 opaque PNG dumped on the presented path (device leg writes the
// real gallery frame here; the host stub writes this placeholder so the
// `--dump-frame` shape holds without a GPU).
const unsigned char kPlaceholderPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
    0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xFF, 0xFF, 0x3F,
    0x00, 0x05, 0xFE, 0x02, 0xFE, 0xDC, 0xCC, 0x59, 0xE7, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

// Failure terminal: CPU fallback push (same process, no blank frame) + log,
// then the nonzero contract code. Never throws.
int FailWithFallback(const char* trigger) {
  CpuSurfaceViewPush();
  EmitSkiaFallbackLog(trigger);
  return 1;
}

}  // namespace

int GalleryPresentSkia(const char* png_out_path) {
  if (png_out_path == nullptr) {
    return FailWithFallback("null png_out_path");
  }
  if (png_out_path[0] == '\0') {
    return FailWithFallback("empty png_out_path");
  }
  if (!StubSkiaInit()) {
    return FailWithFallback("skiaInitialize: init failed");
  }
  if (!StubSkiaMakeContext()) {
    return FailWithFallback("skiaMakeContext: context lost");
  }
  if (!StubSkiaSwapOnce()) {
    return FailWithFallback("skiaSwapBuffers: present failed");
  }
  std::FILE* out = std::fopen(png_out_path, "wb");
  if (out == nullptr) {
    return FailWithFallback("fopen: unwritable png_out_path");
  }
  const std::size_t wrote =
      std::fwrite(kPlaceholderPng, 1, sizeof(kPlaceholderPng), out);
  std::fclose(out);
  if (wrote != sizeof(kPlaceholderPng)) {
    return FailWithFallback("fwrite: frame dump failed");
  }
  return 0;
}
