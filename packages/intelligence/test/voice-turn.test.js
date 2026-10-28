'use strict';
// T1 — Shared turn state machine with turnId/targetId contract (Batch 1).
// Plan Interface (verbatim):
// `createTurn(): { id: turnId:string, status: 'idle'|'listening'|'loading'|'thinking'|'ready'|'denied'|'no-speech'|'error'|'moderated', commit(targetId:string): boolean }`
// — `commit` returns false and drops the write when the turn id is stale
// (a newer turn committed first); error shape
// `{ code: 'stale-turn'|'denied'|'no-speech'|'auth'|'upstream'|'timeout'|'schema', route: string }`.
// Spec refs: FR-MUSE-008 (AC-MUSE-032), RULE-MUSE-007 (required).
// Hermetic: node:test + node:assert/strict only; no network, no secrets,
// no config, no live services. Time is not wall-clock dependent.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createTurn } = require('../lib/turn.js');

const STATUSES = [
  'idle', 'listening', 'loading', 'thinking', 'ready',
  'denied', 'no-speech', 'error', 'moderated',
];

describe('T1 — createTurn shape (AC-MUSE-032, RULE-MUSE-007)', () => {
  it('returns an object with a string turn id and idle initial status', () => {
    const turn = createTurn();
    assert.equal(typeof turn.id, 'string');
    assert.ok(turn.id.length > 0);
    assert.equal(turn.status, 'idle');
  });

  it('mints a unique turnId per call (client-generated, mic-tap through deltas)', () => {
    const a = createTurn();
    const b = createTurn();
    assert.notEqual(a.id, b.id);
  });

  it('exposes commit as a function returning a boolean', () => {
    const turn = createTurn();
    assert.equal(typeof turn.commit, 'function');
  });

  it('status value is always within the shared RULE-MUSE-007 set', () => {
    const turn = createTurn();
    assert.ok(STATUSES.includes(turn.status));
  });
});

describe('T1 — two rapid turns: latest committed turnId only, stale deltas dropped', () => {
  it('the latest turn commits true; the earlier turn then commits false (stale)', () => {
    const first = createTurn();
    const second = createTurn();
    assert.equal(second.commit('target-a'), true);
    assert.equal(first.commit('target-a'), false);
  });

  it('a stale commit drops the write: lastError carries the stale-turn shape', () => {
    // ASSUMPTION (see unit-tests/T1.md): the named error shape is surfaced on
    // `turn.lastError` as `{ code, route }` so the sheet can render/ignore it.
    const first = createTurn();
    const second = createTurn();
    assert.equal(second.commit('target-a'), true);
    assert.equal(first.commit('target-a'), false);
    assert.deepEqual(first.lastError, { code: 'stale-turn', route: 'transcribe' });
  });

  it('three rapid turns: only the latest commit succeeds', () => {
    const t1 = createTurn();
    const t2 = createTurn();
    const t3 = createTurn();
    assert.equal(t3.commit('target-a'), true);
    assert.equal(t2.commit('target-a'), false);
    assert.equal(t1.commit('target-a'), false);
  });
});

describe('T1 — commit to a moved-on target writes nothing', () => {
  it('committing a stale turn against a new targetId returns false', () => {
    const first = createTurn();
    const second = createTurn();
    assert.equal(second.commit('target-b'), true);
    assert.equal(first.commit('target-b'), false);
  });

  it('a turn committed to target-a cannot later write to target-b', () => {
    const turn = createTurn();
    assert.equal(turn.commit('target-a'), true);
    // Second commit from the same turn after the session moved on is stale.
    assert.equal(turn.commit('target-b'), false);
  });
});

describe('T1 — named error shape (proxy taxonomy subset)', () => {
  it('denied mic permission surfaces { code: denied, route } without throwing', () => {
    // ASSUMPTION: `turn.deny(route)` moves status to denied and records lastError.
    const turn = createTurn();
    turn.deny('transcribe');
    assert.equal(turn.status, 'denied');
    assert.deepEqual(turn.lastError, { code: 'denied', route: 'transcribe' });
  });

  it('silence timeout surfaces { code: no-speech, route } without throwing', () => {
    // ASSUMPTION: `turn.noSpeech(route)` moves status to no-speech and records lastError.
    const turn = createTurn();
    turn.noSpeech('transcribe');
    assert.equal(turn.status, 'no-speech');
    assert.deepEqual(turn.lastError, { code: 'no-speech', route: 'transcribe' });
  });

  it('upstream failure records one of the proxy taxonomy codes with its route', () => {
    // ASSUMPTION: `turn.fail(code, route)` records lastError and status error.
    const turn = createTurn();
    turn.fail('upstream', 'reason');
    assert.equal(turn.status, 'error');
    assert.deepEqual(turn.lastError, { code: 'upstream', route: 'reason' });
  });
});
