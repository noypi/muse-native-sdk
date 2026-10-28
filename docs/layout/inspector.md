# Layout Inspector (FR-LAYOUT-005, FR-LAYOUT-015)

Dev-only layout data feed for the dx inspector (FR-DX-003 owns the
transport/UI in `tools/inspector` — untouched by this run). This page is the
manual release procedure for TC-051 (AC-LAYOUT-051). There is no callable
surface on this page: `none — prose manual procedure, no callable surface`.

The code surface is `OverlayFrame buildOverlayFrame(RootId root) ->
{ OverlayRect[] bounds }` (each rect carries the source `GeomHandle` proving
same-object provenance with the committed renderer boxes) plus the T6
release gate (`inspectorEnabled()` / `inspectorOverheadNs()`).

## TC-051 manual release procedure (AC-LAYOUT-051)

Run on a release build on device (cannot run in CI).

### Step 1 — Build release and record the frame-time baseline

1. Build the release variant.
2. With the inspector disabled (default in release), drive the standard
   resize/scroll interaction and record the median frame time
   (`frame_time_disabled_ms`).

### Step 2 — Confirm frame time is unchanged with hooks present

1. Keep the inspector disabled (release no-op path).
2. Re-run the identical interaction and record `frame_time_hooks_ms`.
3. Pass criterion: `frame_time_hooks_ms` is within measurement noise of
   `frame_time_disabled_ms` (inspector hooks compiled out, zero overhead —
   `inspectorOverheadNs() == 0`, AC-LAYOUT-049). Attach both numbers to the
   release report.

### Step 3 — Capture the enabled node report (dev build)

1. On a dev build, solve the same scene and call `dumpLayoutTree(root)` /
   `buildOverlayFrame(root)`.
2. Record per node: resolved token values with cascade source, active
   constraint variables, and the last invalidation reason
   (`tokens`, `constraints`, `reasons` in the dump; overlay rects equal the
   renderer boxes per TC-050).
3. Attach the node report to the release report as the TC-051 evidence.
