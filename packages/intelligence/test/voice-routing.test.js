'use strict';
// T4 — transcribe→reason pipeline, denied/no-speech named states (Batch 2).
// Plan Interface (verbatim):
// `startTurn(control, audioChunk): void` → emits `transcribe` SSE partials then
// final, then `reason` deltas carrying the same `turnId`; exposes
// `{ route:'transcribe→reason', latencyMs:number, reason:string }`; mic denial →
// status `denied`, silence timeout → `no-speech`.
// Spec refs: FR-MUSE-008 (AC-MUSE-024), FR-MUSE-002 (required dependency).
// Hermetic: node:test + node:assert/strict only. fetch/audio/clock are injected
// doubles; no network, no secrets, no config, no live services. Builds on the
// T1 `createTurn` surface (imported from ../lib/turn.js) and the T3
// `createMuseVoice` factory; this file never re-stubs their behavior.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createTurn } = require('../lib/turn.js');
const { createMuseVoice, startTurn } = require('../lib/voice.js');

function stubTarget(value = '') {
  return { value, caret: 0, selection: [0, 0], focus: false };
}

function stubDeps(overrides = {}) {
  const calls = [];
  const sseEvents = [];
  return {
    calls,
    sseEvents,
    fetchImpl: async (url, opts) => {
      calls.push({ url, opts });
      if (String(url).startsWith('upstream:')) throw new Error('must not call upstream directly');
      return { ok: true, json: async () => ({}) };
    },
    audio: {
      denied: false,
      silent: false,
      partials: ['hel', 'hello'],
      finalTranscript: 'hello world',
      reasonDeltas: ['Hel', 'Hello, world.'],
      ...overrides.audio,
    },
    clock: { now: 1000, ...overrides.clock },
  };
}

function makeControl(target = stubTarget(), audio) {
  const [control] = createMuseVoice({
    icon: 'mic',
    activeIcon: 'graphic_eq',
    iconVariant: 'outlined',
    activeVariant: 'filled',
    target,
    icons: new Set(['mic', 'graphic_eq', 'check']),
  });
  void audio;
  return control;
}

describe('T4 — transcribe then reason with one turnId (AC-MUSE-024)', () => {
  it('emits transcribe SSE partials then final, then reason deltas with the same turnId', () => {
    const deps = stubDeps();
    const control = makeControl();
    const turn = createTurn();
    const seen = startTurn(control, new Uint8Array([1, 2, 3]), { ...deps, turn });
    assert.ok(Array.isArray(seen.partials));
    assert.deepEqual(seen.partials, ['hel', 'hello']);
    assert.equal(seen.final, 'hello world');
    assert.ok(Array.isArray(seen.reasonDeltas));
    const ids = new Set([...seen.partialTurnIds, seen.finalTurnId, ...seen.reasonTurnIds]);
    assert.equal(ids.size, 1);
    assert.equal([...ids][0], turn.id);
  });

  it("exposes { route:'transcribe→reason', latencyMs:number, reason:string }", () => {
    const deps = stubDeps();
    const control = makeControl();
    const turn = createTurn();
    const seen = startTurn(control, new Uint8Array([9]), { ...deps, turn });
    assert.equal(seen.route, 'transcribe→reason');
    assert.equal(typeof seen.latencyMs, 'number');
    assert.ok(seen.latencyMs >= 0);
    assert.equal(typeof seen.reason, 'string');
    assert.ok(seen.reason.length > 0);
  });

  it('sends the transcript with the prompt to the reason route', () => {
    const deps = stubDeps();
    const control = makeControl();
    const turn = createTurn();
    const seen = startTurn(control, new Uint8Array([7]), {
      ...deps,
      turn,
      prompt: 'Summarize for a clinician.',
    });
    const reasonCall = deps.calls.find((c) => String(c.url).includes('reason'));
    assert.ok(reasonCall);
    assert.ok(String(JSON.stringify(reasonCall)).includes('hello world'));
    assert.ok(String(JSON.stringify(reasonCall)).includes('Summarize for a clinician.'));
    void seen;
  });

  it('privacy auto uses Muse transcribe then reason only (no non-Muse route)', () => {
    const deps = stubDeps();
    const control = makeControl();
    const turn = createTurn();
    startTurn(control, new Uint8Array([5]), { ...deps, turn, privacy: 'auto' });
    const routes = deps.calls.map((c) => String(c.url));
    assert.ok(routes.some((u) => u.includes('transcribe')));
    assert.ok(routes.some((u) => u.includes('reason')));
    assert.ok(routes.every((u) => !u.startsWith('upstream:')));
  });
});

describe('T4 — denied / no-speech named states (AC-MUSE-024)', () => {
  it('mic denial moves status to denied with { code: denied }', () => {
    const deps = stubDeps({ audio: { denied: true } });
    const control = makeControl();
    const turn = createTurn();
    const seen = startTurn(control, new Uint8Array([1]), { ...deps, turn });
    assert.equal(seen.status, 'denied');
    assert.equal(seen.error.code, 'denied');
  });

  it('silence timeout moves status to no-speech with { code: no-speech }', () => {
    const deps = stubDeps({ audio: { silent: true } });
    const control = makeControl();
    const turn = createTurn();
    const seen = startTurn(control, new Uint8Array([1]), { ...deps, turn });
    assert.equal(seen.status, 'no-speech');
    assert.equal(seen.error.code, 'no-speech');
  });
});

describe('T4 — errors surface only the proxy taxonomy (RULE-MUSE-007)', () => {
  it('upstream failure records one of auth/upstream/timeout/schema with its route', () => {
    const deps = stubDeps();
    deps.fetchImpl = (url, opts) => {
      deps.calls.push({ url, opts });
      const err = new Error('upstream boom');
      err.code = 'upstream';
      err.route = 'transcribe';
      throw err;
    };
    const control = makeControl();
    const turn = createTurn();
    const seen = startTurn(control, new Uint8Array([1]), { ...deps, turn });
    assert.equal(seen.status, 'error');
    assert.ok(['auth', 'upstream', 'timeout', 'schema'].includes(seen.error.code));
    assert.equal(typeof seen.error.route, 'string');
    assert.ok(!('body' in seen.error) && !('stack' in seen.error));
  });
});
