# SVG import subset (FR-MOTION-010 / AC-MOTION-030)

Closes Q-MOTION-002 for this selection: this file is the declared subset.
Anything not listed here as supported is out of subset and must fail
loudly (query → `kUnsupported`; conversion → `kUnsupported` with a reason
naming the element). Silent drops are forbidden.

## Supported (lowercase element names)

Paths, basic shapes, grouping, and transform/opacity keyframes:

| Element          | Notes                                    |
|------------------|------------------------------------------|
| `path`           |                                          |
| `rect`           | basic shape                              |
| `circle`         | basic shape                              |
| `ellipse`        | basic shape                              |
| `line`           | basic shape                              |
| `polyline`       | basic shape                              |
| `polygon`        | basic shape                              |
| `g`              | grouping                                 |
| `animate`        | keyframe (opacity / attribute animation) |
| `animateTransform` | keyframe (transform animation)         |
| `set`            | keyframe (discrete attribute set)        |
| `svg`            | root container only (neutral, not art)   |

Name matching is exact lowercase. Prefixed spellings (`svg:path`),
uppercase variants (`Path`), and aliases are NOT supported — they report
`kUnsupported` like any unknown name.

## Excluded (→ `kUnsupported`, reason names the element)

- **Filters**: `filter` element and filter effects (`fe*`).
- **Masks / clipping**: `mask`, `clipPath`.
- **Script**: `script` — never executed, never dropped; rejected loudly.
- **Foreign content**: `foreignObject`.
- **SMIL event timing**: `onbegin` / `onend` / `onrepeat` attributes (and
  event-clock `begin` values) — excluded even when the host element is
  otherwise supported.
- **Everything else unlisted**: `text`, `image`, `use`, `style`, `defs`
  (as content), `symbol`, `marker`, `pattern`, `linearGradient`,
  `radialGradient`, unknown/future elements — all `kUnsupported`.

## Conversion rules (`motion_convertSvg`)

- Fully in-subset input → `kOk` with an empty `unsupportedReason`.
- First out-of-subset element → `kUnsupported` with
  `"unsupported SVG element: <name>"` (or the SMIL timing form naming the
  attribute and host element). The offending name is always present.
- Empty input → `kUnsupported` + non-empty reason.
- Input without an `<svg>` root (or malformed tags) → `kUnsupported` +
  non-empty reason.
- Input over 512 KiB → `kUnsupported` + non-empty reason, rejected
  without parsing (never throws, never hangs).
- Conversion never throws for any input; failures are `AssetError`
  values, never exceptions.
