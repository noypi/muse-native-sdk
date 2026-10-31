# Platform Views Strategy — API 29+ (FR-PVIEWS-008 umbrella)

Scope: feature `nfr-pviews-001-nfr-pviews-002-fr-pviews-008-fr-pviews-001`, item T4 (docs-only alignment note, no callable surface).
Spec source: `manual-context.local/specs-platform-views-embedding.md`, FR-PVIEWS-008 (no AC/TC).

## Single strategy (API 29+)

The primary platform-views approach for Android API 29+ is a single zero-copy surface
compositing model with a hardware-clipping layer and a compatibility layer for SDKs that
require real Views. The Vulkan DAG remains the single compositor and z-order authority.

Mechanism stack, verbatim from the spec Strategy section:

> Sync-Fenced AHardwareBuffer + VK_EXTERNAL_MEMORY Zero-Copy Compositing +
> ASurfaceControl_reparent Hierarchy + ASurfaceControl Frame Pacing + Dynamic Surface
> Clipping + Hybrid View Embedding for complex SDKs.

In full, per the spec Strategy section: platform views (WebViews, Maps) write directly
into AHardwareBuffers synchronized with EGL/Vulkan native sync fences and are imported
into Vulkan as VkImages; ASurfaceControl_reparent attaches sub-surfaces directly above
or below the Vulkan surface for hardware clipping, while ASurfaceControl syncs native
view transformations and system IME soft-keyboard animations with Vulkan dynamic render
passes; Dynamic Surface Clipping enforces ASurfaceControl hardware clip regions and
corner radius properties on API 31+, with offscreen Vulkan compositing fallback for
complex blending; Hybrid View Embedding handles complex native SDKs (AdMob, banking
SDKs) via dedicated Activity/Window layers with HardwareRenderer export.

## Trace to FR-PVIEWS-001/005/006 paths

- FR-PVIEWS-001 (z-order/clipping, AC-PVIEWS-002): ASurfaceControl_reparent hierarchy +
  Dynamic Surface Clipping path — covered in this run by T3.
- FR-PVIEWS-005 (Dynamic Surface Clipping, AC-PVIEWS-007): hardware clip regions /
  corner radius on API 31+ plus offscreen Vulkan compositing fallback — behavior
  out-of-scope for this run (not implemented; T3 covers only the FR-PVIEWS-001
  reparent/clip slice).
- FR-PVIEWS-006 (Hybrid View Embedding, AC-PVIEWS-008): dedicated hidden
  Activity/Window layer with HardwareRenderer export consumed via
  VK_ANDROID_external_memory_android_hardware_buffer — behavior out-of-scope for this
  run (not implemented).

## Item coverage map

- T1 — zero-copy import/fallback probe (`probeExternalMemory`, NFR-PVIEWS-001 /
  AC-PVIEWS-001): implements the Sync-Fenced AHardwareBuffer + VK_EXTERNAL_MEMORY
  zero-copy leg of the stack.
- T2 — device-lab zero-copy Perfetto trace on 127.0.0.1:6562 (NFR-PVIEWS-002 /
  AC-PVIEWS-006): evidences the zero-copy + frame-pacing leg of the stack.
- T3 — z-order/clipping via ASurfaceControl_reparent (`placeSurface`, FR-PVIEWS-001 /
  AC-PVIEWS-002): implements the reparent-hierarchy + clipping slice of the stack.
- FR-PVIEWS-005 / FR-PVIEWS-006 behavior: explicitly out-of-scope for this run; this
  doc traces their paths but asserts no behavior for them.
