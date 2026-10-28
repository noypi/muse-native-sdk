'use strict';
// T9 — Scope teardown reverse-construction + test-double override (Batch 4).
// Plan Interface (verbatim):
// `createScope(parent?:Scope): {get<T>(k:symbol):T, dispose():void}`,
// `overrideForTest(scope:Scope, key:symbol, double:any): void`
// — `dispose()` runs disposers LIFO (reverse construction), deterministic;
// `get` after `dispose()` throws `Error("MUSE_STATE_SCOPE_DESTROYED: <key-description>")`;
// override swaps only the scoped binding, prod bindings untouched. Startup path
// (createRegistry + bind sequence) performs no dynamic allocation beyond the
// initial Map setup (assert factory laziness: no factory runs before first get).
// Spec refs: FR-STATE-006, AC-STATE-027, AC-STATE-028.
// ASSUMPTIONS (contract silent, most defensible behavior):
// A1: createScope optionally accepts a parent registry-or-scope as fallback for
//     `get` (override test passes the prod registry as parent).
// A2: scope teardown calls a `dispose()` function property on a constructed
//     value when present (mirrors lib/scope.js disposeInstance LIFO entries).
// Hermetic: node:test + node:assert/strict only; no network/secrets/fs/live services.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createRegistry, createScope, overrideForTest } = require('../lib/registry.js');

describe('T9 — no factory runs during bind/startup (lazy construction)', () => {
  it('bind sequence executes zero factories before first get', () => {
    const A = Symbol('t9-a');
    const B = Symbol('t9-b');
    let calls = 0;
    const registry = createRegistry();
    registry.bind(A, () => { calls += 1; return { id: 'a' }; }, 'singleton');
    registry.bind(B, () => { calls += 1; return { id: 'b' }; }, 'singleton');
    assert.equal(calls, 0);
    registry.get(A);
    assert.equal(calls, 1);
  });
});

describe('T9 — dispose runs reverse construction order (LIFO, deterministic)', () => {
  it('bind A,B,C constructed in order tears down C,B,A', () => {
    const A = Symbol('scope-a');
    const B = Symbol('scope-b');
    const C = Symbol('scope-c');
    const order = [];
    function make(id) {
      return { id, dispose() { order.push(id); } };
    }
    const registry = createRegistry();
    registry.bind(A, () => make('A'), 'singleton');
    registry.bind(B, () => make('B'), 'singleton');
    registry.bind(C, () => make('C'), 'singleton');
    const scope = createScope(registry);
    assert.equal(scope.get(A).id, 'A');
    assert.equal(scope.get(B).id, 'B');
    assert.equal(scope.get(C).id, 'C');
    scope.dispose();
    assert.deepEqual(order, ['C', 'B', 'A']);
  });

  it('teardown order is deterministic across builds (no RNG/time)', () => {
    function runOnce() {
      const A = Symbol('det-a');
      const B = Symbol('det-b');
      const C = Symbol('det-c');
      const order = [];
      const registry = createRegistry();
      registry.bind(A, () => ({ dispose() { order.push('A'); } }), 'singleton');
      registry.bind(B, () => ({ dispose() { order.push('B'); } }), 'singleton');
      registry.bind(C, () => ({ dispose() { order.push('C'); } }), 'singleton');
      const scope = createScope(registry);
      scope.get(A); scope.get(B); scope.get(C);
      scope.dispose();
      return order;
    }
    assert.deepEqual(runOnce(), ['C', 'B', 'A']);
    assert.deepEqual(runOnce(), ['C', 'B', 'A']);
  });

  it('dispose continues past a failing disposer and rethrows the first error', () => {
    const A = Symbol('fail-a');
    const B = Symbol('fail-b');
    const order = [];
    const boom = new Error('dispose-boom');
    const registry = createRegistry();
    registry.bind(A, () => ({ dispose() { order.push('A'); } }), 'singleton');
    registry.bind(B, () => ({ dispose() { order.push('B'); throw boom; } }), 'singleton');
    const scope = createScope(registry);
    scope.get(A); scope.get(B);
    assert.throws(() => { scope.dispose(); }, (err) => err === boom);
    assert.deepEqual(order, ['B', 'A']);
  });
});

describe('T9 — get after dispose fails fast with a diagnostic', () => {
  it('get after dispose throws MUSE_STATE_SCOPE_DESTROYED with key description', () => {
    const KEY = Symbol('my-service');
    const registry = createRegistry();
    registry.bind(KEY, () => ({ v: 1 }), 'singleton');
    const scope = createScope(registry);
    assert.deepEqual(scope.get(KEY), { v: 1 });
    scope.dispose();
    assert.throws(() => { scope.get(KEY); }, (err) => {
      assert.ok(err instanceof Error);
      assert.ok(err.message.startsWith('MUSE_STATE_SCOPE_DESTROYED: '));
      assert.ok(err.message.includes('my-service'));
      return true;
    });
  });
});

