#ifndef GRAPHICS_SKIA_PROBE_SKIA_H_
#define GRAPHICS_SKIA_PROBE_SKIA_H_

// Skia backend capability-probe selection leg (FR-003 / AC-GRAPHICS-039 T1).
// Selection only: decides whether the Skia backend joins the FR-003
// capability-probe selection set alongside the Vulkan/GLES paths. Mirrors
// the `BackendProbe` selection shape in `engine/runtime/src/presenter.cpp`
// without touching its existing selection arms; engine-side only, no
// package GPU symbols (FR-PKG-007).
//
// Hermetic: plain C++17 aggregates, no Skia SDK calls, no GPU context, no
// I/O. `ProbeSkia` never throws; `reason` always points at a static string
// naming the select/skip trigger.

struct SkiaProbeInput {
  bool skia_available = false;
  bool cpu_baseline_ok = false;
};

struct SkiaProbeResult {
  bool selected = false;
  const char* reason = nullptr;
};

// Returns `selected=true` only when `skia_available && cpu_baseline_ok`.
// Never throws; `reason` is a static string naming the select/skip trigger.
SkiaProbeResult ProbeSkia(const SkiaProbeInput& input) noexcept;

// FR-GRAPHICS-016 / AC-GRAPHICS-045: standardized Skia backend variant is
// Graphite (selection-only; no raster/perf change). Single-variant enum so
// the selection leg names the Graphite backend without touching Skia SDK,
// GPU context, or AC-039 probe semantics.
enum class SkiaBackendVariant {
  kGraphite,
};

// Returns `kGraphite` whenever `ProbeSkia(input).selected` is true; error
// shape: never throws, no sentinel needed (unselected input yields
// `kGraphite` pending-selection value; callers must still gate on
// `ProbeSkia().selected`). Deterministic pure function of `input`.
SkiaBackendVariant SelectSkiaBackendVariant(const SkiaProbeInput& input) noexcept;

#endif  // GRAPHICS_SKIA_PROBE_SKIA_H_
