# Performance raw datasets

Seed directory for raw performance-claim datasets (NFR-PERFORMANCE-007,
AC-015). No device data has been published yet — this README plus
`schema.json` define the layout that future claim datasets MUST follow.

## Layout

One directory per claim under this folder:

```text
benchmark/datasets/<claim-id>/
  results.json      # measured values per area (gate input)
  traces/           # Perfetto traces incl. FrameTimeline data
  harness.log       # exact harness commands + output
  metadata.json     # claim attribution fields, validated by schema.json
```

`metadata.json` MUST validate against `benchmark/datasets/schema.json`.

## Exclusion flags

Per `benchmark/METHOD.md` (Exclusion / outlier rule), excluded runs,
frames, or samples are retained with exclusion flags — never silently
dropped. `metadata.json` records per-sample `excluded` +
`exclusion_reason` fields as modeled in `schema.json`.
