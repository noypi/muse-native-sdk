'use strict';
// T2 — scheduler/flush module contract (Batch 1).
// Plan Interface (verbatim): `schedule(job: () => void): void` — enqueue a job
// for the next flush; `flush(): void` — run the queue once, each job at most
// once per flush; `batch(fn: () => void): void` — run `fn` with flush deferred
// until `fn` returns (nested `batch` collapses to the outermost).
// Identical-value writes never call `schedule` (no-op at the write path in T4).
// Spec refs: AC-MCORE-009, AC-MCORE-010 (scheduler half), RULE-MCORE-003.
// Hermetic: node:test + node:assert/strict only, no network/secrets/fs/live services.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { schedule, flush, batch } = require('../lib/scheduler.js');

describe('T2 — schedule/flush basic semantics', () => {
  it('schedule enqueues a job that does not run until flush', () => {
    let ran = 0;
    schedule(() => { ran += 1; });
    assert.equal(ran, 0);
    flush();
    assert.equal(ran, 1);
  });

  it('flush runs each queued job once and drains the queue', () => {
    let a = 0;
    let b = 0;
    schedule(() => { a += 1; });
    schedule(() => { b += 1; });
    flush();
    assert.equal(a, 1);
    assert.equal(b, 1);
    // Second flush with nothing queued runs nothing and does not throw.
    flush();
    assert.equal(a, 1);
    assert.equal(b, 1);
  });

  it('flush with an empty queue is a no-op', () => {
    assert.doesNotThrow(() => { flush(); });
  });

  it('scheduling the same job fn three times in one flush runs it once (AC-MCORE-009 coalescing analogue)', () => {
    let runs = 0;
    const job = () => { runs += 1; };
    schedule(job);
    schedule(job);
    schedule(job);
    flush();
    assert.equal(runs, 1);
  });

  it('three writes across two states produce one flush with each job once', () => {
    // Scheduler-observable half of AC-MCORE-009: three schedule calls for two
    // logical states collapse to one flush, each distinct job once.
    const seen = [];
    const jobA = () => { seen.push('A'); };
    const jobB = () => { seen.push('B'); };
    schedule(jobA);
    schedule(jobB);
    schedule(jobA);
    flush();
    assert.deepEqual(seen.sort(), ['A', 'B']);
  });

  it('a job scheduled during a flush runs in the same flush if not yet visited', () => {
    const order = [];
    schedule(() => {
      order.push('first');
      schedule(() => { order.push('second'); });
    });
    flush();
    assert.deepEqual(order, ['first', 'second']);
  });

  it('a throwing job does not prevent remaining jobs from running (ASSUMPTION)', () => {
    // ASSUMPTION: flush continues past a throwing job; the error is captured
    // and rethrown (or aggregated) after the queue drains. Minimal assertion:
    // the second job still ran. If the coder surfaces errors differently,
    // this case is reconciled without changing the coalescing contract.
    let secondRan = false;
    schedule(() => { throw new Error('job boom'); });
    schedule(() => { secondRan = true; });
    assert.throws(() => { flush(); }, /job boom/);
    assert.equal(secondRan, true);
  });
});

describe('T2 — batch defers flush until fn returns', () => {
  it('jobs scheduled inside batch do not run until batch returns, then run once', () => {
    let runs = 0;
    batch(() => {
      schedule(() => { runs += 1; });
      schedule(() => { runs += 1; });
      assert.equal(runs, 0);
    });
    assert.equal(runs, 2);
  });

  it('nested batch collapses to the outermost (inner exit does not flush)', () => {
    let runs = 0;
    batch(() => {
      schedule(() => { runs += 1; });
      batch(() => {
        schedule(() => { runs += 1; });
        assert.equal(runs, 0);
      });
      assert.equal(runs, 0);
    });
    assert.equal(runs, 2);
  });

  it('batch propagates fn errors to the caller (ASSUMPTION)', () => {
    // ASSUMPTION: batch does not swallow fn exceptions; deferred jobs still
    // flush. Plan/spec do not settle error propagation through batch.
    let runs = 0;
    assert.throws(() => {
      batch(() => {
        schedule(() => { runs += 1; });
        throw new Error('batch boom');
      });
    }, /batch boom/);
    assert.equal(runs, 1);
  });
});

describe('T2 — reschedule-loop guard', () => {
  it('a job that reschedules itself forever triggers the bounded overflow error (ASSUMPTION: name matches /overflow/i)', () => {
    // Plan Detail: "guard against infinite reschedule loops with a bounded
    // pass count that throws the named scheduler-overflow error". Exact error
    // name is unsettled in spec; ASSUMPTION asserts only an /overflow/i match.
    const self = () => { schedule(self); };
    schedule(self);
    assert.throws(() => { flush(); }, /overflow/i);
  });
});
