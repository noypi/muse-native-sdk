'use strict';
// T6+T7 — effect / watch / watchEffect contract (Batch 3).
// Plan Interfaces (verbatim):
// T6: `effect(fn: () => (void | (() => void))): () => void` — runs `fn`
//   immediately, re-runs after a flush where a tracked dep changed; if `fn`
//   returns a function it is the ONE cleanup, run before the next invocation
//   and on dispose; returning a second/non-function cleanup throws
//   `MUSE_CORE_CLEANUP_ARITY`. Returns a disposer that runs the pending
//   cleanup exactly once and stops future runs.
// T7: `watch<T>(source: { value: T } | (() => T), cb: (newValue: T, oldValue: T) => void,
//   opts?: { immediate?: boolean, flush?: "post" }): () => void` — invokes
//   `cb(newValue, oldValue)` after the flush on each change of a state/computed
//   source; `watchEffect(fn: () => void): () => void` — tracks deps read in
//   `fn`, re-runs after the flush (unchanged). Both return disposers;
//   identical-value source writes never invoke the callback.
// Spec refs: AC-MCORE-008 (TC-MCORE-008), AC-MCORE-009 (TC-MCORE-009),
//   AC-MCORE-010 (TC-MCORE-010), AC-MCORE-020 (TC-MCORE-020).
// Hermetic: node:test + node:assert/strict only; scheduler/state/computed via
// the modules under test; no network/secrets/fs/live services. Usable outside
// a component: every case runs at top level with no instance (instance-owned
// disposal on unmount is T9/scope.test.ts territory — see ASSUMPTIONs).

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const scheduler = require('../lib/scheduler.js');
const { flush } = scheduler;

const { state } = require('../lib/state.js');
const { computed } = require('../lib/computed.js');
const { effect, watch, watchEffect } = require('../lib/effects.js');

beforeEach(() => {
  flush();
});

// ---------------------------------------------------------------- T6: effect

describe('TC-MCORE-020 (effect half) + AC-MCORE-008 effect half — immediate run + rerun-on-change', () => {
  it('runs fn immediately once upon registration', () => {
    const s = state(1);
    let runs = 0;
    let seen;
    effect(() => { runs += 1; seen = s.value; });
    assert.equal(runs, 1);
    assert.equal(seen, 1);
  });

  it('re-runs after a flush where a tracked dep changed, not synchronously', () => {
    const s = state(1);
    let runs = 0;
    let seen;
    effect(() => { runs += 1; seen = s.value; });
    assert.equal(runs, 1);
    s.value = 2;
    assert.equal(runs, 1); // no synchronous re-run before the flush
    flush();
    assert.equal(runs, 2);
    assert.equal(seen, 2);
  });

  it('does not re-run when no tracked dep changed', () => {
    const s = state(1);
    const other = state(100);
    let runs = 0;
    effect(() => { runs += 1; void s.value; });
    assert.equal(runs, 1);
    other.value = 101;
    flush();
    assert.equal(runs, 1);
  });

  it('void (undefined) return needs no cleanup and re-runs cleanly', () => {
    const s = state(1);
    let runs = 0;
    const dispose = effect(() => { runs += 1; void s.value; });
    s.value = 2;
    flush();
    assert.equal(runs, 2);
    assert.doesNotThrow(() => { dispose(); });
  });

  it('coalesces multiple writes into one re-run per flush (AC-MCORE-009 effect angle)', () => {
    const a = state(1);
    const b = state(10);
    let runs = 0;
    effect(() => { runs += 1; void a.value; void b.value; });
    assert.equal(runs, 1);
    a.value = 2;
    b.value = 20;
    a.value = 3;
    flush();
    assert.equal(runs, 2);
  });

  it('identical-value writes cause no re-run (AC-MCORE-010 effect angle)', () => {
    const s = state(1);
    let runs = 0;
    effect(() => { runs += 1; void s.value; });
    assert.equal(runs, 1);
    s.value = 1;
    flush();
    assert.equal(runs, 1);
  });
});

