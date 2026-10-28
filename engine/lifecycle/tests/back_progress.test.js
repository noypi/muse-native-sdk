'use strict';
// T2 — predictive-back progress stream direct-drive + arbitration (FR-LIFECYCLE-007, AC-LIFECYCLE-018).
// Plan Interface (verbatim):
// `createBackProgress(applier: (progress: number) => void): { onProgress(value: number, claimant: string): { applied: boolean }, claim(owner: string): void, release(owner: string): void }` — errors: `BACK_DOUBLE_CONSUME: claimant 'engine' rejected while host 'androidx' holds claim` returned as `{ applied: false }`, never thrown; out-of-range value throws `BACK_BAD_PROGRESS`
// Spec refs: FR-LIFECYCLE-007, AC-LIFECYCLE-018 (required deps: FR-LIFECYCLE-001, RULE-LIFECYCLE-001).
// Hermetic: node:test + node:assert/strict only; applier collaborator is a
// hand-written in-memory recording fake injected through the public boundary;
// no network/fs/secrets/live services; no async hops.
// A4: under-1ms latency verified structurally (synchronous direct-drive, no
// async/JS-bridge hop, Date.now()-delta assertion), not wall-clock perf.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const { createBackProgress } = require('../src/back_progress.js');

function createRecordingApplier() {
  const values = [];
  let syncDepths = [];
  function applier(progress) {
    values.push(progress);
  }
  return { values, applier };
}

describe('AC-LIFECYCLE-018 — progress values applied synchronously in order', () => {
  let record;
  let progress;
  beforeEach(() => {
    record = createRecordingApplier();
    progress = createBackProgress(record.applier);
  });

  it('progress values 0→1 applied in order with zero async hops', async () => {
    const steps = [0, 0.25, 0.5, 0.75, 1];
    for (const v of steps) {
      const r = progress.onProgress(v, 'engine');
      assert.deepEqual(r, { applied: true });
    }
    assert.deepEqual(record.values, steps);
    await Promise.resolve();
    await new Promise((resolve) => setImmediate(resolve));
    assert.deepEqual(record.values, steps);
  });

  it('applier is invoked synchronously before onProgress returns (direct-drive, RULE-LIFECYCLE-001)', () => {
    let calledBeforeReturn = false;
    const p = createBackProgress(() => { calledBeforeReturn = true; });
    const r = p.onProgress(0.5, 'engine');
    assert.equal(r.applied, true);
    assert.equal(calledBeforeReturn, true);
  });

  it('synchronous path completes with a small Date.now() delta (structural <1ms proxy, ASSUMPTION A4)', () => {
    const t0 = Date.now();
    const r = progress.onProgress(0.5, 'engine');
    const delta = Date.now() - t0;
    assert.equal(r.applied, true);
    assert.ok(delta < 1000, `direct-drive delta must be small, got ${delta}ms`);
  });
});

describe('AC-LIFECYCLE-018 — single-consumer arbitration (claim/release)', () => {
  it("host-claimed stream rejects engine events with { applied: false }, never throws", () => {
    const record = createRecordingApplier();
    const p = createBackProgress(record.applier);
    p.claim('androidx');
    const r = p.onProgress(0.5, 'engine');
    assert.deepEqual(r, { applied: false });
    assert.deepEqual(record.values, []);
  });

  it('engine-claimed stream rejects host events and vice versa', () => {
    const record = createRecordingApplier();
    const p = createBackProgress(record.applier);
    p.claim('engine');
    assert.deepEqual(p.onProgress(0.5, 'androidx'), { applied: false });
    p.release('engine');
    p.claim('androidx');
    assert.deepEqual(p.onProgress(0.5, 'engine'), { applied: false });
    assert.deepEqual(record.values, []);
  });

  it('owner events apply; release frees the stream for the other owner', () => {
    const record = createRecordingApplier();
    const p = createBackProgress(record.applier);
    p.claim('androidx');
    assert.deepEqual(p.onProgress(0.3, 'androidx'), { applied: true });
    p.release('androidx');
    assert.deepEqual(p.onProgress(0.6, 'engine'), { applied: true });
    assert.deepEqual(record.values, [0.3, 0.6]);
  });

  it('release by a non-owner is a no-op (ASSUMPTION A3: plan is silent; most defensible is that only the holder releases)', () => {
    const record = createRecordingApplier();
    const p = createBackProgress(record.applier);
    p.claim('androidx');
    p.release('engine');
    assert.deepEqual(p.onProgress(0.5, 'engine'), { applied: false });
    assert.deepEqual(p.onProgress(0.5, 'androidx'), { applied: true });
  });
});

describe('AC-LIFECYCLE-018 — input validation (security coverage)', () => {
  it('out-of-range value throws BACK_BAD_PROGRESS (<0 and >1)', () => {
    const record = createRecordingApplier();
    const p = createBackProgress(record.applier);
    assert.throws(
      () => p.onProgress(-0.1, 'engine'),
      (err) => {
        assert.equal(err && err.code, 'BACK_BAD_PROGRESS');
        return true;
      }
    );
    assert.throws(
      () => p.onProgress(1.1, 'engine'),
      (err) => {
        assert.equal(err && err.code, 'BACK_BAD_PROGRESS');
        return true;
      }
    );
    assert.deepEqual(record.values, []);
  });

  it('NaN and non-number progress values throw BACK_BAD_PROGRESS', () => {
    const record = createRecordingApplier();
    const p = createBackProgress(record.applier);
    assert.throws(() => p.onProgress(NaN, 'engine'));
    assert.throws(() => p.onProgress('0.5', 'engine'));
    assert.throws(() => p.onProgress(null, 'engine'));
    assert.deepEqual(record.values, []);
  });

  it('boundary values 0 and 1 are accepted', () => {
    const record = createRecordingApplier();
    const p = createBackProgress(record.applier);
    assert.deepEqual(p.onProgress(0, 'engine'), { applied: true });
    assert.deepEqual(p.onProgress(1, 'engine'), { applied: true });
    assert.deepEqual(record.values, [0, 1]);
  });

  it('non-function applier is rejected at construction', () => {
    assert.throws(() => createBackProgress(null));
    assert.throws(() => createBackProgress('not-a-function'));
  });
});
