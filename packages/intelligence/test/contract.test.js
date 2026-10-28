'use strict';
// T1 — Shared Muse-component contract (RULE-MUSE-007).
// Hermetic: no network, no keys, no config reads. Imports only the unit
// under test (../lib/contract.js). Uses node:test + node:assert.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createMuseState, resolvePreset } = require('../lib/contract.js');

describe('createMuseState — initial state (RULE-MUSE-007)', () => {
  it('starts idle with null route/reason/latency/error', () => {
    const s = createMuseState();
    assert.equal(s.status, 'idle');
    assert.equal(s.route, null);
    assert.equal(s.reason, null);
    assert.equal(s.latencyMs, null);
    assert.equal(s.error, null);
  });
});

describe('createMuseState — legal transitions', () => {
  it('idle -> loading -> thinking -> ready is accepted', () => {
    const s = createMuseState();
    s.transition('loading');
    assert.equal(s.status, 'loading');
    s.transition('thinking');
    assert.equal(s.status, 'thinking');
    s.transition('ready');
    assert.equal(s.status, 'ready');
  });

  it('any state may transition to error', () => {
    for (const from of ['loading', 'thinking']) {
      const s = createMuseState();
      s.transition('loading');
      if (from === 'thinking') s.transition('thinking');
      s.transition('error');
      assert.equal(s.status, 'error');
    }
  });

  it('error carries only the proxy taxonomy (auth/upstream/timeout/schema)', () => {
    const s = createMuseState();
    s.transition('loading');
    s.transition('error');
    assert.ok(['auth', 'upstream', 'timeout', 'schema'].includes(s.error.code));
  });
});

describe('createMuseState — illegal transitions throw RangeError', () => {
  it('idle -> thinking (skipping loading) throws RangeError', () => {
    const s = createMuseState();
    assert.throws(() => s.transition('thinking'), RangeError);
  });

  it('idle -> ready throws RangeError', () => {
    const s = createMuseState();
    assert.throws(() => s.transition('ready'), RangeError);
  });

  it('loading -> ready (skipping thinking) throws RangeError', () => {
    const s = createMuseState();
    s.transition('loading');
    assert.throws(() => s.transition('ready'), RangeError);
  });

  it('unknown state name throws RangeError', () => {
    const s = createMuseState();
    assert.throws(() => s.transition('flying'), RangeError);
  });
});

describe('resolvePreset — preset-name-only (RULE-MUSE-007)', () => {
  it('undefined preset resolves to null (route default)', () => {
    assert.equal(resolvePreset(undefined, ['lens-fast', 'reason-default']), null);
  });

  it('allow-listed preset key resolves to itself', () => {
    assert.equal(resolvePreset('lens-fast', ['lens-fast', 'reason-default']), 'lens-fast');
  });

  it('unknown preset key throws MuseError unknown-preset pre-dispatch', () => {
    assert.throws(() => resolvePreset('nope', ['lens-fast']), (err) => err.code === 'unknown-preset');
  });

  it('raw model id as preset throws unknown-preset (never forwarded)', () => {
    assert.throws(
      () => resolvePreset('muse-spark-1.3', ['lens-fast']),
      (err) => err && err.code === 'unknown-preset',
    );
  });

  it('raw endpoint/param props are rejected at type check (TS2322-class)', () => {
    // JS-level guard: resolvePreset only accepts strings in the allow-list;
    // non-string model/endpoint objects are rejected, never forwarded.
    assert.throws(() => resolvePreset({ model: 'x' }, ['lens-fast']));
  });
});
