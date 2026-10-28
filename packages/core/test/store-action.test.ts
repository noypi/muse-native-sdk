'use strict';
// T2 — store actions on worker-pool seam, single batched invalidation (Batch 1).
// Plan Interface (verbatim):
// `createStore<S>(init:()=>S, actions:{...}): {state:S, dispatch:(name:string,...args:any[])=>Promise<any>}`
// — dispatch runs via injected worker-pool seam (default queueMicrotask), one
// scheduler batch per action; unknown name rejects
// `Error("MUSE_STATE_UNKNOWN_ACTION: "+name)`.
// Acceptance: action writing 3 atoms → one flush; subscriber notified once;
// runs on injected seam; unknown rejects.
// Spec refs: FR-STATE-002, AC-STATE-011.
// Hermetic: node:test + node:assert/strict only; scheduler observed via a
// require-cache spy; injected seam is an in-test fake (no threads, no network,
// no secrets/fs/live services).
//
// ASSUMPTION (seam injection): the Interface line names `createStore(init,
// actions)` plus an "injected worker-pool seam (default queueMicrotask)"; the
// injection point is an optional third argument `createStore(init, actions,
// { schedule })` where `schedule(fn)` defaults to `queueMicrotask`. Tests use
// that optional arg; the two-arg shape still works with the default seam.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const schedulerPath = require.resolve('../lib/scheduler.js');
const scheduler = require(schedulerPath);
let scheduleCalls = 0;
const origSchedule = scheduler.schedule;
scheduler.schedule = function (job) { scheduleCalls += 1; return origSchedule.call(this, job); };

const { createStore } = require('../lib/store.js');
const { flush } = scheduler;
const { state, getRefRecord } = require('../lib/state.js');

beforeEach(() => {
  scheduleCalls = 0;
  flush();
  scheduleCalls = 0;
});

function tick() {
  return new Promise((resolve) => { queueMicrotask(resolve); });
}

describe('T2 — action writing 3 atoms: one flush, subscriber notified once (AC-STATE-011)', () => {
  it('one action writing 3 atoms schedules each atom once but flushes a single batch', async () => {
    const s = createStore(
      () => ({ x: state(0), y: state(0), z: state(0) }),
      { write3: (ctx) => { ctx.state.x.value = 1; ctx.state.y.value = 2; ctx.state.z.value = 3; } },
      { schedule: (fn) => fn() },
    );
    let flushes = 0;
    const origFlush = scheduler.flush;
    scheduler.flush = function () { flushes += 1; return origFlush.call(this); };
    try {
      scheduleCalls = 0;
      await s.dispatch('write3');
      assert.equal(scheduleCalls, 3);
      assert.equal(flushes, 1);
      assert.equal(s.state.x.value, 1);
      assert.equal(s.state.y.value, 2);
      assert.equal(s.state.z.value, 3);
    } finally {
      scheduler.flush = origFlush;
    }
  });

  it('a subscriber to store atoms is notified once per action flush', async () => {
    const s = createStore(
      () => ({ x: state(0), y: state(0), z: state(0) }),
      { write3: (ctx) => { ctx.state.x.value = 1; ctx.state.y.value = 2; ctx.state.z.value = 3; } },
      { schedule: (fn) => fn() },
    );
    let notifications = 0;
    getRefRecord(s.state.x).subs.add(() => { notifications += 1; });
    await s.dispatch('write3');
    flush();
    assert.equal(notifications, 1);
  });
});

describe('T2 — dispatch runs on the injected worker-pool seam (AC-STATE-011)', () => {
  it('action runs on the injected seam in order: seam then action', async () => {
    const order = [];
    const s = createStore(
      () => ({ n: state(0) }),
      { inc: (ctx) => { order.push('action'); ctx.state.n.value += 1; } },
      { schedule: (fn) => { order.push('seam'); fn(); } },
    );
    await s.dispatch('inc');
    assert.deepEqual(order, ['seam', 'action']);
    assert.equal(s.state.n.value, 1);
  });

  it('dispatch resolves with the action return value and forwards extra args', async () => {
    const s = createStore(
      () => ({ n: state(0) }),
      { get: (ctx, suffix) => `v:${ctx.state.n.value}:${suffix}` },
      { schedule: (fn) => fn() },
    );
    assert.equal(await s.dispatch('get', 'a'), 'v:0:a');
  });

  it('default seam is queueMicrotask: dispatch is async, never a synchronous tree walk', async () => {
    const s = createStore(
      () => ({ n: state(0) }),
      { inc: (ctx) => { ctx.state.n.value += 1; } },
    );
    let ran = false;
    const p = s.dispatch('inc').then(() => { ran = true; });
    assert.equal(ran, false);
    await p;
    await tick();
    flush();
    assert.equal(ran, true);
    assert.equal(s.state.n.value, 1);
  });
});

describe('T2 — unknown action rejects with the named error (AC-STATE-011)', () => {
  it('unknown action name rejects with MUSE_STATE_UNKNOWN_ACTION', async () => {
    const s = createStore(() => ({}), {}, { schedule: (fn) => fn() });
    await assert.rejects(
      () => s.dispatch('nope'),
      (err) => err instanceof Error && err.message === 'MUSE_STATE_UNKNOWN_ACTION: nope',
    );
  });

  it('dispatch returns a Promise rejection (never throws synchronously) for unknown names', async () => {
    const s = createStore(() => ({}), {}, { schedule: (fn) => fn() });
    const p = s.dispatch('missing');
    assert.ok(p instanceof Promise);
    await assert.rejects(() => p, /MUSE_STATE_UNKNOWN_ACTION: missing/);
  });

  it('a throwing action rejects and leaves prior state intact', async () => {
    const s = createStore(
      () => ({ n: state(1) }),
      { boom: () => { throw new Error('kablam'); } },
      { schedule: (fn) => fn() },
    );
    await assert.rejects(() => s.dispatch('boom'), /kablam/);
    assert.equal(s.state.n.value, 1);
  });
});
