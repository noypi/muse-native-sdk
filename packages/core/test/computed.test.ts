'use strict';
// T5 — lazy cached topological computed contract (Batch 2).
// Plan Interface (verbatim):
// `computed<T>(getter: () => T): { readonly value: T }` — lazy on first read,
// cached until a dependency changes; writing `.value` throws
// `MUSE_CORE_READONLY_WRITE`.
// Spec refs: AC-MCORE-006, AC-MCORE-007 (TC-MCORE-006/007 → AC-MCORE-006/007).
// Hermetic: node:test + node:assert/strict only; scheduler/state observed via
// the modules under test; no network/secrets/fs/live services. Usable outside
// a component: every case runs at top level with no instance.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const schedulerPath = require.resolve('../lib/scheduler.js');
const scheduler = require(schedulerPath);
let scheduleCalls = 0;
const origSchedule = scheduler.schedule;
scheduler.schedule = function (job) { scheduleCalls += 1; return origSchedule.call(this, job); };
const { flush } = scheduler;

const { state } = require('../lib/state.js');
const { computed } = require('../lib/computed.js');

beforeEach(() => {
  scheduleCalls = 0;
  flush();
  scheduleCalls = 0;
});

describe('TC-MCORE-006 — lazy zero-extra-invocation caching; write throws (AC-MCORE-006)', () => {
  it('getter is lazy: not invoked until the first read', () => {
    const s = state(1);
    let calls = 0;
    const c = computed(() => { calls += 1; return s.value * 2; });
    assert.equal(calls, 0);
    assert.equal(c.value, 2);
    assert.equal(calls, 1);
  });

  it('repeated reads before a change invoke the getter zero additional times', () => {
    const s = state(1);
    let calls = 0;
    const c = computed(() => { calls += 1; return s.value + 1; });
    assert.equal(c.value, 2);
    assert.equal(c.value, 2);
    assert.equal(c.value, 2);
    assert.equal(calls, 1);
  });

  it('after a dependency change the next read recomputes exactly once', () => {
    const s = state(1);
    let calls = 0;
    const c = computed(() => { calls += 1; return s.value * 10; });
    assert.equal(c.value, 10);
    assert.equal(calls, 1);
    s.value = 2;
    flush();
    assert.equal(c.value, 20);
    assert.equal(calls, 2);
    assert.equal(c.value, 20);
    assert.equal(calls, 2);
  });

  it('identical-value source write does not invalidate the cache (no recompute)', () => {
    const s = state(1);
    let calls = 0;
    const c = computed(() => { calls += 1; return s.value * 2; });
    assert.equal(c.value, 2);
    assert.equal(calls, 1);
    s.value = 1;
    flush();
    assert.equal(c.value, 2);
    assert.equal(calls, 1);
  });

  it('writing .value throws MUSE_CORE_READONLY_WRITE and preserves the cached value', () => {
    const s = state(1);
    const c = computed(() => s.value + 1);
    assert.equal(c.value, 2);
    assert.throws(() => { c.value = 99; }, /MUSE_CORE_READONLY_WRITE/);
    assert.equal(c.value, 2);
  });
});

describe('TC-MCORE-007 — topological glitch-free chains, exactly-once recompute per flush (AC-MCORE-007)', () => {
  it('a chained computed observes the updated upstream value (no glitch)', () => {
    const s = state(1);
    const seen = [];
    const a = computed(() => s.value + 1);
    const b = computed(() => { const v = a.value; seen.push(v); return v * 10; });
    assert.equal(b.value, 20);
    s.value = 2;
    flush();
    assert.equal(b.value, 30);
    // b never observed a stale/intermediate `a` (only 2 then 3).
    assert.deepEqual(seen, [2, 3]);
  });

  it('each computed in a chain recomputes exactly once per flush', () => {
    const s = state(1);
    let aCalls = 0;
    let bCalls = 0;
    let cCalls = 0;
    const a = computed(() => { aCalls += 1; return s.value + 1; });
    const b = computed(() => { bCalls += 1; return a.value + 1; });
    const c = computed(() => { cCalls += 1; return b.value + 1; });
    assert.equal(c.value, 4);
    assert.deepEqual([aCalls, bCalls, cCalls], [1, 1, 1]);
    s.value = 10;
    flush();
    assert.equal(c.value, 13);
    assert.deepEqual([aCalls, bCalls, cCalls], [2, 2, 2]);
  });

  it('diamond chains settle to one consistent value with one recompute each (ASSUMPTION: scheduler orders topological evaluation)', () => {
    // ASSUMPTION: the coder evaluates computeds through T2's scheduler in
    // topological order, so the join node never publishes a mixed
    // (updated-left, stale-right) value. The observable contract is the
    // final value plus exactly-once recompute; order itself is internal.
    const s = state(1);
    let leftCalls = 0;
    let rightCalls = 0;
    let joinCalls = 0;
    const left = computed(() => { leftCalls += 1; return s.value + 1; });
    const right = computed(() => { rightCalls += 1; return s.value + 2; });
    const join = computed(() => { joinCalls += 1; return left.value + right.value; });
    assert.equal(join.value, 5);
    s.value = 10;
    flush();
    assert.equal(join.value, 23);
    assert.deepEqual([leftCalls, rightCalls, joinCalls], [2, 2, 2]);
  });
});
