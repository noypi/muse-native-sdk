# Two-Surface Proof — FR-RUNTIME-011 / AC-RUNTIME-035 / TC-RUNTIME-035 (TC-035)

> **STATUS: NOT YET EXECUTED.** No device runs were performed from this environment.
> All verdicts in the companion evidence JSON are `false` / pending placeholders.
> An operator must execute this procedure verbatim on real hardware and then flip
> each verdict to `true` only on observed passing evidence. Do NOT mark this proof
> complete until both surfaces record all six verdicts `true`, screenshot paths are
> filled, and the T3 gate exit is `0` per surface.

- Spec: `manual-context.local/specs-engine-runtime.md` — FR-RUNTIME-011, AC-RUNTIME-035, TC-RUNTIME-035 (Method: manual).
- Plan: `workflow.local/runs/fr-runtime-011/plan.md` item T5 (depends on T3, T4).
- Contract: `workflow.local/runs/fr-runtime-011/unit-tests/T5.md`.
- Evidence record: `examples/m3-gallery/docs/two-surface-evidence.json` (template/skeleton — NOT YET EXECUTED).
- Interface: none — manual device procedure + evidence record; no callable code.
- T4 build under proof: the gallery reference client conformed to the T1 Surface seam (Batch 3, T4 staged).
- T3 gate: `muse-native gallery-real --assert-surface-contract [--device <serial>]` → exit 0 per surface.

## TC-RUNTIME-035 procedure — verbatim from spec

> 1. Install the contract build on the pinned emulator and on a physical device. 2. Page the full gallery on each with a screenshot per page. 3. Confirm per-page counts, no blank viewports, vector text with atlas reuse, tap/repaint response, and layering z-order on both surfaces. 4. Confirm frame stats with zero FATAL on both.

## Prerequisites

1. Use the T4 contract build of `examples/m3-gallery` (gallery calling the T1 seam exclusively — no fallback-to-static-root).
2. Have the pinned emulator image booted and one physical device attached (`adb devices` shows both serials).
3. Have the T3 gate available: `muse-native gallery-real --assert-surface-contract`.
4. Clear logcat on each surface before the run so the zero-FATAL check is scoped to this proof.

## Procedure (repeat once per surface: `pinned-emulator`, then `physical-device`)

### Step 1 — Install the contract build on both surfaces

1. Install the same T4 contract build APK on the pinned emulator.
2. Install the same T4 contract build APK on the physical device.
3. Launch the gallery on each surface and confirm it reaches the first page without a crash.

### Step 2 — Page the full gallery with one screenshot per page per surface

1. Starting from the first gallery page, page forward through the **full** gallery (every page, Prev/Next in order, no skips) on surface A.
2. Capture **one screenshot per page** on surface A; save under a per-surface directory, e.g.
   `examples/m3-gallery/docs/screenshots/pinned-emulator/page-<NN>.png`.
3. Repeat steps 1–2 on surface B (e.g. `.../screenshots/physical-device/page-<NN>.png`).
4. Record every screenshot path in the evidence JSON `screenshots` array for that surface, in page order. A missing screenshot for any page → that surface's record is incomplete (verdict stays `false`).

### Step 3 — Confirm the five render/interaction verdicts on both surfaces

For **each** surface, using the screenshots plus on-device observation:

1. **counts** — Per-page node counts match the gallery manifest on every page. Any page whose count mismatches → `counts: false` for that surface.
2. **noBlank** — Zero blank viewports: every screenshot shows rendered content (pixel-variance check — no fully blank / black / white viewport). Any blank viewport → `noBlank: false`.
3. **vectorTextAtlas** — Text is vector-rendered with glyph-atlas reuse (atlas reuse counter > 0 observed, no rasterized-text fallback). No reuse or bitmap-text fallback → `vectorTextAtlas: false`.
4. **tapRepaint** — Tap advances the page with a repaint within ≤ 2 frames (tap→repaint response observed). Stuck page or late repaint → `tapRepaint: false`.
5. **layering** — Layering z-order matches the contract expectation on every page (scrims/overlays/elevation order correct). Any inversion → `layering: false`.

### Step 4 — Confirm frame stats with zero FATAL on both surfaces

For **each** surface:

1. Read frame stats (`getFrameStats`: presented / dropped / deadlineMissed) — confirm presents are non-zero and counters increment across the paging run.
2. Scan logcat for the run window: **zero `FATAL`** entries. Any FATAL → `frameStatsZeroFatal: false` for that surface.
3. Run the T3 gate against that surface and record its exit code:
   `muse-native gallery-real --assert-surface-contract [--device <serial>]`
   Expected: exit `0` with per-step PASS lines. A non-zero exit → record the actual code in `t3GateExit`; the proof is incomplete until it is `0`.
4. Set `frameStatsZeroFatal: true` only when frame stats are confirmed **and** logcat shows zero FATAL.

## Pass criteria (proof complete only when ALL hold)

- Both surfaces (`pinned-emulator`, `physical-device`) present in the evidence JSON.
- All six verdicts `true` per surface: `counts`, `noBlank`, `vectorTextAtlas`, `tapRepaint`, `layering`, `frameStatsZeroFatal`.
- `screenshots` non-empty with one path per gallery page, in order, per surface.
- `t3GateExit: 0` recorded per surface.
- A single-surface failure must not mask the other: per-surface records are independent — re-run only the failing surface.

## How to record results

1. Execute the procedure above on real hardware.
2. Edit `examples/m3-gallery/docs/two-surface-evidence.json`: fill `screenshots`, flip each observed-passing verdict to `true`, set `t3GateExit` to the observed exit code, and flip top-level `status` from `NOT_YET_EXECUTED` to `COMPLETE` (only when every criterion above holds) or `PARTIAL` (any verdict still `false`).
3. Do not fabricate passing verdicts: every `true` must trace to a screenshot, a stats readout, a logcat scan, or a T3 gate run listed in this doc.
