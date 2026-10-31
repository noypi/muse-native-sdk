#ifndef GRAPHICS_SKIA_PRESENT_SKIA_H_
#define GRAPHICS_SKIA_PRESENT_SKIA_H_

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

#endif  // GRAPHICS_SKIA_PRESENT_SKIA_H_
