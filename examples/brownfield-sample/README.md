# brownfield-sample — stock Android sample hosting EngineView (T9)

Scope: FR-PVIEWS-010, AC-PVIEWS-015, TC-PVIEWS-015 (coder builds this sample;
this README is the test contract it must satisfy).

## What the sample must show

A **stock** Android app (existing Activity/Fragment structure, unmodified
except as below) hosts `EngineView` in an **existing Fragment** with a
**single dependency addition**:

```gradle
dependencies {
    implementation "com.muse.pviews:pviews:<version>"  // the one addition
}
```

- No host-app restructuring; no manifest entries beyond the engine's own
  (AC-PVIEWS-013 carries over; this sample is where it is observed).
- Shared-engine mode: two hosts share one engine process with isolated UI
  state (wires to T6 `createHost` shared mode / T7 refcount teardown).
- Multi-engine mode MAY be shown as a second screen; shared mode MUST be shown.

## Layout contract (minimum)

- `HostActivity` (existing) + `ExistingFragment` (existing, now contains
  `<com.muse.pviews.EngineView android:id="@+id/engine_view" …/>`).
- A second host (`SecondFragment` or second Activity) sharing the same engine
  instance, rendering different UI state (isolation proof).
- Engine init failure shows a host-owned fallback view (T7 contract).

## Acceptance (TC-PVIEWS-015)

1. Sample builds with one dependency addition (`gradle :brownfield-sample:assemble`).
2. Two hosts share one engine; UI state isolated per host
   (`hostsShareEngine: true`, `uiIsolated: true` in the evidence JSON).
3. Cold-start delta within the PERF-START budget
   (`coldStartDeltaMs <= budgetMs`; overrun is `COLD_START_OVER_BUDGET`).

Steps 2–3 are measured via `device-lab/host-cold-start/run.sh` (see
`device-lab/host-cold-start/README.md`); this README pins what the coder
must place here for that procedure to have something to measure.

## Red state (batch-first)

At contract time `examples/brownfield-sample/` was absent (missing files).
This README is the test file for the sample half of T9: until the coder
adds the sample, TC-PVIEWS-015 has no subject and stays RED.