describe('TC-MCORE-020 — single cleanup exactly-once before next run and on dispose', () => {
  it('returned cleanup runs exactly once before the next invocation', () => {
    const s = state(1);
    const cleanups = [];
    let runs = 0;
    effect(() => {
      runs += 1;
      const seen = s.value;
      return () => { cleanups.push(seen); };
    });
    assert.equal(runs, 1);
    assert.deepEqual(cleanups, []);
    s.value = 2;
    flush();
    assert.equal(runs, 2);
    assert.deepEqual(cleanups, [1]);
  });

  it('disposer runs the pending cleanup exactly once and stops future runs', () => {
    const s = state(1);
    const cleanups = [];
    let runs = 0;
    const dispose = effect(() => {
      runs += 1;
      const seen = s.value;
      return () => { cleanups.push(seen); };
    });
    s.value = 2;
    flush();
    assert.deepEqual(cleanups, [1]);
    dispose();
    assert.deepEqual(cleanups, [1, 2]); // pending cleanup from the last run
    s.value = 3;
    flush();
    assert.equal(runs, 2); // stopped: no further invocations
    assert.deepEqual(cleanups, [1, 2]); // never again after dispose
  });

  it('double dispose runs the cleanup exactly once and stays silent', () => {
    const s = state(1);
    const cleanups = [];
    let runs = 0;
    const dispose = effect(() => {
      runs += 1;
      const seen = s.value;
      return () => { cleanups.push(seen); };
    });
    assert.equal(runs, 1);
    dispose();
    dispose();
    assert.deepEqual(cleanups, [1]);
    s.value = 9;
    flush();
    assert.equal(runs, 1);
    assert.deepEqual(cleanups, [1]);
  });

  it('returning a non-function cleanup value throws MUSE_CORE_CLEANUP_ARITY (ASSUMPTION: any non-undefined non-function return throws, surfacing from the invocation that produced it)', () => {
    assert.throws(() => { effect(() => 42); }, /MUSE_CORE_CLEANUP_ARITY/);
  });

  it('returning a non-function object cleanup value throws MUSE_CORE_CLEANUP_ARITY', () => {
    assert.throws(() => { effect(() => 'not-a-cleanup'); }, /MUSE_CORE_CLEANUP_ARITY/);
  });
});

// ---------------------------------------------------------------- T7: watch

