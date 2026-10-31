# Canvas Parity vs android.graphics.Canvas (FR-PVIEWS-014)

Scope: feature `fr-pviews-002-fr-pviews-007-fr-pviews-014`, item T3 (docs-only, no callable surface).
Spec source: FR-PVIEWS-014 (AC-PVIEWS-021, TC-PVIEWS-021), RULE-PVIEWS-005; parity is a static doc table, live conformance is T5.

## 1. Path effects / boolean ops

| Operation | Status | Notes |
|-----------|--------|-------|
| drawPath (fill / stroke) | `supported` | Direct record into display list, winding fill observed |
| PathEffect: corner / discrete / dash | `approximated` | Emulation via dash-segment flattening on CPU before record |
| PathEffect: compose / sum | `approximated` | Emulation by chaining flattened effects in order |
| Path boolean ops (union / intersect / xor / difference) | `approximated` | Emulation via even-odd clip chaining instead of true geometry op |
| Path verbs (arc / quad / cubic / conic weights) | `supported` | Verbs recorded verbatim, conic weight preserved |

## 2. Gradient shaders

| Operation | Status | Notes |
|-----------|--------|-------|
| LinearGradient | `supported` | Local-matrix transform carried on the shader record |
| RadialGradient | `supported` | Center / radius / tile mode recorded verbatim |
| SweepGradient | `approximated` | Emulation via angle-quantized sweep mesh (256 slices) |
| ComposeShader | `approximated` | Emulation by baking two shaders through an offscreen layer |

## 3. Image shaders

| Operation | Status | Notes |
|-----------|--------|-------|
| BitmapShader (clamp / repeat / mirror) | `supported` | Tile modes recorded with local matrix |
| drawBitmap / drawBitmapRect | `supported` | Filter / mip-map flags carried on the record |
| drawImage / drawImageRect (9-patch stretch) | `approximated` | Emulation via center-patch tiling fallback |
| Lattice / mesh-warp draw | `unsupported` | Omission: vertex-mesh warping has no record form in this run |

## 4. PorterDuff / blend modes

| Operation | Status | Notes |
|-----------|--------|-------|
| PorterDuff SRC_OVER / SRC_IN / DST_OVER / CLEAR | `supported` | Native blend record, no fallback |
| PorterDuff MULTIPLY / SCREEN / OVERLAY | `supported` | Advanced blend recorded verbatim |
| BlendMode COLOR / HUE / SATURATION / LUMINOSITY | `approximated` | Emulation via offscreen isolation-group pass |
| Xfermode AVOID / TARGETCOLOR legacy modes | `unsupported` | Omission: deprecated tolerance-based modes not recorded |

## 5. Nested clips / save layers

| Operation | Status | Notes |
|-----------|--------|-------|
| save / restore (matrix + clip stack) | `supported` | Stack depth matches platform limit, balanced-pair checked |
| clipRect / clipPath / clipRegion | `supported` | AA flag preserved, nested intersection observed |
| clipOutRect / clipOutPath (D_O / REPLACE ops) | `supported` | Difference-clip recorded verbatim |
| saveLayer / saveLayerAlpha | `approximated` | Emulation via single offscreen buffer with alpha pre-multiply |
| Deeply nested clips beyond 32 levels | `unsupported` | Omission: levels past 32 are rejected with an error record |

## 6. Picture recording

| Operation | Status | Notes |
|-----------|--------|-------|
| beginRecording / endRecording (Picture) | `supported` | Record handle replays in draw order |
| Picture.draw into canvas | `supported` | Nested picture flattened at replay time |
| Picture serialization to stream | `unsupported` | Omission: stream persistence format out of scope for this run |

## 7. Draw-to-bitmap

| Operation | Status | Notes |
|-----------|--------|-------|
| Canvas(bitmap) construction | `supported` | Backing store dimensions bound at construction |
| draw-to-bitmap then drawBitmap replay | `supported` | Pixel-identical replay under identical flags |
| readPixels / getPixels round-trip | `approximated` | Emulation via RGBA_8888 readback with channel-swap fixup |
| Hardware-bitmap direct targeting | `unsupported` | Omission: GPU-only backing has no CPU record path here |

## 8. Text-on-path

| Operation | Status | Notes |
|-----------|--------|-------|
| drawText / drawString along baseline | `supported` | Font / size / alignment recorded verbatim |
| drawTextOnPath (hOffset / vOffset) | `approximated` | Emulation via per-glyph placement along flattened path |
| Vertical / bidi-run shaping on path | `unsupported` | Omission: complex-script shaping on path deferred to later run |

Mapped tests:

- T3 — Canvas parity table vs android.graphics.Canvas (AC-PVIEWS-021): `docs/platform-views/canvas-parity.md` (this file; TC-PVIEWS-021 file check, every op row carries exactly one status plus notes per RULE-PVIEWS-005).
- T5 — live conformance render-diff (future, not implemented in this run): mapped future test, not-built-here.
