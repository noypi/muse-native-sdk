// Batch 1 T1 RED pin — recorder verbs -> retained descriptors, unknown fails closed named.
// Spec: AC-CANVAS-001 (FR-CANVAS-001). Interface (plan verbatim):
//   record(verb: string, payload: unknown): Descriptor | never;
//   unknown -> throw CanvasUnknownKindError(kind: string)
// STOP: verbs map 1:1 onto EXISTING PrimitiveKinds only (Rect, RoundedRect,
// Image, Text, StateLayer, Ripple, ElevationShadow, CircleArc, VectorIcon,
// ShapedText, OverlayScrim — engine/runtime/include/runtime/renderer.h); no invented kinds.
// Hermetic: node:test + node:assert/strict only. No device, no network.
// RED by construction: ../src/recorder.ts does not exist (Batch 3 owns src/).

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';

// @ts-expect-error RED: implementation lands in Batch 3 (T6)
import { record, CanvasUnknownKindError } from '../src/recorder.ts';

describe('T1 recorder verbs map onto existing PrimitiveKinds', () => {
  it('fillRect records a retained Rect descriptor', () => {
    const d = record('fillRect', { x: 1, y: 2, w: 3, h: 4 });
    assert.equal(d.kind, 'Rect');
  });

  it('drawImage records a retained Image descriptor', () => {
    const d = record('drawImage', { imageId: 7, x: 0, y: 0 });
    assert.equal(d.kind, 'Image');
  });

  it('arc records a retained CircleArc descriptor', () => {
    const d = record('arc', { x: 0, y: 0, radius: 5, startDeg: 0, sweepDeg: 90 });
    assert.equal(d.kind, 'CircleArc');
  });

  it('fillText records a retained ShapedText descriptor', () => {
    const d = record('fillText', { text: 'hi', runId: 9 });
    assert.equal(d.kind, 'ShapedText');
  });

  it('save/restore record retained descriptors and paint nothing during recording', () => {
    const s = record('save', {});
    const r = record('restore', {});
    assert.ok(s && r, 'save/restore must record retained descriptors');
    assert.equal(s.paints, 0);
    assert.equal(r.paints, 0);
  });

  it('unknown verb fails closed naming the kind', () => {
    assert.throws(() => record('blurHash', {}), (err) => {
      assert.ok(err instanceof CanvasUnknownKindError);
      assert.match(String(err.kind ?? err.message), /blurHash/);
      return true;
    });
  });
});
