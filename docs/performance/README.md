# Performance: honest overhead bounds (NFR-PERFORMANCE-004)

Scope: this note states the bounded-overhead position required by
NFR-PERFORMANCE-004 (AC-PERFORMANCE-004 / TC-PERFORMANCE-024).
It stays inside NFR-004: it states the allocation split and reports
each overhead source below as a measured bound or as an explicit
target/TBD. The full GC/driver/OS modeling section belongs to
NFR-PERFORMANCE-010 (AC-PERFORMANCE-023-025) and is out of scope
here; nothing below contradicts it.

## 1. Allocation honesty boundary

The C++ render path is allocation-stable and GC-free after bootstrap:
steady-state frame work performs no heap allocation on the hot path
and therefore triggers no collection. The Hermes / JS isolate is
outside that path and still collects garbage normally; every
JS-authored callback, action body, and subscriber runs under the
collector and may allocate.

This matches the competitive matrix GC row (C++ render/layout hot
path GC-free via RAII; Hermes isolate still collects with bounded
pauses) and AC-CORE-017 (never claim process-wide freedom from
collection; always state the render-path / isolate split).

Wording rule: every bound below names the exact path it applies to;
a render-path bound must never be read as a process-wide claim.

## 2. JS GC pause impact — target (unmeasured in this checkout)

- Bound (target, not yet measured here): per AC-CORE-018 / CORE-008,
  99th-percentile JS-GC-attributable frame work stays below 2 ms over
  a 60 s scroll trace on the reference feed app (API 33+ device), and
  no frame over the 16.6 ms budget is attributable to a single JS GC
  pause.
- Status: TARGET / TBD. The `benchmark/js-gc` harness
  (`./gradlew :benchmark:jsGcPause`, TC-CORE-018) has not produced a
  measured datum in this checkout, so the 2 ms p99 figure is recorded
  as a target only — it is not asserted as a measured result.

## 3. Vulkan driver time — TBD (unmeasured in this checkout)

- Bound: TBD. No measured per-frame Vulkan driver-time bound has been
  recorded in this checkout.
- Status: explicit TBD — pending a device-lab trace (FrameTimeline /
  Perfetto) under the reproducible methodology of NFR-PERFORMANCE-007.
  No numeric bound is asserted here.

## 4. OS scheduling overhead — TBD (unmeasured in this checkout)

- Bound: TBD. No measured OS-scheduling-jitter bound (e.g. VSYNC
  wake-up delay, thread-preemption contribution to frame time) has
  been recorded in this checkout.
- Status: explicit TBD — pending the same device-lab protocol as
  section 3. Nothing here asserts a scheduling bound of nil.

## 5. Relation to NFR-PERFORMANCE-010

The detailed GC/driver/OS model (AC-PERFORMANCE-023-025) is owned by
NFR-PERFORMANCE-010 and is not built in this note. These bounds are
stated so that the scan over `docs/performance` finds stated bounds
(or explicit TBD markers) and no absolute process-wide claim.
