# Performance budgets (NFR-PERFORMANCE-003)

Versioned per-subsystem budget manifest consumed by the CI perf gate
(`benchmark/gate`, TEST-PERF-GATE) and by the measurement harnesses.
Numbers are taken verbatim from `manual-context.local/specs-performance.md`
Targets/Footprint and AC-006–011 (plan A4); no new thresholds invented,
except where the spec only requires a tracked budget without a number
(warm/hot start splits, battery drain) — those rows are marked tracked.

## Files

- `budgets.json` — the manifest (single edit point for thresholds).
- `budgets.py` — loader: `load_budgets(path) -> dict`,
  `check_coverage(manifest) -> list[str]`, `BudgetError(area, reason)`.

## Schema

```json
{"version": 1,
 "reference_apps": ["minimal", "m3-gallery"],
 "budgets": [{"area": "startup", "subsystem": "lifecycle",
               "threshold": {"cold_ms": 80}, "unit": "ms",
               "reference_app": "minimal"}]}
```

- `reference_apps` is extensible (plan A1): `["minimal"]` plus every app
  under `examples/` (currently `m3-gallery`). Coverage is always computed
  against the manifest's own list, so adding an app means adding one row
  per area for it — no schema change.
- The nine areas (`startup`, `first_frame`, `frame`, `jank_rate`,
  `memory`, `size`, `battery`, `network`, `thermal`) map to
  NFR-PERFORMANCE-003's "startup, first frame, frame time p50/p99, jank
  rate, memory, APK/install size, battery, network and thermal".
  "Frame time p50/p99" is one area (`frame`); battery/network/thermal
  are three.
- `threshold` may be a scalar or an object of named bounds; spot-checks
  read every numeric value in the row. Every row must name an owner
  `subsystem` and carry a `threshold`, or the loader raises `BudgetError`.

## Budgets by area

| Area | Owner | Threshold (verbatim spec) |
|------|-------|---------------------------|
| `startup` | lifecycle | cold first frame 50–80 ms; warm/hot tracked separately |
| `first_frame` | lifecycle | first frame within the 80 ms cold envelope |
| `frame` | graphics | typical UI <4 ms, complex <8 ms, p99 within refresh (16 ms); touch 1–2 ms, keyboard <16 ms, back <1 ms, scroll >120 FPS |
| `jank_rate` | graphics | per-app jank-rate target (tracked) |
| `memory` | core | idle RSS 25–35 MB (engine 12–15 MB), VRAM within adaptive profile (≤128 MB), no soak growth |
| `size` | build | base APK 12–18 MB, ICU subset ~3 MB, modules 15–25 MB, installed 35–50 MB |
| `battery` | power | per-app drain budget (tracked) |
| `network` | net | HTTP dispatch <1 ms JS-to-native |
| `thermal` | power | sustained frame p99 held under throttling |

## Usage

```python
from benchmark.budgets import BudgetError, check_coverage, load_budgets

manifest = load_budgets("benchmark/budgets.json")  # raises BudgetError on gap
gaps = check_coverage(manifest)  # [] when every (area x app) is covered
```
