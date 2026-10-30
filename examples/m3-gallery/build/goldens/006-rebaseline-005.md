# 006 golden re-baseline — BUG-M3-FIDELITY-005 T8 (single re-proof)

Date: 2026-10-30. Tool: `acceptance/text_goldens_gen.cpp`
(`--pages dist/tree.mui.bin`, `--shaped-text`) + `acceptance/recompress_png.py`
(lossless recompress, round-trip validated). No hand-drawn pixels: pages come
from the current `RasterGalleryPage` CPU oracle through `ParseGalleryRows`;
shaped-text comes from the production shaping pipeline
(`shapeRunWithFallback` → `GlyphAtlas` → `shapeAndPlace` → `rasterizeTiles`).

## What changed

| File | Old (006 @2a4d55e) | New (005 T8) | Delta |
|------|--------------------|--------------|-------|
| page-p0..p4.png | 720x1600 RGB, ~10–15KB, 5-page tree, bitmap captions incl. `?` degradation (`M? GALLER?`, `Te?t btn`) | 720x1600 RGB, ~11–12KB, 11-page tree (110 rows), no bitmap captions | p0: 99.98% bytes differ (3,457,027/3,457,600) |
| shaped-text.png | 200x20 RGB, 152B | 200x20 RGB, ~12KB | 99.83% bytes differ (12,000/12,020); old sha `05ccd4f02850cfc3`, new sha `c543999f8d023af0` |

Old page-p0 sha `106996f1757996d0`, new sha `d184069cbd5889a0`
(new p1 `2d7edda0e0a000f6`, p2 `8ab0bc243c96407a`, p3 `d3538d58217df0ef`,
p4 `64d7d6b5213e393a`).

## Why the delta is this large (two components, recorded not hidden)

1. **Tree evolution (dominant).** The committed 006 goldens depict a 5-page
   tree (`M? GALLER? 1/5`, footer `PAGE 1 OF 5`). The current run-branch tree
   (`dist/tree.mui.bin`, 30320B, 110 rows → 11 pages at the 720x1600 oracle
   viewport) shows different components per page. This tree difference
   predates T8 (same 110-row shape as the installed baseline APK's asset;
   the 147-row/17-page device count comes from the smaller on-device
   viewport, and the 35267B main-workspace asset noted in the 009 re-proof is
   a separate pre-existing discrepancy, not re-litigated here).
2. **Bitmap caption retirement (T7).** The old goldens' captions are 5x7
   bitmap glyphs with the T4 `?` degradation baked in. The new oracle paints
   no gallery captions (`PaintCaption` is a documented no-op; captions ride
   the retained `ShapedText` path to the device Swapchain). Vector-text proof
   moved to the 12 `text-*` samples plus T9 device screenshots.

## Untouched

`circle-arc.png`, `elevation-shadow.png`, `overlay-scrim.png`, `ripple.png`,
`statelayer.png`, `vector-icon.png`, `m3-tolerances.json` — no text
dependence, no churn (no piecemeal flips).

## Gates after re-baseline

- `muse-native gallery-diff --tolerance .../m3-tolerances.json --code-root .`
  → ok, 5 pages within tolerance.
- `text_goldens_check.py` → 12/12 exact match.
- `gallery_present_test` hermetic (invoked by gallery-diff) → pass.
