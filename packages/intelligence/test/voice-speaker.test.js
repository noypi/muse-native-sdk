'use strict';
// T6 — MuseVoiceSpeaker preset + render-prop forms over all six states (Batch 2).
// Plan Interface (verbatim):
// `MuseVoiceSpeaker({ state:{ status, envelope:0..1, partial, route, latencyMs,
// reason }, waveform?:'bars'|'wave'|'dot', showPartials?:boolean,
// showBadge?:boolean, children?:(state)=>Visual })` — strictly presentational
// (mic envelope in, visuals out, never a proxy call); `children` wins when
// present (dev warning, never a device error).
// Spec refs: FR-MUSE-008 (AC-MUSE-033).
// Hermetic: node:test + node:assert/strict only. No fetch/audio/TTS/clock; a
// fetch spy that throws on any call proves the Speaker never hits the proxy.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { MuseVoiceSpeaker } = require('../lib/voice.js');

const SIX = ['idle', 'listening', 'loading', 'thinking', 'ready', 'error'];
const EXTRA = ['denied', 'no-speech'];
const ALL = [...SIX, ...EXTRA];

function baseState(status) {
  return { status, envelope: 0.5, partial: 'hel', route: 'transcribe→reason', latencyMs: 42, reason: 'dictation' };
}

function throwingFetch() {
  return async () => { throw new Error('MuseVoiceSpeaker must never call fetch'); };
}

describe('T6 — preset form covers all six states (+denied/no-speech)', () => {
  for (const status of ALL) {
    it(`preset bars renders state ${status} without throwing`, () => {
      const out = MuseVoiceSpeaker({ state: baseState(status), waveform: 'bars', fetchImpl: throwingFetch() });
      assert.equal(typeof out, 'object');
      assert.equal(out.kind, 'bars');
      assert.equal(out.state.status, status);
    });
  }

  it("waveform defaults to bars when omitted", () => {
    const out = MuseVoiceSpeaker({ state: baseState('listening') });
    assert.equal(out.kind, 'bars');
  });

  it('wave and dot waveforms render', () => {
    assert.equal(MuseVoiceSpeaker({ state: baseState('listening'), waveform: 'wave' }).kind, 'wave');
    assert.equal(MuseVoiceSpeaker({ state: baseState('listening'), waveform: 'dot' }).kind, 'dot');
  });

  it('showPartials gates the interim text; showBadge gates the route chip', () => {
    const withPartials = MuseVoiceSpeaker({ state: baseState('listening'), showPartials: true });
    assert.equal(withPartials.partial, 'hel');
    const withoutPartials = MuseVoiceSpeaker({ state: baseState('listening'), showPartials: false });
    assert.equal(withoutPartials.partial, undefined);
    const withBadge = MuseVoiceSpeaker({ state: baseState('listening'), showBadge: true });
    assert.equal(withBadge.badge, 'transcribe→reason');
    const withoutBadge = MuseVoiceSpeaker({ state: baseState('listening'), showBadge: false });
    assert.equal(withoutBadge.badge, undefined);
  });

  it('envelope outside 0..1 is rejected with a named error', () => {
    assert.throws(
      () => MuseVoiceSpeaker({ state: { ...baseState('listening'), envelope: 1.5 } }),
      (err) => err && err.code === 'bad-envelope'
    );
  });

  it('unknown waveform is rejected with a named error', () => {
    assert.throws(
      () => MuseVoiceSpeaker({ state: baseState('listening'), waveform: 'mesh' }),
      (err) => err && typeof err.code === 'string'
    );
  });
});

describe('T6 — render-prop children form (children wins)', () => {
  for (const status of ALL) {
    it(`children render-prop covers state ${status}`, () => {
      const out = MuseVoiceSpeaker({
        state: baseState(status),
        waveform: 'wave',
        children: (s) => ({ kind: 'custom-figure', amplitude: s.envelope, status: s.status }),
      });
      assert.equal(out.kind, 'custom-figure');
      assert.equal(out.status, status);
      assert.equal(out.amplitude, 0.5);
    });
  }

  it('children wins over preset props when present', () => {
    const out = MuseVoiceSpeaker({
      state: baseState('listening'),
      waveform: 'bars',
      showPartials: true,
      showBadge: true,
      children: (s) => ({ kind: 'custom-figure', status: s.status }),
    });
    assert.equal(out.kind, 'custom-figure');
  });

  it('children winning emits a dev warning, never a device error', () => {
    const warnings = [];
    const out = MuseVoiceSpeaker({
      state: baseState('ready'),
      children: (s) => ({ kind: 'custom-figure', status: s.status }),
      onWarn: (msg) => warnings.push(msg),
    });
    assert.equal(out.kind, 'custom-figure');
    assert.equal(warnings.length, 1);
    assert.equal(typeof warnings[0], 'string');
  });
});

describe('T6 — strictly presentational: never a proxy call', () => {
  it('renders every state with a throwing fetch double without calling it', () => {
    for (const status of ALL) {
      const out = MuseVoiceSpeaker({ state: baseState(status), fetchImpl: throwingFetch() });
      assert.ok(out);
    }
  });
});

describe('T6 — static frame under reduce-motion', () => {
  it('reduceMotion swaps the animated visual for a static frame', () => {
    const out = MuseVoiceSpeaker({ state: baseState('listening'), waveform: 'bars', reduceMotion: true });
    assert.equal(out.animated, false);
    assert.equal(out.kind, 'bars');
  });
});
