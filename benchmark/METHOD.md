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
