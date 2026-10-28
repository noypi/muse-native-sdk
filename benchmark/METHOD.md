# Reproducible performance-claim methodology (NFR-PERFORMANCE-001)

How to publish a performance claim so that anyone can reproduce it.
Covers AC-PERFORMANCE-001 / TC-PERFORMANCE-001.

## Reference apps

Claims are measured against the manifest's own reference-app list in
`benchmark/budgets.json` (`reference_apps`): `minimal` and `m3-gallery`.
Every claim names exactly one reference app from that list. Adding an
app to the manifest extends the set of valid claim targets; no claim
may target an app outside the manifest list.

Thresholds are read verbatim from `benchmark/budgets.json` (frame area:
typical 4 ms, complex 8 ms, p99 within 16 ms refresh). This document
invents no thresholds.

## Required attribution fields per claim

Every published claim MUST state all six fields below. A claim missing
any field is not reproducible and fails TC-PERFORMANCE-001 inspection.

| # | Field | What to record |
|---|-------|----------------|
| 1 | Reference app | One of `minimal`, `m3-gallery` (per `benchmark/budgets.json`) |
| 2 | Device | Exact device model + OS version (see Device matrix) |
| 3 | Build type | e.g. `release`, including flags that affect performance |
| 4 | Thermal state | Device thermal condition at run start (see Thermal protocol) |
| 5 | Sample size | Number of runs/frames measured (e.g. `n=200 frames x 5 runs`) |
| 6 | Raw-dataset link | URL or path to the raw dataset backing the claim |

### Claim template

```text
Claim: <what was measured, area, threshold from budgets.json>
Reference app: <minimal | m3-gallery>
Device: <model + OS version>
Build type: <release + flags>
Thermal state: <as-measured condition>
Sample size: <n runs/frames>
Raw dataset: <link to raw data>
Result: <measured value vs threshold, pass/fail>
```

## Device matrix

TBD per open question Q-PERFORMANCE-001. No device list is fixed by this
document; leave the Device field as the measured device until the matrix
is settled. Do not invent a matrix here.

## Thermal protocol

TBD per open question Q-PERFORMANCE-001. Until the soak/cool-down protocol
is settled, record the as-measured thermal state honestly (e.g. ambient,
screen-on time, charging state) instead of claiming a standard protocol.

## Exclusion / outlier rule

No silent drops. Any excluded run, frame, or sample MUST be declared with
count and reason (e.g. `excluded 3/200 frames: harness restart, listed in
raw dataset`). If nothing was excluded, state `exclusions: none`. The raw
dataset MUST retain excluded samples with their exclusion flags so the
claim can be re-checked.

Thermal drift is handled only through this rule: record the as-measured
thermal state per the Thermal protocol (TBD) for every run, flag any run
showing thermal drift in the raw dataset, and exclude a drift-affected run
only by declaring its count and reason here — never silently.

## Repeat count, confidence level, and aggregation

Every claim MUST state a minimum repeat count (e.g. `minimum 5 runs` of
`n=200 frames` each) and the stated confidence level backing the claim
(e.g. `confidence: median of 5 runs within the 5% noise band`). Fewer
repeats than the stated minimum invalidates the claim.

Aggregate repeated runs with the median plus percentile summary
(e.g. report median frame time with p50/p95/p99 percentiles), consistent
with the T2 gate's median + 5% noise band: a claim passes only when the
median across the minimum repeat count stays within the 5% noise band of
the threshold comparison.

## Raw-data publication

Every claim MUST publish its raw data alongside the results so the
claim can be re-checked without re-running the device:

- Traces: Perfetto traces including FrameTimeline data backing frame
  claims, stored with the dataset (e.g. `traces/<run-id>.perfetto-trace`).
- Harness scripts: the exact harness run scripts and commands used
  (e.g. `benchmark/frame/run.py`, `benchmark/startup/run.py`, and any
  wrapper flags), committed or copied alongside the results so the run
  is reproducible from a clone.
- Dataset layout: one directory per claim under `benchmark/datasets/`
  (e.g. `benchmark/datasets/<claim-id>/`), containing `results.json`,
  `traces/`, `harness.log`, and validated against
  `benchmark/datasets/schema.json` (see `benchmark/datasets/README.md`).
- Excluded samples: per the Exclusion / outlier rule above, excluded
  runs, frames, or samples are retained in the raw dataset with their
  exclusion flags (e.g. `excluded: true` plus `exclusion_reason`) —
  never deleted. A claim declaring `exclusions: none` ships a dataset
  with no flagged samples.

## Independent reproduction

Anyone with the listed device can independently reproduce a claim:

1. Clone the repository at the claimed commit.
2. Run the harness commands recorded with the claim on the listed
   device (same reference app, build type, and thermal state), e.g.
   `python3 benchmark/frame/run.py --app <reference-app>` and the
   other harness scripts named in the dataset.
3. Re-run the gate check with the documented CLI:
   `python3 benchmark/gate/run.py benchmark/budgets.json <results-json>`
   where `<results-json>` is the reproduced `results.json`.
4. Compare within the stated tolerance: the reproduced median must
   fall within the gate's 5% noise band of the claimed value, and
   within the per-metric tolerance stated with the claim. A
   reproduction outside that tolerance fails the claim.

Running the claim in an external lab is out of scope for this
document — only the commands and tolerance above are normative here.
