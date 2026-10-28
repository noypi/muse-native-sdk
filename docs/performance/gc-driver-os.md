# GC, driver, and OS overhead (NFR-PERFORMANCE-010)

Scope: honest GC/driver/OS modeling for NFR-PERFORMANCE-010
(AC-PERFORMANCE-023, AC-PERFORMANCE-024). Each overhead source below is
reported as a measured bound or as an explicit target/TBD, consistent
with the wording of `docs/performance/README.md` section 1. Nothing
here is asserted as a measured device-lab result unless stated.

## render-path split

The C++ render path is allocation-stable and render-path GC-free after
bootstrap: steady-state frame work performs no heap allocation on the
hot path and therefore triggers no collection. The Hermes / JS isolate
is outside that path and still collects garbage normally; every
JS-authored callback, action body, and subscriber runs under the
collector and may allocate.

The split is explicit: the C++ side stays render-path GC-free while the
JS isolate still collects. A render-path bound must never be read as a
process-wide claim.

## js-gc bound

- Bound (target, not yet measured here): per AC-CORE-018 / CORE-008,
  99th-percentile JS-GC-attributable frame work stays below 2 ms over
  a 60 s scroll trace on the reference feed app (API 33+ device), and
  no frame over the 16.6 ms budget is attributable to a single JS GC
  pause.
- Status: TARGET. The `benchmark/js-gc` harness (`benchmark/js-gc/run.py`,
  `measure_js_gc`) has not produced a measured datum in this checkout,
  so the 2 ms p99 figure is recorded as a target only — it is not
  asserted as a measured result. The harness replays the bound check
  against seeded values so the CORE-008 reproduction is structural.

## driver bound

- Bound: TBD. No measured per-frame Vulkan driver-time bound has been
  recorded in this checkout.
- Status: explicit TBD — pending a device-lab trace (FrameTimeline /
  Perfetto) under the reproducible methodology of NFR-PERFORMANCE-007.
  No numeric driver bound is asserted here.

## os bound

- Bound: TBD. No measured OS-scheduling-jitter bound (e.g. VSYNC
  wake-up delay, thread-preemption contribution to frame time) has
  been recorded in this checkout.
- Status: explicit TBD — pending the same device-lab protocol as the
  driver bound above. Nothing here asserts an OS scheduling bound of
  nil.

## no-absolute-claim

This document makes no process-wide zero-GC claim. The render-path
GC-free statement above applies to the C++ hot path only; the JS
isolate still collects, and driver/OS contributions remain TBD targets
until measured.
