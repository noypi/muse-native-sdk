// Batch 2 T5 RED pin — composed golden frame at fixed t + per-element mapping.
// Spec: AC-CANVAS-005 (FR-CANVAS-001). Interface (plan verbatim):
//   emitFrame(scene: Descriptor[], t: number): DrawCmd[];
//   golden == snapshot(t=fixed)
// Detail: one composed frame pinned at fixed t (representative verbs across
// elements); per-element descriptor->DrawCmd incl. unknown-named.
// STOP: per-element mapping onto EXISTING PrimitiveKinds only (Rect, Image,
// CircleArc, ShapedText, StateLayer — engine/runtime/include/runtime/renderer.h);
// no invented kinds; no engine/graphics/sample edits.
// Hermetic: node:test + node:assert/strict only. No device, no network.
// RECONCILED: Batch 3 T6 landed the Real in ../src/canvas (plain TS, no JSX).

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';

// @ts-expect-error RED: implementation lands in Batch 3 (T6)
import { emitFrame, CanvasUnknownKindError } from '../src/canvas.ts';

// Fixed tick for the golden frame. Never Date.now(), never Math.random().
const T_FIXED = 1000;

// Deterministic scene: representative verbs across elements, plain descriptors
// (literal objects so this pin does not depend on the recorder under test in T1).
function goldenScene() {
  return [
    { verb: 'fillRect', kind: 'Rect', x: 0, y: 0, w: 320, h: 240 },
    { verb: 'save', kind: 'StateLayer', op: 'save' },
    { verb: 'translate', kind: 'StateLayer', op: 'translate', x: 10, y: 20 },
    { verb: 'drawImage', kind: 'Image', imageId: 7, x: 10, y: 20 },
    { verb: 'arc', kind: 'CircleArc', x: 40, y: 40, radius: 5, startDeg: 0, sweepDeg: 90 },
    { verb: 'fillText', kind: 'ShapedText', text: 'hi', runId: 9 },
    { verb: 'restore', kind: 'StateLayer', op: 'restore' },
  ];
}

// Pinned kind sequence for the composed frame at T_FIXED.
const GOLDEN_KINDS = ['Rect', 'StateLayer', 'StateLayer', 'Image', 'CircleArc', 'ShapedText', 'StateLayer'];

describe('T5 composed golden frame at fixed t', () => {
  it('emits one DrawCmd per scene element in order (kind sequence pinned)', () => {
    const cmds = emitFrame(goldenScene(), T_FIXED);
    assert.equal(cmds.length, goldenScene().length);
    assert.deepEqual(cmds.map((c) => c.kind), GOLDEN_KINDS);
  });

  it('golden frame is byte-identical across repeated emits at fixed t', () => {
    const a = emitFrame(goldenScene(), T_FIXED);
    const b = emitFrame(goldenScene(), T_FIXED);
    assert.equal(JSON.stringify(a), JSON.stringify(b));
    assert.deepEqual(a, b);
  });

  it('replays identically with the wall clock frozen (no time reads)', () => {
    const realNow = Date.now;
    try {
      // @ts-expect-error test forces a fixed clock
      Date.now = () => 0;
      const a = emitFrame(goldenScene(), T_FIXED);
      const b = emitFrame(goldenScene(), T_FIXED);
      assert.equal(JSON.stringify(a), JSON.stringify(b));
    } finally {
      Date.now = realNow;
    }
  });

  it('per-element: fillRect descriptor maps to a Rect DrawCmd', () => {
    const [cmd] = emitFrame([{ verb: 'fillRect', kind: 'Rect', x: 1, y: 2, w: 3, h: 4 }], T_FIXED);
    assert.equal(cmd.kind, 'Rect');
  });

  it('per-element: drawImage descriptor maps to an Image DrawCmd', () => {
    const [cmd] = emitFrame([{ verb: 'drawImage', kind: 'Image', imageId: 7, x: 0, y: 0 }], T_FIXED);
    assert.equal(cmd.kind, 'Image');
  });

  it('per-element: arc descriptor maps to a CircleArc DrawCmd', () => {
    const [cmd] = emitFrame(
      [{ verb: 'arc', kind: 'CircleArc', x: 0, y: 0, radius: 5, startDeg: 0, sweepDeg: 90 }],
      T_FIXED,
    );
    assert.equal(cmd.kind, 'CircleArc');
  });

  it('per-element: fillText descriptor maps to a ShapedText DrawCmd', () => {
    const [cmd] = emitFrame([{ verb: 'fillText', kind: 'ShapedText', text: 'hi', runId: 9 }], T_FIXED);
    assert.equal(cmd.kind, 'ShapedText');
  });

  it('empty scene emits zero DrawCmds (boundary)', () => {
    assert.deepEqual(emitFrame([], T_FIXED), []);
  });

  it('unknown descriptor kind fails closed naming the kind', () => {
    assert.throws(() => emitFrame([{ verb: 'blurHash', kind: 'BlurHash' }], T_FIXED), (err) => {
      assert.ok(err instanceof CanvasUnknownKindError);
      assert.match(String(err.kind ?? err.message), /BlurHash/);
      return true;
    });
  });
});
