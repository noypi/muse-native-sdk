#ifndef GRAPHICS_SKIA_PRESENT_SKIA_H_
#define GRAPHICS_SKIA_PRESENT_SKIA_H_

#include <cstdint>
#include <string>

// Skia gallery present leg (FR-003 / AC-GRAPHICS-039 T2, TC-GRAPHICS-040).
// Presents the gallery scene through the Skia path and dumps a frame to
// `png_out_path` (`muse-native gallery-present --path skia --dump-frame`).
// Engine-side only, no package GPU symbols (FR-PKG-007).
//
// Hermetic: plain C++17, no Skia SDK calls, no GPU context on host. The real
// Skia swap happens on the device leg (T5); the host lane uses the stub seam
// below (same shape as `engine/runtime/src/egl_blit.cpp`).

// Presents one gallery frame via Skia, dumping to `png_out_path`.
// `png_out_path` must be a non-null writable path. Returns 0 when the frame
// was presented, nonzero on failure (emits `GFX-SKIA-FALLBACK: <trigger>`
// on stderr). Never throws.
int GalleryPresentSkia(const char* png_out_path);

// AC-GRAPHICS-039 T3: Skia fallback contract (ported from the AC-034
// `PresentFrameWithFallback` contract in `engine/runtime/src/egl_blit.cpp`).
// Tries the Skia init -> context -> swap lane once per frame; on ANY Skia
// failure (init/context/swap/surface) pushes via the proven in-process CPU
// SurfaceView path and reports the Skia trigger. Returns true always (a
// frame is always pushed); null pixels/dims and a null trigger are
// tolerated, never a crash, never a blank frame, never a second process.
// On fallback sets `*fallback_trigger` to one of
// `skia-init-fail|skia-context-lost|skia-swap-fail|skia-no-surface` and
// emits `GFX-BLIT-FALLBACK: <trigger>`; on success clears it to empty.
bool PresentSkiaFrameWithFallback(const uint32_t* rgba, int w, int h,
                                  std::string* fallback_trigger);

// Test-only fault-injection seam (extended `egl_blit_test::SetFaultHooks`
// shape; device lane may no-op it). `nullptr` clears all faults.
namespace skia_test {
struct FaultHooks {
  bool fail_init = false;      // Skia init path reports failure
  bool lose_context = false;   // Skia context reports lost
  bool fail_next_swap = false;  // next Skia swap reports failure
  bool no_surface = false;     // no surface to present into
};
void SetFaultHooks(const FaultHooks* hooks);  // nullptr clears
}  // namespace skia_test

#endif  // GRAPHICS_SKIA_PRESENT_SKIA_H_
