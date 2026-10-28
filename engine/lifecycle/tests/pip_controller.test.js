'use strict';
// T4 — PiP controller (FR-LIFECYCLE-008, AC-LIFECYCLE-020).
// Plan Interface (verbatim):
// `createPipController(hooks: { onPlayback(mode: string): void }): { enter(params: { aspectRatio: number, sourceRect: object, actions: object[] }): { entered: boolean }, dispatchAction(actionId: string): { dispatched: boolean }, exit(): { restored: boolean }, getState(): string }` — errors: `PIP_BAD_ASPECT` thrown for non-positive aspectRatio; `PIP_UNKNOWN_ACTION` as { dispatched: false }.
// Spec refs: FR-LIFECYCLE-008, AC-LIFECYCLE-020 (required deps: FR-LIFECYCLE-001, RULE-LIFECYCLE-003).
// Hermetic: node:test + node:assert/strict only; playback collaborator is a hand-written
// in-memory recording hook injected through the public boundary; no network/fs/secrets/
// live services.
// EXTERNAL GAP (non-executable in-repo): `device-lab/picture-in-picture` API-level matrix
// (auto-enter/stashing/policy differences across API levels, real OS PiP) requires a real
// device/lab; this file mirrors state transitions, params, actions, and playback continuity
// only and makes no device-verified claims (plan A2).

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const { createPipController } = require('../src/pip_controller.js');

function createRecordingHooks() {
  const modes = [];
  return {
    modes,
    hooks: { onPlayback(mode) { modes.push(mode); } },
  };
}

function validParams() {
  return {
    aspectRatio: 16 / 9,
    sourceRect: { x: 0, y: 0, w: 320, h: 180 },
    actions: [{ id: 'play' }, { id: 'pause' }],
  };
}

describe('AC-LIFECYCLE-020 — enter with aspect/source-rect/actions', () => {
  let record;
  let pip;
  beforeEach(() => {
    record = createRecordingHooks();
    pip = createPipController(record.hooks);
  });

  it('starts in fullscreen state', () => {
    assert.equal(pip.getState(), 'fullscreen');
  });

  it('enter with 16/9 + sourceRect + 2 actions enters pip', () => {
    const r = pip.enter(validParams());
    assert.deepEqual(r, { entered: true });
    assert.equal(pip.getState(), 'pip');
  });

  it('known actions dispatch true', () => {
    pip.enter(validParams());
    assert.deepEqual(pip.dispatchAction('play'), { dispatched: true });
    assert.deepEqual(pip.dispatchAction('pause'), { dispatched: true });
  });

  it('unknown action returns { dispatched: false } (PIP_UNKNOWN_ACTION), never throws', () => {
    pip.enter(validParams());
    assert.deepEqual(pip.dispatchAction('nope'), { dispatched: false });
  });

  it('dispatch before enter returns { dispatched: false }, never throws', () => {
    assert.deepEqual(pip.dispatchAction('play'), { dispatched: false });
  });
});

describe('AC-LIFECYCLE-020 — aspect validation (PIP_BAD_ASPECT)', () => {
  it('zero aspectRatio throws PIP_BAD_ASPECT', () => {
    const record = createRecordingHooks();
    const pip = createPipController(record.hooks);
    assert.throws(() => pip.enter({ ...validParams(), aspectRatio: 0 }), (err) => err && err.code === 'PIP_BAD_ASPECT');
  });

  it('negative aspectRatio throws PIP_BAD_ASPECT', () => {
    const record = createRecordingHooks();
    const pip = createPipController(record.hooks);
    assert.throws(() => pip.enter({ ...validParams(), aspectRatio: -1 }), (err) => err && err.code === 'PIP_BAD_ASPECT');
  });

  it('failed enter leaves state at fullscreen', () => {
    const record = createRecordingHooks();
    const pip = createPipController(record.hooks);
    assert.throws(() => pip.enter({ ...validParams(), aspectRatio: 0 }), (err) => err && err.code === 'PIP_BAD_ASPECT');
    assert.equal(pip.getState(), 'fullscreen');
  });
});

describe('AC-LIFECYCLE-020 — state transitions + playback continuity (RULE-LIFECYCLE-003)', () => {
  it('exit restores fullscreen', () => {
    const record = createRecordingHooks();
    const pip = createPipController(record.hooks);
    pip.enter(validParams());
    const r = pip.exit();
    assert.deepEqual(r, { restored: true });
    assert.equal(pip.getState(), 'fullscreen');
  });

  it('illegal exit from fullscreen is a no-op staying fullscreen', () => {
    const record = createRecordingHooks();
    const pip = createPipController(record.hooks);
    pip.exit();
    assert.equal(pip.getState(), 'fullscreen');
  });

  it('playback hook stays invoked across enter/pip/exit (continuity)', () => {
    const record = createRecordingHooks();
    const pip = createPipController(record.hooks);
    const before = record.modes.length;
    pip.enter(validParams());
    pip.dispatchAction('play');
    pip.exit();
    assert.ok(record.modes.length > before, 'onPlayback must fire across PiP transitions');
  });

  it('actions do not dispatch after exit', () => {
    const record = createRecordingHooks();
    const pip = createPipController(record.hooks);
    pip.enter(validParams());
    pip.exit();
    assert.deepEqual(pip.dispatchAction('play'), { dispatched: false });
  });
});
