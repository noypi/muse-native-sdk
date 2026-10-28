'use strict';
// T6 — Optimistic mutation with inverse patch + restart-safe rollback (Batch 3).
// Plan Interface (verbatim):
// `dispatch.optimistic(name:string, optimisticPatch:(s:any)=>()=>void,
//   authoritative:()=>Promise<void>): Promise<void>` — applies patch immediately,
//   records `inverse:()=>void`; on authoritative reject/failure runs `inverse()`
//   then flushes one batch. `serializePending(): string` returns JSON of
//   `{action, inversePatch, idempotencyKey}` for the durable-queue stub;
//   `restorePending(serialized:string): void` re-registers it. Failure after
//   restart: `restorePending` + failed authoritative result → `inverse()` runs
//   atomically.
// Acceptance: failing authoritative → state deep-equals pre-mutation snapshot,
// single flush; kill/restart simulated via save→fresh-store→load→fail → still
// rolls back atomically; idempotency key present in serialized form.
// Spec refs: FR-STATE-002, AC-STATE-012.
// Hermetic: node:test + node:assert/strict only; scheduler observed via a
// require-cache spy; worker-pool seam + durable-queue seam are in-test fakes
// (no threads, no network, no secrets/fs/live services).
//
// ASSUMPTION (durable seam injection): the Interface line names
// `serializePending`/`restorePending` plus an "injected `{save, load}` seam"
// from the plan Detail without settling the injection parameter. Tests assume an
// optional `createStore(init, actions, { schedule, save, load })` shape where
// `save(s:string)` persists and `load()` returns `string|null`; the two/three-arg
// shapes keep working otherwise. If the coder/planner chooses a different
// injection shape, the call sites (not the expectations) must be reconciled.
// ASSUMPTION (restart semantics): `restorePending(serialized)` re-applies the
// pending optimistic patch and re-registers its inverse + idempotency key, so a
// subsequent failed authoritative result for the same action rolls back
// atomically. Restart-test patches are constant-sets (idempotent) so
// re-application is safe.
// ASSUMPTION (unconditional tombstone; crash-before-settle only): success
// tombstones the durable slot UNCONDITIONALLY (no `exported` nuance). Restart
// tests therefore model crash-before-settle ONLY — never resolve success on
// the pre-restart store in a restart flow; success-then-restore would replay
// a completed intent and violate exactly-once (AC-STATE-012).

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

const syncSeam = { schedule: (fn) => fn() };

beforeEach(() => {
  scheduleCalls = 0;
  flush();
  scheduleCalls = 0;
  try { scheduler.getInvalidatedPaths(); } catch (e) { void e; }
});

function snapshot(s) {
  const out = {};
  for (const k of Object.keys(s)) {
    const v = s[k];
    out[k] = v && typeof v === 'object' && 'value' in v ? v.value : v;
  }
  return out;
}

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((res, rej) => { resolve = res; reject = rej; });
  return { promise, resolve, reject };
}

function countFlushes() {
  let flushes = 0;
  const origFlush = scheduler.flush;
  scheduler.flush = function () { flushes += 1; return origFlush.call(this); };
  return {
    get count() { return flushes; },
    restore() { scheduler.flush = origFlush; },
  };
}

