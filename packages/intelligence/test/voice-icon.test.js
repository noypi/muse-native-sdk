'use strict';
// T3 — createMuseVoice factory + MuseVoiceControl per-state icons (Batch 2).
// Plan Interface (verbatim):
// `createMuseVoice(opts:{ icon:string, activeIcon:string, iconVariant:'filled'|'tonal'|'outlined'|'standard', activeVariant:same, confirmIcon?:string, target:WritableState, preset?:string }): [MuseVoiceControl, MuseVoiceSpeaker, voiceState]`;
// unknown icon token throws `{ code: 'unknown-icon', name }` at build/type-check.
// Spec refs: FR-MUSE-008 (AC-MUSE-023).
// Hermetic: node:test + node:assert/strict only; no network, no secrets, no
// config, no live services. Icon registry + WritableState target are injected
// doubles (see ASSUMPTIONS in unit-tests/T3.md); no real @muse/core import.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createMuseVoice } = require('../lib/voice.js');

const VARIANTS = ['filled', 'tonal', 'outlined', 'standard'];

// Injected icon registry double: the tokens the factory must accept.
// Mirrors `muse.manifest.json` registered tokens without reading the real file.
function stubIcons(extra) {
  return new Set(['mic', 'graphic_eq', 'check', ...(extra ?? [])]);
}

function stubTarget(value = '') {
  return { value, caret: 0, selection: [0, 0], focus: false };
}

function makeOpts(overrides = {}) {
  return {
    icon: 'mic',
    activeIcon: 'graphic_eq',
    iconVariant: 'outlined',
    activeVariant: 'filled',
    target: stubTarget(),
    icons: stubIcons(),
    ...overrides,
  };
}

describe('T3 — factory tuple shape (AC-MUSE-023)', () => {
  it('returns a readonly 3-tuple [Control, Speaker, voiceState]', () => {
    const tuple = createMuseVoice(makeOpts());
    assert.ok(Array.isArray(tuple));
    assert.equal(tuple.length, 3);
    assert.throws(() => { tuple.push('x'); }, /./);
    const [control, speaker, voiceState] = tuple;
    assert.equal(typeof control, 'object');
    assert.equal(typeof speaker, 'object');
    assert.equal(typeof voiceState, 'object');
  });

  it('one factory owns one shared turn: Control and Speaker see the same turn id', () => {
    const [control, speaker, voiceState] = createMuseVoice(makeOpts());
    assert.equal(typeof voiceState.turnId, 'string');
    assert.ok(voiceState.turnId.length > 0);
    assert.equal(control.turnId, voiceState.turnId);
    assert.equal(speaker.turnId, voiceState.turnId);
  });

  it('two factories are independent turns and must not share a target', () => {
    const targetA = stubTarget();
    const targetB = stubTarget();
    const [, , stateA] = createMuseVoice(makeOpts({ target: targetA }));
    const [, , stateB] = createMuseVoice(makeOpts({ target: targetB }));
    assert.notEqual(stateA.turnId, stateB.turnId);
    assert.notEqual(stateA.target, stateB.target);
  });

  it('two factories sharing one target object are rejected', () => {
    const shared = stubTarget();
    createMuseVoice(makeOpts({ target: shared }));
    assert.throws(() => createMuseVoice(makeOpts({ target: shared })), (err) => err && err.code === 'shared-target');
  });
});

describe('T3 — per-state icons (AC-MUSE-023)', () => {
  it('idle renders icon + iconVariant', () => {
    const [control] = createMuseVoice(makeOpts({ icon: 'mic', iconVariant: 'outlined' }));
    assert.equal(control.status, 'idle');
    assert.deepEqual(control.icon(), { name: 'mic', variant: 'outlined' });
  });

  it('listening renders activeIcon + activeVariant', () => {
    const [control] = createMuseVoice(makeOpts());
    control.setStatus('listening');
    assert.deepEqual(control.icon(), { name: 'graphic_eq', variant: 'filled' });
  });

  it('loading and thinking render activeIcon + activeVariant', () => {
    const [control] = createMuseVoice(makeOpts());
    for (const s of ['loading', 'thinking']) {
      control.setStatus(s);
      assert.deepEqual(control.icon(), { name: 'graphic_eq', variant: 'filled' });
    }
  });

  it('confirmIcon flashes on ready', () => {
    const [control] = createMuseVoice(makeOpts({ confirmIcon: 'check' }));
    control.setStatus('ready');
    assert.deepEqual(control.icon(), { name: 'check', variant: 'filled' });
  });

  it('ready without confirmIcon falls back to activeIcon + activeVariant', () => {
    const [control] = createMuseVoice(makeOpts());
    control.setStatus('ready');
    assert.deepEqual(control.icon(), { name: 'graphic_eq', variant: 'filled' });
  });

  it('all four IconVariant tokens are accepted for both slots', () => {
    for (const v of VARIANTS) {
      const [control] = createMuseVoice(makeOpts({ iconVariant: v, activeVariant: v }));
      assert.deepEqual(control.icon(), { name: 'mic', variant: v });
    }
  });
});

describe('T3 — unknown icon token (AC-MUSE-023)', () => {
  it("unknown icon throws { code: 'unknown-icon', name }", () => {
    assert.throws(
      () => createMuseVoice(makeOpts({ icon: '__no_such_icon__' })),
      (err) => err && err.code === 'unknown-icon' && err.name === '__no_such_icon__'
    );
  });

  it("unknown activeIcon throws { code: 'unknown-icon', name }", () => {
    assert.throws(
      () => createMuseVoice(makeOpts({ activeIcon: '__no_such_icon__' })),
      (err) => err && err.code === 'unknown-icon' && err.name === '__no_such_icon__'
    );
  });

  it("unknown confirmIcon throws { code: 'unknown-icon', name }", () => {
    assert.throws(
      () => createMuseVoice(makeOpts({ confirmIcon: '__no_such_icon__' })),
      (err) => err && err.code === 'unknown-icon' && err.name === '__no_such_icon__'
    );
  });
});

describe('T3 — preset runtime validation (Q-MUSE-003)', () => {
  it("unknown preset string rejects with { code: 'unknown-preset' }", () => {
    assert.throws(
      () => createMuseVoice(makeOpts({ preset: 'no-such-preset' })),
      (err) => err && err.code === 'unknown-preset'
    );
  });

  it('absent preset uses the route default without throwing', () => {
    const tuple = createMuseVoice(makeOpts());
    assert.equal(tuple.length, 3);
  });
});
