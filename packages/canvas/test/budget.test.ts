// Batch 1 T3 RED pin — declared budgets enforced, fail closed with log.
// Spec: AC-CANVAS-003 (FR-CANVAS-001). Interface (plan verbatim):
//   createCanvas(opts: {maxCmds: number; maxDraws: number; maxInstances: number; loop: boolean}): Surface
// Detail: over-budget -> error+log, never silent truncation/OOM;
// idle after first paint unless loop.
// Hermetic: node:test + node:assert/strict only. No device, no network.
// RED by construction: ../src/budget.ts does not exist (Batch 3 owns src/).

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';

// @ts-expect-error RED: implementation lands in Batch 3 (T6)
import { createCanvas, CanvasBudgetError } from '../src/budget.ts';

describe('T3 declared budgets fail closed with log', () => {
  it('over maxCmds errors named and logs (never silent truncation)', () => {
    const surface = createCanvas({ maxCmds: 1, maxDraws: 10, maxInstances: 10, loop: true });
    const logs = [];
    assert.throws(() => surface.emitFrame([{ kind: 'Rect' }, { kind: 'Rect' }], { log: logs }), (err) => {
      assert.ok(err instanceof CanvasBudgetError);
      assert.match(String(err.message), /maxCmds/);
      return true;
    });
    assert.ok(logs.length >= 1, 'over-budget frame must log');
  });

  it('over maxDraws errors named', () => {
    const surface = createCanvas({ maxCmds: 100, maxDraws: 1, maxInstances: 100, loop: true });
    assert.throws(() => surface.emitFrame([{ kind: 'Rect' }, { kind: 'Rect' }]), /maxDraws/);
  });

  it('surface goes idle after first paint unless loop', () => {
    const once = createCanvas({ maxCmds: 10, maxDraws: 10, maxInstances: 10, loop: false });
    once.emitFrame([{ kind: 'Rect' }]);
    assert.equal(once.isIdle(), true);
    const looping = createCanvas({ maxCmds: 10, maxDraws: 10, maxInstances: 10, loop: true });
    looping.emitFrame([{ kind: 'Rect' }]);
    assert.equal(looping.isIdle(), false);
  });
});
