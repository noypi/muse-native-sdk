# Release size trends (TC-BUILD-074 file check)

Per-release per-artifact size accounting for NFR-BUILD-019 (AC-BUILD-074).
Artifact taxonomy follows BUILD-006 by reference: base AAB, splits, each
asset pack, each feature module. Budget numbers are owned by perf
`binary_and_footprint_estimates` (`specs-performance.md`) and are referenced
here, never redefined — this file records measured sizes; the
`toolchain/size/sizeReport.js` + `toolchain/size/sizeGate.js` gates compare
them against the referenced budgets.

> Initial release rows below mirror the T5 `recordSizes` taxonomy with
> hermetic fixture sizes (bytes). Replace `measured` values with the real
> release report output each release; keep one row per artifact per release.

| Release | Artifact | Kind | downloadBytes | installBytes | Budget ref |
|---------|----------|------|---------------|--------------|------------|
| 0.1.0 | base.aab | base AAB | 900 | 950 | perf `binary_and_footprint_estimates` |
| 0.1.0 | split-arm64-v8a.apk | split | 420 | 460 | perf `binary_and_footprint_estimates` |
| 0.1.0 | split-armeabi-v7a.apk | split | 400 | 440 | perf `binary_and_footprint_estimates` |
| 0.1.0 | asset-pack-hero | asset pack | 400 | 480 | perf `binary_and_footprint_estimates` |
| 0.1.0 | asset-pack-gallery | asset pack | 350 | 420 | perf `binary_and_footprint_estimates` |
| 0.1.0 | feature-chat | feature module | 200 | 250 | perf `binary_and_footprint_estimates` |
| 0.1.0 | feature-share | feature module | 150 | 190 | perf `binary_and_footprint_estimates` |

## Reading this file

- One row per artifact per release; append a new release block per release —
  never overwrite history.
- An artifact whose `downloadBytes` or `installBytes` exceeds its referenced
  perf budget trips the T5/T8 gates (`over: true` / `pass: false` with a
  per-artifact delta); the only accepted fix is a reviewed budget change in
  `specs-performance.md` (TC-BUILD-073 overrun policy).
