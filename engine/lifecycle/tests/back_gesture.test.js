'use strict';
// T3 — back-gesture end-to-end sync incl. root back-to-home (FR-LIFECYCLE-007, AC-LIFECYCLE-019).
// Plan Interface (verbatim):
// `createBackGesture(dispatcher, progress): { swipe(destinationId: string, steps: number[]): { offScreen: boolean, frames: number }, pressBackFromRoot(): { wentHome: boolean } }` — errors: none; unconsumed yields { offScreen: false } / { wentHome: true }.
// Spec refs: FR-LIFECYCLE-007, AC-LIFECYCLE-019 (required dep: FR-INPUT-006 contract —
// input side produces the gesture progress stream consumed here; contract-only, not implemented).
// Hermetic: node:test + node:assert/strict only; dispatcher/progress collaborators are
// hand-written in-memory fakes injected through the public boundary matching the T1/T2
// surfaces; no network/fs/secrets/live services; no async hops.
// EXTERNAL GAP (non-executable in-repo): `device-lab/predictive-back` (TC-019) requires a
// real device/lab (Compose/AndroidX host, gesture sync, <1ms on-device latency); this file
// is the hermetic mirror only and makes no device-verified claims (plan A2).

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const { createBackGesture } = require('../src/back_gesture.js');

// Fake T1 dispatcher surface: { handleBack(eventId) }.
function createFakeDispatcher({ consumed = true, wentHome = false } = {}) {
  const events = [];
  return {
    events,
    handleBack(eventId) {
      events.push(eventId);
      return { consumed, destinationId: consumed ? String(eventId) : null, wentHome };
    },
  };
}

// Fake T2 progress surface: { onProgress(value, claimant) }.
function createFakeProgress({ applied = true } = {}) {
  const values = [];
  return {
    values,
    onProgress(value, claimant) {
      values.push([value, claimant]);
      return { applied };
    },
  };
}

function tenSteps() {
  return [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1];
}

describe('AC-LIFECYCLE-019 — swipe drives transition frames 1:1 with gesture steps', () => {
  let dispatcher;
  let progress;
  let gesture;
  beforeEach(() => {
    dispatcher = createFakeDispatcher({ consumed: true, wentHome: false });
    progress = createFakeProgress({ applied: true });
    gesture = createBackGesture(dispatcher, progress);
  });

  it('10-step swipe yields 10 applied frames ending offScreen true', () => {
    const steps = tenSteps();
    const r = gesture.swipe('A', steps);
    assert.deepEqual(r, { offScreen: true, frames: 10 });
  });

  it('each swipe step maps 1:1 to a progress frame in order (no coalescing/skipping)', () => {
    const steps = tenSteps();
    gesture.swipe('A', steps);
    assert.deepEqual(progress.values.map(([v]) => v), steps);
    assert.equal(progress.values.length, steps.length);
  });

  it('swipe dispatches completion through the T1 dispatcher surface', () => {
    gesture.swipe('B', tenSteps());
    assert.ok(dispatcher.events.length >= 1, 'swipe must consult the dispatcher');
  });
});

describe('AC-LIFECYCLE-019 — unconsumed / host-claimed gesture never double-consumed', () => {
  it('unconsumed progress yields { offScreen: false } and never throws', () => {
    const dispatcher = createFakeDispatcher({ consumed: false, wentHome: true });
    const progress = createFakeProgress({ applied: false });
    const gesture = createBackGesture(dispatcher, progress);
    const r = gesture.swipe('A', tenSteps());
    assert.equal(r.offScreen, false);
  });

  it('interleaved host-claimed gesture reports one applied:false side (no double consume)', () => {
    const dispatcher = createFakeDispatcher({ consumed: true, wentHome: false });
    let call = 0;
    const progress = {
      values: [],
      onProgress(value, claimant) {
        call += 1;
        this.values.push([value, claimant]);
        // First frame consumed by host side, rest rejected: exactly one consumer wins.
        return { applied: call === 1 };
      },
    };
    const gesture = createBackGesture(dispatcher, progress);
    const r = gesture.swipe('A', [0.25, 0.5, 0.75, 1]);
    assert.equal(r.offScreen, false);
    assert.ok(r.frames < 4, `partially applied frames expected, got ${r.frames}`);
  });

  it('empty steps never throws and yields { offScreen: false, frames: 0 }', () => {
    const dispatcher = createFakeDispatcher({ consumed: true, wentHome: false });
    const progress = createFakeProgress({ applied: true });
    const gesture = createBackGesture(dispatcher, progress);
    const r = gesture.swipe('A', []);
    assert.deepEqual(r, { offScreen: false, frames: 0 });
  });
});

describe('AC-LIFECYCLE-019 — back from root goes home', () => {
  it('pressBackFromRoot with empty stack yields { wentHome: true }', () => {
    const dispatcher = createFakeDispatcher({ consumed: false, wentHome: true });
    const progress = createFakeProgress({ applied: true });
    const gesture = createBackGesture(dispatcher, progress);
    const r = gesture.pressBackFromRoot();
    assert.deepEqual(r, { wentHome: true });
  });

  it('pressBackFromRoot consults the dispatcher and never throws', () => {
    const dispatcher = createFakeDispatcher({ consumed: false, wentHome: true });
    const progress = createFakeProgress({ applied: true });
    const gesture = createBackGesture(dispatcher, progress);
    const r = gesture.pressBackFromRoot();
    assert.equal(r.wentHome, true);
    assert.ok(dispatcher.events.length >= 1, 'root back must consult the dispatcher');
  });
});