describe('T6 — optimistic patch applies immediately, failing authoritative rolls back (AC-STATE-012)', () => {
  it('dispatch.optimistic exists on dispatch and applies the patch before authoritative settles', async () => {
    const s = createStore(
      () => ({ n: state(1) }),
      {},
      syncSeam,
    );
    assert.equal(typeof s.dispatch.optimistic, 'function');
    const gate = deferred();
    const p = s.dispatch.optimistic(
      'rename',
      (st) => { const prev = st.n.value; st.n.value = 99; return () => { st.n.value = prev; }; },
      () => gate.promise,
    );
    assert.equal(s.state.n.value, 99);
    gate.resolve();
    await p;
    assert.equal(s.state.n.value, 99);
  });

  it('failing authoritative rejects and restores the exact pre-mutation snapshot (deep-equal)', async () => {
    const s = createStore(
      () => ({ a: state(1), b: state('x') }),
      {},
      syncSeam,
    );
    const before = snapshot(s.state);
    let inverseRuns = 0;
    await assert.rejects(
      () => s.dispatch.optimistic(
        'rename',
        (st) => {
          const pa = st.a.value; const pb = st.b.value;
          st.a.value = 2; st.b.value = 'y';
          return () => { inverseRuns += 1; st.a.value = pa; st.b.value = pb; };
        },
        () => Promise.reject(new Error('authoritative-nope')),
      ),
      /authoritative-nope/,
    );
    assert.equal(inverseRuns, 1);
    assert.deepEqual(snapshot(s.state), before);
  });

  it('rollback flushes exactly one batch (apply + rollback = exactly 2 flushes)', async () => {
    const s = createStore(
      () => ({ a: state(0), b: state(0), c: state(0) }),
      {},
      syncSeam,
    );
    const counter = countFlushes();
    try {
      await assert.rejects(
        () => s.dispatch.optimistic(
          'write3',
          (st) => {
            const pa = st.a.value; const pb = st.b.value; const pc = st.c.value;
            st.a.value = 1; st.b.value = 2; st.c.value = 3;
            return () => { st.a.value = pa; st.b.value = pb; st.c.value = pc; };
          },
          () => Promise.reject(new Error('boom')),
        ),
        /boom/,
      );
      assert.equal(counter.count, 2);
    } finally {
      counter.restore();
    }
  });

  it('successful authoritative keeps the optimistic values and never runs the inverse', async () => {
    const s = createStore(
      () => ({ n: state(1) }),
      {},
      syncSeam,
    );
    let inverseRuns = 0;
    await s.dispatch.optimistic(
      'rename',
      (st) => { const prev = st.n.value; st.n.value = 42; return () => { inverseRuns += 1; st.n.value = prev; }; },
      () => Promise.resolve(),
    );
    assert.equal(inverseRuns, 0);
    assert.equal(s.state.n.value, 42);
  });
});

describe('T6 — pending intent serializes with action + idempotency key (AC-STATE-012)', () => {
  it('serializePending returns JSON containing action and a non-empty idempotencyKey', async () => {
    const s = createStore(
      () => ({ n: state(1) }),
      {},
      syncSeam,
    );
    assert.equal(typeof s.serializePending, 'function');
    const gate = deferred();
    const p = s.dispatch.optimistic(
      'rename',
      (st) => { const prev = st.n.value; st.n.value = 7; return () => { st.n.value = prev; }; },
      () => gate.promise,
    );
    const raw = s.serializePending();
    const parsed = JSON.parse(raw);
    assert.equal(parsed.action, 'rename');
    assert.equal(typeof parsed.idempotencyKey, 'string');
    assert.ok(parsed.idempotencyKey.length > 0);
    assert.ok('inversePatch' in parsed);
    // Key is stable for the same pending.
    assert.equal(JSON.parse(s.serializePending()).idempotencyKey, parsed.idempotencyKey);
    gate.resolve();
    await p;
  });
});

