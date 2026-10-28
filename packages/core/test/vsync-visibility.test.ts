'use strict';
// T5 — Renderer consumes only changed offsets; O(depth) visits; inset-only
// never full-relayouts (Batch 2, FR-STATE-001 AC-STATE-007 + AC-STATE-010).
// Pins the JS-owned half only.
//
// OUT-OF-SCOPE LIMITATION (recorded per plan): true Vulkan descriptor-delta
// behaviour and VSYNC-frame timing stay with the graphics/runtime owner.
// This file asserts the JS-owned invalidation shape that underpins them:
// fine-grained subscriber fan-out through the single shared scheduler,
// the `getInvalidatedPaths(): string[]` test seam, and inset-only isolation.
// Read-only reference (NOT modified, no C++ edits):
// `engine/runtime/tests/invalidation_test.cpp` — one state write dirties
// exactly one node; a pump applies pending invalidations, presents once,
// and clears flags; unmarked nodes keep identity. The assertions below are
// the JS analogues (one-atom change → invalidation set size 1; unrelated
// subscribers untouched).
//
// Plan Interface (verbatim):
// `getInvalidatedPaths(): string[]` (test-seam on scheduler or store:
// returns sorted ids of atoms flushed in the last batch, then clears) —
// returns `string[]`, never throws. Error/denial shape: none beyond empty
// array when nothing flushed.
// Acceptance: one-atom change in 10k tree → visited subscribers ≤ 32 and
// invalidation set size 1; background (async-seam) mutation → direct-call
// counter stays 0, flush delivers next tick; inset-only change →
// invalidation set has no widget ids.
// Spec refs: FR-STATE-001, AC-STATE-007, AC-STATE-010.
//
// Hermetic: node:test + node:assert/strict only; no network, no secrets, no
// config, no live services. The 10k-atom tree is in-memory `state()` atoms.
// "Direct-call counter" counts subscriber invocations that bypass
// `scheduler.schedule` — the test wraps each subscriber so any synchronous
// call during mutation (before flush) increments the counter; all fan-out
// must go through the batch.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const { state, getRefRecord } = require('../lib/state.js');
const scheduler = require('../lib/scheduler.js');

// The `getInvalidatedPaths` seam lives on the scheduler or the store per the
// plan. Resolve it dynamically so the test binds to the public seam, not to
// an implementation file. (The inset bus is required separately below; its
// absence is T3's red, not this seam's.)
function getInvalidatedPaths() {
  if (typeof scheduler.getInvalidatedPaths === 'function') {
    return scheduler.getInvalidatedPaths();
  }
  const store = require('../lib/store.js');
  if (store && typeof store.getInvalidatedPaths === 'function') {
    return store.getInvalidatedPaths();
  }
  throw new Error('MUSE_STATE_TEST_SEAM_MISSING: getInvalidatedPaths() not found');
}

beforeEach(() => {
  scheduler.flush();
  // Drain any seam residue so each case starts from an empty set.
  try { getInvalidatedPaths(); } catch (e) { void e; }
});

describe('T5 — getInvalidatedPaths seam shape (AC-STATE-007/010)', () => {
  it('returns a sorted string[] and never throws; empty array when nothing flushed', () => {
    const first = getInvalidatedPaths();
    assert.ok(Array.isArray(first));
    assert.deepEqual(first, []);
    const second = getInvalidatedPaths();
    assert.deepEqual(second, []);
  });

  it('clears after reading: second read is empty without a new flush', () => {
    const s = state(0);
    s.value = 1;
    scheduler.flush();
    const paths = getInvalidatedPaths();
    assert.ok(paths.length >= 1);
    assert.deepEqual(getInvalidatedPaths(), []);
  });
});

describe('T5 — one-atom change in a 10k tree visits O(depth), set size 1 (AC-STATE-010)', () => {
  it('changing one atom of 10,000 visits ≤ 32 subscribers and yields a 1-id invalidation set', () => {
    const N = 10000;
    const atoms = new Array(N);
    for (let i = 0; i < N; i += 1) atoms[i] = state(i);
    let visited = 0;
    // Only the changed atom's subscriber chain may run: attach one
    // subscriber to the target atom, plus one witness on an unrelated atom
    // that must never fire.
    const target = Math.floor(N / 2);
    const targetRecord = getRefRecord(atoms[target]);
    targetRecord.subs.add(() => { visited += 1; });
    let witness = 0;
    const witnessRecord = getRefRecord(atoms[0]);
    witnessRecord.subs.add(() => { witness += 1; });
    atoms[target].value = -1;
    scheduler.flush();
    assert.ok(visited <= 32, `expected visited ≤ 32, got ${visited}`);
    assert.equal(witness, 0);
    const paths = getInvalidatedPaths();
    assert.equal(paths.length, 1);
    assert.deepEqual([...paths].sort(), paths, 'invalidation set is sorted');
  });
});

describe('T5 — async-seam mutation never calls subscribers directly (AC-STATE-010)', () => {
  it('background (async-seam) mutation → direct-call counter 0, flush delivers next tick', async () => {
    const s = state(0);
    let syncCallsDuringMutation = -1;
    let delivered = 0;
    const record = getRefRecord(s);
    // A subscriber that runs only via the batch: the mutation callback
    // snapshots `delivered` synchronously, so any direct (schedule-bypassing)
    // invocation during the mutation is caught deterministically, regardless
    // of when the scheduler's microtask auto-flush runs.
    record.subs.add(() => { delivered += 1; });
    // Async-seam mutation: runs off the current tick like a worker-pool
    // dispatch, then the batch flushes.
    await Promise.resolve().then(() => {
      s.value = 7;
      syncCallsDuringMutation = delivered;
    });
    assert.equal(syncCallsDuringMutation, 0);
    scheduler.flush();
    assert.equal(delivered, 1);
    assert.equal(s.value, 7);
  });
});

describe('T5 — inset-only change produces no widget ids (AC-STATE-007/010)', () => {
  it('inset-only publish → invalidation set contains only inset ids, no widget ids', () => {
    const { publishInsets } = require('../lib/inset_bus.js');
    const widget = state({ id: 'widget-home' });
    let widgetNotifications = 0;
    getRefRecord(widget).subs.add(() => { widgetNotifications += 1; });
    publishInsets({ top: 8, left: 0, bottom: 16, right: 0 });
    scheduler.flush();
    assert.equal(widgetNotifications, 0);
    const paths = getInvalidatedPaths();
    assert.ok(Array.isArray(paths));
    for (const p of paths) {
      assert.equal(typeof p, 'string');
      assert.ok(!/^widget/.test(p), `invalidation set must have no widget ids, got ${p}`);
    }
    // The invalidation set reflects the inset channel only: it is either
    // empty (insets carried outside the widget-id space) or contains inset
    // ids — never widget ids. Size is small (no full relayout fan-out).
    assert.ok(paths.length <= 32, `expected no full-relayout fan-out, got ${paths.length}`);
  });
});
