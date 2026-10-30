# m3-tolerances justification (AC-GRAPHICS-030)

Tolerance file: `m3-tolerances.json` — `{"perPixel": 64, "maxFailPct": 0.12}`.
Values are frozen; do not retune them here.

## Why perPixel 64 / maxFailPct 0.12

From the T9 retained-vs-CPU diff: worst page 8.36%, with the remaining pages
falling in the lower p99 band. The observed drift comes from the retained
`DrawCmd` path (rounded-rect AA, shadow blur, arc stroking) versus the CPU
reference oracle — not from missing primitives.

- `perPixel 64`: per-pixel channel tolerance bounds the AA/blur edge drift seen
  on the worst page while still failing on real mis-renders.
- `maxFailPct 0.12`: caps the fraction of out-of-tolerance pixels at 12%,
  bounding the observed worst-page 8.36% diff with headroom for GPU/driver
  variance, without hiding a regressed page.

No spec edit, no tolerance change in this item — justification only.
