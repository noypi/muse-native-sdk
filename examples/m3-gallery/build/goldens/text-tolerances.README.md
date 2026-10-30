# text-tolerances justification (AC-TEXT-046)

Tolerance file: `text-tolerances.json` — `{"perPixel": 0, "maxFailPct": 0.0}`.
Exact pixel match, set from measured runs (not relaxed to fit).

The 12 samples (`text-{latin,cjk,emoji,small}-{1,1.3,2}.png`) are byte-stable
because the whole generation chain is deterministic: fixed stub advances from
`shapeRunWithFallback`, per-(font, scale) atlas keys, `shapeAndPlace`
placements, and `tile_raster` coverage — no wall-clock, no RNG, no device,
no fonts off-disk. Any differing byte is therefore a real engine-stack
change, so the recorded tolerance is exact match. Gate:
`python3 acceptance/text_goldens_check.py --gen <text_goldens_gen>
--goldens examples/m3-gallery/build/goldens [--scripts latin,cjk,emoji,small]
[--scales 1,1.3,2]` — exit 0 iff all selected samples match, nonzero plus a
per-sample delta report otherwise (never a silent pass).

Rendering note: each sample stamps per-glyph advance rects shaded by shaped
glyph id with tile-rasterizer-covered 8px cells overdrawn near-white. Every
bright pixel traces to an engine value (shaped id / placement / coverage);
legibility on real glyph outlines is proven on-device (T9 screenshots), not
in these host coverage maps — the stack has no host outline rasterizer (the
Slug shader runs on the device Swapchain path).

Page-level goldens keep their own `m3-tolerances.json` (perPixel 64 /
maxFailPct 0.12); no spec edit, no AC weakening in this item.
