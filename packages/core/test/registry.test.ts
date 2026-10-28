'use strict';
// T8 — Compile-time-keyed DI registry, allocation-free hot path (Batch 4).
// Plan Interface (verbatim):
// `createRegistry(): {bind<T>(key:symbol, factory:(c:Registry)=>T, scope:"singleton"|"transient"): void, get<T>(key:symbol): T}`
// — keys are Symbol constants (constexpr-id analogue, never runtime string map
// on hot path); `get` after bootstrap performs zero allocation on hit (cached
// singleton returned directly); unknown key throws `Error("MUSE_STATE_UNBOUND_SERVICE")`.
// Spec refs: FR-STATE-006, AC-STATE-026.
// Hermetic: node:test + node:assert/strict only; registry module under test;
// no network/secrets/fs/live services. Every collaborator is in-process.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createRegistry } = require('../lib/registry.js');

describe('T8 — registry binds symbol keys and resolves singletons', () => {
  it('bind + get returns the factory product', () => {
    const KEY = Symbol('svc-a');
    const registry = createRegistry();
    registry.bind(KEY, () => ({ name: 'a' }), 'singleton');
    assert.deepEqual(registry.get(KEY), { name: 'a' });
  });

  it('10,000 gets hit the cache: factory runs once, identical reference', () => {
    const KEY = Symbol('svc-singleton');
    let calls = 0;
    const registry = createRegistry();
    registry.bind(KEY, () => { calls += 1; return { id: 1 }; }, 'singleton');
    const first = registry.get(KEY);
    for (let i = 0; i < 10000; i += 1) {
      assert.equal(registry.get(KEY), first);
    }
    assert.equal(calls, 1);
  });

  it('factory receives the registry for composed resolution', () => {
    const A = Symbol('svc-base');
    const B = Symbol('svc-composed');
    const registry = createRegistry();
    registry.bind(A, () => ({ v: 41 }), 'singleton');
    registry.bind(B, (c) => ({ v: c.get(A).v + 1 }), 'singleton');
    assert.equal(registry.get(B).v, 42);
  });

  it('transient scope constructs a fresh value per get', () => {
    const KEY = Symbol('svc-transient');
    let calls = 0;
    const registry = createRegistry();
    registry.bind(KEY, () => { calls += 1; return { n: calls }; }, 'transient');
    const a = registry.get(KEY);
    const b = registry.get(KEY);
    assert.notEqual(a, b);
    assert.equal(calls, 2);
  });

  it('bind with a string key throws TypeError (no runtime string map)', () => {
    const registry = createRegistry();
    assert.throws(() => { registry.bind('name-string', () => ({}), 'singleton'); }, TypeError);
  });

  it('bind with a non-symbol key (number) throws TypeError', () => {
    const registry = createRegistry();
    assert.throws(() => { registry.bind(42, () => ({}), 'singleton'); }, TypeError);
  });

  it('get of an unknown key throws MUSE_STATE_UNBOUND_SERVICE', () => {
    const registry = createRegistry();
    const MISSING = Symbol('missing');
    assert.throws(() => { registry.get(MISSING); }, (err) => {
      assert.ok(err instanceof Error);
      assert.equal(err.message, 'MUSE_STATE_UNBOUND_SERVICE');
      return true;
    });
  });

  it('distinct symbols resolve independently', () => {
    const A = Symbol('a');
    const B = Symbol('b');
    const registry = createRegistry();
    registry.bind(A, () => 'A', 'singleton');
    registry.bind(B, () => 'B', 'singleton');
    assert.equal(registry.get(A), 'A');
    assert.equal(registry.get(B), 'B');
  });
});