describe('TC-MCORE-008 — watch(source, cb) receives (newValue, oldValue) after flush on each change', () => {
  it('invokes cb(newValue, oldValue) after the flush on a state change', () => {
    const s = state(1);
    const calls = [];
    watch(s, (n, o) => { calls.push([n, o]); });
    s.value = 2;
    assert.deepEqual(calls, []); // never synchronously
    flush();
    assert.deepEqual(calls, [[2, 1]]);
  });

  it('invokes cb on each change with the latest (newValue, oldValue) pair', () => {
    const s = state(1);
    const calls = [];
    watch(s, (n, o) => { calls.push([n, o]); });
    s.value = 2;
    flush();
    s.value = 3;
    flush();
    assert.deepEqual(calls, [[2, 1], [3, 2]]);
  });

  it('observes a computed source with (newValue, oldValue)', () => {
    const s = state(1);
    const c = computed(() => s.value * 10);
    void c.value;
    const calls = [];
    watch(c, (n, o) => { calls.push([n, o]); });
    s.value = 2;
    flush();
    assert.deepEqual(calls, [[20, 10]]);
  });

  it('getter-function source is tracked and reported (ASSUMPTION: getter form supported per plan Interface)', () => {
    const s = state(1);
    const calls = [];
    watch(() => s.value * 2, (n, o) => { calls.push([n, o]); });
    s.value = 5;
    flush();
    assert.deepEqual(calls, [[10, 2]]);
  });

  it('does not invoke cb when nothing changed', () => {
    const s = state(1);
    let calls = 0;
    watch(s, () => { calls += 1; });
    flush();
    assert.equal(calls, 0);
  });

  it('identical-value source writes never invoke the callback (AC-MCORE-010)', () => {
    const s = state(1);
    let calls = 0;
    watch(s, () => { calls += 1; });
    s.value = 1;
    flush();
    assert.equal(calls, 0);
  });

  it('immediate:true fires once at setup with (currentValue, undefined)', () => {
    const s = state(7);
    const calls = [];
    watch(s, (n, o) => { calls.push([n, o]); }, { immediate: true });
    assert.deepEqual(calls, [[7, undefined]]);
  });

  it('immediate:true setup call is followed by per-change callbacks with correct oldValue', () => {
    const s = state(7);
    const calls = [];
    watch(s, (n, o) => { calls.push([n, o]); }, { immediate: true });
    s.value = 8;
    flush();
    assert.deepEqual(calls, [[7, undefined], [8, 7]]);
  });

  it('flush:"post" delivers post-flush, never synchronously', () => {
    const s = state(1);
    const calls = [];
    watch(s, (n, o) => { calls.push([n, o]); }, { flush: 'post' });
    s.value = 2;
    assert.deepEqual(calls, []);
    flush();
    assert.deepEqual(calls, [[2, 1]]);
  });

  it('disposer stops future callbacks, including across a queued flush', () => {
    const s = state(1);
    let calls = 0;
    const dispose = watch(s, () => { calls += 1; });
    dispose();
    s.value = 2;
    flush();
    assert.equal(calls, 0);
  });
});

describe('TC-MCORE-009 — three-writes-one-flush through watch; identical-write no-op', () => {
  it('three synchronous writes across two states produce one flush and one callback with the latest value', () => {
    const a = state(1);
    const b = state(10);
    const total = computed(() => a.value + b.value);
    void total.value;
    const calls = [];
    watch(total, (n, o) => { calls.push([n, o]); });
    a.value = 2;
    b.value = 20;
    a.value = 3;
    flush();
    // ASSUMPTION: on coalesced writes oldValue is the value at the last
    // callback (here: setup), newValue the settled latest.
    assert.deepEqual(calls, [[23, 11]]);
  });

  it('identical writes among real writes do not produce extra callbacks', () => {
    const s = state(1);
    const calls = [];
    watch(s, (n, o) => { calls.push([n, o]); });
    s.value = 1; // no-op
    s.value = 2; // real change
    s.value = 2; // no-op
    flush();
    assert.deepEqual(calls, [[2, 1]]);
  });
});

// ------------------------------------------------------- T7: watchEffect

describe('TC-MCORE-008 (watchEffect half) — tracks deps read in fn, re-runs after the flush', () => {
  it('re-runs after the flush when a read dep changed, not synchronously', () => {
    const s = state(1);
    const other = state(100);
    let runs = 0;
    let seen;
    watchEffect(() => { runs += 1; seen = s.value; });
    assert.equal(runs, 1);
    assert.equal(seen, 1);
    s.value = 2;
    assert.equal(runs, 1);
    flush();
    assert.equal(runs, 2);
    assert.equal(seen, 2);
    void other;
  });

  it('does not re-run when an unread value changes', () => {
    const s = state(1);
    const other = state(100);
    let runs = 0;
    watchEffect(() => { runs += 1; void s.value; });
    other.value = 101;
    flush();
    assert.equal(runs, 1);
  });

  it('identical-value writes cause no re-run', () => {
    const s = state(1);
    let runs = 0;
    watchEffect(() => { runs += 1; void s.value; });
    s.value = 1;
    flush();
    assert.equal(runs, 1);
  });

  it('disposer stops future re-runs', () => {
    const s = state(1);
    let runs = 0;
    const dispose = watchEffect(() => { runs += 1; void s.value; });
    assert.equal(runs, 1);
    dispose();
    s.value = 2;
    flush();
    assert.equal(runs, 1);
  });
});