describe('T6 — kill/restart via save→fresh-store→load→fail still rolls back atomically (AC-STATE-012)', () => {
  it('restorePending alone re-applies the recorded patch (state correct before any fresh call)', async () => {
    // ASSUMPTION (restore re-applies patch): `restorePending(serialized)` must
    // re-apply the recorded optimistic patch so the fresh store's state is
    // correct immediately after restore, before any fresh `optimistic` call.
    // Review gap: the current restart test only passes because it issues a
    // fresh `optimistic` call afterwards, masking a hollow `restorePending`.
    let saved = null;
    const save1 = (v) => { saved = v; };
    const load1 = () => saved;
    const patch = (st) => { st.n.value = 99; return () => { st.n.value = 1; }; };
    const s1 = createStore(
      () => ({ n: state(1) }),
      {},
      { schedule: (fn) => fn(), save: save1, load: load1 },
    );
    const gate = deferred();
    const p1 = s1.dispatch.optimistic('rename', patch, () => gate.promise);
    // Avoid unhandled rejection: s1 is abandoned (crash) before settle; its
    // authoritative will FAIL after the restore below, never succeed.
    p1.catch(() => {});
    const raw = s1.serializePending();
    save1(raw);
    // Crash-before-settle: NO gate.resolve() here — s1 never succeeds.
    const s2 = createStore(
      () => ({ n: state(1) }),
      {},
      { schedule: (fn) => fn(), save: save1, load: load1 },
    );
    s2.restorePending(load1());
    // No fresh `optimistic` call issued: the optimistic value must already
    // be visible on the fresh store.
    assert.equal(s2.state.n.value, 99);
    // Now fail the pre-crash authoritative; s1 must roll back atomically.
    gate.reject(new Error('pre-crash-fail'));
    await assert.rejects(() => p1, /pre-crash-fail/);
    assert.equal(s1.state.n.value, 1);
  });

  it('authoritative success clears the durable save slot (no replay of a completed mutation)', async () => {
    // ASSUMPTION (slot-clearing on success): the success continuation clears
    // the durable `save` slot through the injected seam, so `load()` reads
    // empty/cleared after authoritative success and a restart never replays
    // the completed mutation. Accepts null/empty/"null" tombstones.
    let saved = null;
    const save1 = (v) => { saved = v; };
    const load1 = () => saved;
    const s = createStore(
      () => ({ n: state(1) }),
      {},
      { schedule: (fn) => fn(), save: save1, load: load1 },
    );
    await s.dispatch.optimistic(
      'rename',
      (st) => { const prev = st.n.value; st.n.value = 42; return () => { st.n.value = prev; }; },
      () => Promise.resolve(),
    );
    assert.equal(s.state.n.value, 42);
    const slot = load1();
    assert.ok(
      slot === null || slot === undefined || slot === '' || slot === 'null',
      'expected durable slot to be cleared after success, got: ' + String(slot),
    );
  });

  it('save, fresh store, restorePending, failed authoritative → deep-equal restore, single rollback flush', async () => {
    let saved = null;
    const save1 = (v) => { saved = v; };
    const load1 = () => saved;
    const patch = (st) => { st.n.value = 99; return () => { st.n.value = 1; }; };
    const s1 = createStore(
      () => ({ n: state(1) }),
      {},
      { schedule: (fn) => fn(), save: save1, load: load1 },
    );
    const before = snapshot(s1.state);
    const gate = deferred();
    const p1 = s1.dispatch.optimistic('rename', patch, () => gate.promise);
    // Avoid unhandled rejection: s1 crashes before settle; fail it after restore.
    p1.catch(() => {});
    assert.equal(s1.state.n.value, 99);
    assert.equal(typeof s1.serializePending, 'function');
    const raw = s1.serializePending();
    // Simulate the LIFE-SAVE boundary: persist the pending intent, then die.
    // Crash-before-settle: NEVER resolve success on s1 in this flow.
    save1(raw);

    // Fresh store after "restart": base state restored from the snapshot seam.
    const s2 = createStore(
      () => ({ n: state(1) }),
      {},
      { schedule: (fn) => fn(), save: save1, load: load1 },
    );
    assert.equal(typeof s2.restorePending, 'function');
    s2.restorePending(load1());
    // Restored intent is live on the fresh store before any fresh call.
    assert.equal(s2.state.n.value, 99);
    // Fail the pre-crash authoritative AFTER restore (crash-before-settle);
    // s1 rolls back atomically and never succeeds.
    gate.reject(new Error('pre-crash-fail'));
    await assert.rejects(() => p1, /pre-crash-fail/);
    assert.deepEqual(snapshot(s1.state), before);
    const counter = countFlushes();
    try {
      await assert.rejects(
        () => s2.dispatch.optimistic('rename', patch, () => Promise.reject(new Error('post-restart-fail'))),
        /post-restart-fail/,
      );
      assert.deepEqual(snapshot(s2.state), before);
      assert.equal(counter.count, 2);
    } finally {
      counter.restore();
    }
    const reparsed = JSON.parse(saved);
    assert.equal(typeof reparsed.idempotencyKey, 'string');
    assert.ok(reparsed.idempotencyKey.length > 0);
  });
});