describe('T9 — overrideForTest swaps only the scoped binding', () => {
  it('double is visible in the test scope; prod bindings untouched', () => {
    const KEY = Symbol('svc-prod');
    const registry = createRegistry();
    registry.bind(KEY, () => ({ real: true }), 'singleton');
    const scope = createScope(registry);
    const double = { real: false, double: true };
    overrideForTest(scope, KEY, double);
    assert.equal(scope.get(KEY), double);
    assert.deepEqual(registry.get(KEY), { real: true });
  });

  it('override with a string key throws TypeError', () => {
    const registry = createRegistry();
    const scope = createScope(registry);
    assert.throws(() => { overrideForTest(scope, 'name-string', {}); }, TypeError);
  });
});

describe('T9 — override×dispose: overridden bindings do not leak after dispose', () => {
  it('get then override: dispose drains the originally constructed value (no leak)', () => {
    // ASSUMPTION: `overrideForTest` enters teardown order; dispose drains the
    // value `get` would have returned at each construction step, so the
    // originally constructed disposable is not leaked when an override
    // shadows it. Review MAJOR: current code prefers the override and leaks
    // the original.
    const KEY = Symbol('ovd-after');
    const order = [];
    const registry = createRegistry();
    registry.bind(KEY, () => ({ dispose() { order.push('original'); } }), 'singleton');
    const scope = createScope(registry);
    scope.get(KEY);
    overrideForTest(scope, KEY, { dispose() { order.push('double'); } });
    scope.dispose();
    assert.ok(order.includes('original'), 'expected original disposer to run, got: ' + JSON.stringify(order));
    assert.throws(() => { scope.get(KEY); }, (err) => err.message.startsWith('MUSE_STATE_SCOPE_DESTROYED: '));
  });

  it('override before get: disposable double is tracked and disposed (no silent skip)', () => {
    // ASSUMPTION: overriding before any `get` still tracks the key in
    // teardown order, so a disposable double is disposed. Review MAJOR:
    // current `scopeGet` returns the override early without pushing the key
    // to `order`, so the double is silently never disposed.
    const KEY = Symbol('ovd-before');
    const order = [];
    const registry = createRegistry();
    registry.bind(KEY, () => ({ v: 'real' }), 'singleton');
    const scope = createScope(registry);
    const double = { v: 'double', dispose() { order.push('double'); } };
    overrideForTest(scope, KEY, double);
    assert.equal(scope.get(KEY), double);
    scope.dispose();
    assert.deepEqual(order, ['double']);
    assert.throws(() => { scope.get(KEY); }, (err) => err.message.startsWith('MUSE_STATE_SCOPE_DESTROYED: '));
  });
});

describe('T9 — stacked overrides dispose top-first with no leak (security hardening, AC-STATE-027)', () => {
  it('overrideForTest(k,d1); overrideForTest(k,d2); dispose() disposes d2, d1, and the original (top-first)', () => {
    const KEY = Symbol('stacked-k');
    const order = [];
    const registry = createRegistry();
    registry.bind(KEY, () => ({ dispose() { order.push('original'); } }), 'singleton');
    const scope = createScope(registry);
    scope.get(KEY);
    overrideForTest(scope, KEY, { dispose() { order.push('d1'); } });
    overrideForTest(scope, KEY, { dispose() { order.push('d2'); } });
    scope.dispose();
    assert.deepEqual(order, ['d2', 'd1', 'original']);
    assert.throws(() => { scope.get(KEY); }, (err) => err.message.startsWith('MUSE_STATE_SCOPE_DESTROYED: '));
  });
});

describe('T9 — transient parent binding resolves fresh per scope-get', () => {
  it('transient binding is not frozen to the first value through the scope', () => {
    // ASSUMPTION: a scope does not memoize transient parent bindings into
    // singletons; each `scope.get` resolves fresh. Review MAJOR: current
    // `scopeGet` caches `parent.get()` in `instances`, freezing transient to
    // first value. (Plan Interface names only `Scope` parents; the registry
    // parent here follows contract assumption A1.)
    const KEY = Symbol('transient-k');
    const registry = createRegistry();
    registry.bind(KEY, () => ({}), 'transient');
    const scope = createScope(registry);
    const first = scope.get(KEY);
    const second = scope.get(KEY);
    assert.notEqual(first, second);
  });
});
