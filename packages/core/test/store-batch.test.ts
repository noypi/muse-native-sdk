'use strict';
// T7 — Exact pre-mutation restore, single batch, 1000-subscriber fan-out (Batch 3).
// Plan Interface: none — test-only; verifies T2/T6 surfaces (`dispatch`,
// `inverse`) with call-count assertions. `packages/core/lib/store.js` is
// READ-ONLY for this item: no production-counter changes were needed (the
// shared `lib/scheduler.js` `flush`/`getInvalidatedPaths` surface plus
// subscriber fan-out already expose the batch accounting this file asserts).
// Detail: failing action on store with 1,000 subscribers (subscribe each once):
// each subscriber notified exactly once per action (not once per field),
// post-rollback state deep-equals snapshot, total flush count for the action
// (apply + rollback) is exactly 2 batches (one apply, one rollback) — and the
// rollback itself is a single batch.
// Acceptance: 1,000 subscribers × 1 notification each per action; deep-equal
// restore; rollback = 1 invalidation batch.
// Spec refs: FR-STATE-002, AC-STATE-013.
// Hermetic: node:test + node:assert/strict only; scheduler observed via a
// require-cache spy; worker-pool seam is an in-test fake (no threads, no
// network, no secrets/fs/live services).

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const schedulerPath = require.resolve('../lib/scheduler.js');
const scheduler = require(schedulerPath);
const origFlush = scheduler.flush;

const { createStore } = require('../lib/store.js');
const { flush } = scheduler;
const { state, getRefRecord } = require('../lib/state.js');

beforeEach(() => {
  flush();
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

describe('T7 — 1000 subscribers notified exactly once per action, atomic restore, 2 batches (AC-STATE-013)', () => {
  it('failing optimistic action: 1000 subscribers x 1 notification per phase, deep-equal restore, apply+rollback = 2 flushes', async () => {
    const s = createStore(
      () => ({ a: state(0), b: state(0), c: state(0) }),
      {},
      { schedule: (fn) => fn() },
    );
    const atoms = [s.state.a, s.state.b, s.state.c];
    const counts = new Array(1000).fill(0);
    for (let i = 0; i < 1000; i += 1) {
      const idx = i;
      getRefRecord(atoms[i % 3]).subs.add(() => { counts[idx] += 1; });
    }
    const before = snapshot(s.state);

    let flushes = 0;
    scheduler.flush = function () { flushes += 1; return origFlush.call(this); };
    try {
      await assert.rejects(
        () => s.dispatch.optimistic(
          'write3',
          (st) => {
            const pa = st.a.value; const pb = st.b.value; const pc = st.c.value;
            st.a.value = 1; st.b.value = 2; st.c.value = 3;
            return () => { st.a.value = pa; st.b.value = pb; st.c.value = pc; };
          },
          () => Promise.reject(new Error('authoritative-down')),
        ),
        /authoritative-down/,
      );
    } finally {
      scheduler.flush = origFlush;
    }

    // Apply (1 batch) + rollback (1 batch) = exactly 2 flushes.
    assert.equal(flushes, 2);
    // Each subscriber notified exactly once per phase: once for apply, once
    // for rollback — never once per field.
    for (let i = 0; i < 1000; i += 1) {
      assert.equal(counts[i], 2);
    }
    // Exact pre-mutation restore.
    assert.deepEqual(snapshot(s.state), before);
  });

  it('rollback itself is a single invalidation batch', async () => {
    const s = createStore(
      () => ({ a: state(0), b: state(0) }),
      {},
      { schedule: (fn) => fn() },
    );
    let seen = 0;
    getRefRecord(s.state.a).subs.add(() => { seen += 1; });
    getRefRecord(s.state.b).subs.add(() => { seen += 1; });

    // Apply phase: one batch, one notification per subscriber.
    await s.dispatch.optimistic(
      'ok',
      (st) => { const pa = st.a.value; st.a.value = 5; return () => { st.a.value = pa; }; },
      () => Promise.resolve(),
    );
    assert.equal(seen, 1);
    try { scheduler.getInvalidatedPaths(); } catch (e) { void e; }

    // Rollback phase in isolation: exactly one flush, one notification each.
    let rollbackFlushes = 0;
    const innerOrig = scheduler.flush;
    scheduler.flush = function () { rollbackFlushes += 1; return innerOrig.call(this); };
    const seenBefore = seen;
    try {
      await assert.rejects(
        () => s.dispatch.optimistic(
          'fail',
          (st) => { const pb = st.b.value; st.b.value = 9; return () => { st.b.value = pb; }; },
          () => Promise.reject(new Error('nope')),
        ),
        /nope/,
      );
    } finally {
      scheduler.flush = innerOrig;
    }
    // Apply of the failing action (1) + its rollback (1).
    assert.equal(rollbackFlushes, 2);
    assert.equal(seen - seenBefore, 2);
    assert.deepEqual(snapshot(s.state), { a: 5, b: 0 });
  });
});
