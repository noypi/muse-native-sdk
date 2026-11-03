// Batch 1 T2 RED pin — deterministic time injection.
// Spec: AC-CANVAS-002 (FR-CANVAS-001). Interface (plan verbatim):
//   tick(state: S, t: number, dt: number): DrawCmd[];
//   same(state,t) -> identical bytes
// Detail: no wall-clock reads; seeded randomness only in golden scenes.
// Hermetic: node:test + node:assert/strict only. No device, no network.
// RED by construction: ../src/clock.ts does not exist (Batch 3 owns src/).

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';

// @ts-expect-error RED: implementation lands in Batch 3 (T6)
import { tick } from '../src/clock.ts';

describe('T2 deterministic time injection', () => {
  it('same (state,t) yields identical commands', () => {
    const state = { x: 1 };
    const a = tick(state, 100, 16);
    const b = tick(state, 100, 16);
    assert.deepEqual(a, b);
    assert.equal(JSON.stringify(a), JSON.stringify(b));
  });

  it('different t may advance the scene but stays deterministic', () => {
    const state = { x: 1 };
    const a = tick(state, 100, 16);
    const b = tick(state, 116, 16);
    assert.deepEqual(tick(state, 116, 16), b);
    assert.ok(Array.isArray(a) && Array.isArray(b));
  });

  it('reads no wall clock (Date.now frozen or absent still replays)', () => {
    const realNow = Date.now;
    try {
      // @ts-expect-error test forces a fixed clock
      Date.now = () => 0;
      const state = { x: 2 };
      assert.deepEqual(tick(state, 50, 16), tick(state, 50, 16));
    } finally {
      Date.now = realNow;
    }
  });
});
