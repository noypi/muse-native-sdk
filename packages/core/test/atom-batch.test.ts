'use strict';
// T1 — batched atom invalidation without global lock (Batch 1).
// Plan Interface (verbatim):
// `state(value:any, opts?:{deep?:boolean}): {value:any}` — reads call
// `trackRef(record)` against `getActiveConsumer()`; writes with
// `!Object.is(old,v)` call `schedule(record.notifyJob)` exactly once per
// mutation; `trackRef(record:{subs:Set,notifyJob:Function}): void`,
// `getRefRecord(handle:Object): {subs:Set,notifyJob:Function}|null`.
// Error/denial: `reactive(obj)` throws `TypeError` for non-objects (existing).
// Acceptance: 100 rapid writes → schedule exactly 100 times, subscriber once
// per flush; Object.is-equal → zero schedule; deep-proxy nested set → one
// schedule.
// Spec refs: FR-STATE-001, AC-STATE-006.
// Hermetic: node:test + node:assert/strict only; scheduler observed via a
// require-cache spy (no network/secrets/fs/live services).

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

// Observe the write path through the single shared scheduler: patch the
// scheduler's `schedule` BEFORE loading the state module so the state's
// reference sees the spy.
const schedulerPath = require.resolve('../lib/scheduler.js');
const scheduler = require(schedulerPath);
let scheduleCalls = 0;
const origSchedule = scheduler.schedule;
scheduler.schedule = function (job) { scheduleCalls += 1; return origSchedule.call(this, job); };

const {
  state,
  reactive,
  trackRef,
  getRefRecord,
  getActiveConsumer,
  pushActiveConsumer,
  popActiveConsumer,
} = require('../lib/state.js');
const { flush } = scheduler;

beforeEach(() => {
  scheduleCalls = 0;
  flush();
  scheduleCalls = 0;
});

describe('T1 — reads track via trackRef/getActiveConsumer, writes schedule once (AC-STATE-006)', () => {
  it('a read registers the active consumer on the record subs set', () => {
    const s = state(1);
    const consumer = { deps: new Set() };
    pushActiveConsumer(consumer);
    try {
      void s.value;
    } finally {
      popActiveConsumer();
    }
    const record = getRefRecord(s);
    assert.ok(record);
    assert.ok(record.subs.has(consumer));
  });

  it('no active consumer outside a tracked run: getActiveConsumer() is null and reads do not throw', () => {
    assert.equal(getActiveConsumer(), null);
    const s = state(5);
    assert.equal(s.value, 5);
  });

  it('trackRef(record) adds the consumer; getRefRecord(handle) exposes {subs, notifyJob}', () => {
    const s = state(0);
    const record = getRefRecord(s);
    assert.ok(record.subs instanceof Set);
    assert.equal(typeof record.notifyJob, 'function');
    const consumer = { deps: new Set() };
    pushActiveConsumer(consumer);
    try {
      trackRef(record);
    } finally {
      popActiveConsumer();
    }
    assert.ok(record.subs.has(consumer));
  });

  it('getRefRecord returns null for a non-holder object', () => {
    assert.equal(getRefRecord({}), null);
    assert.equal(getRefRecord(Object.create(null)), null);
  });

  it('reactive(obj) throws TypeError for non-objects', () => {
    assert.throws(() => reactive(1), TypeError);
    assert.throws(() => reactive('x'), TypeError);
    assert.throws(() => reactive(null), TypeError);
    assert.throws(() => reactive(undefined), TypeError);
  });
});

describe('T1 — 100 rapid writes schedule 100 times, subscriber notified once per flush (AC-STATE-006)', () => {
  it('100 rapid writes to one atom call schedule exactly 100 times and notify the subscriber once per flush', () => {
    const s = state(0);
    let notifications = 0;
    const record = getRefRecord(s);
    record.subs.add(() => { notifications += 1; });
    flush(); scheduleCalls = 0; notifications = 0;
    for (let i = 1; i <= 100; i += 1) s.value = i;
    assert.equal(scheduleCalls, 100);
    assert.equal(notifications, 0);
    flush();
    assert.equal(notifications, 1);
    assert.equal(s.value, 100);
  });

  it('subscriber added mid-flush is not visited twice in the same flush', () => {
    const s = state(0);
    const record = getRefRecord(s);
    let late = 0;
    record.subs.add(() => { record.subs.add(() => { late += 1; }); });
    s.value = 1;
    flush();
    assert.equal(late, 0);
    s.value = 2;
    flush();
    assert.equal(late, 1);
  });

  it('interleaved async writes each schedule exactly once', async () => {
    const s = state(0);
    flush(); scheduleCalls = 0;
    await Promise.all([1, 2, 3].map((v) => Promise.resolve().then(() => { s.value = v; })));
    assert.equal(scheduleCalls, 3);
    flush();
  });
});

describe('T1 — Object.is-equal writes are no-ops; deep nested set schedules once (AC-STATE-006)', () => {
  it('Object.is-equal write schedules zero times', () => {
    const s = state(1);
    flush(); scheduleCalls = 0;
    s.value = 1;
    assert.equal(scheduleCalls, 0);
    s.value = NaN;
    assert.equal(scheduleCalls, 1);
    flush(); scheduleCalls = 0;
    s.value = NaN;
    assert.equal(scheduleCalls, 0);
  });

  it('same object identity write is a no-op; new identity schedules once', () => {
    const obj = { a: 1 };
    const s = state(obj);
    flush(); scheduleCalls = 0;
    s.value = obj;
    assert.equal(scheduleCalls, 0);
    s.value = { a: 1 };
    assert.equal(scheduleCalls, 1);
  });

  it('deep-proxy nested set schedules exactly once', () => {
    const s = state({ nested: { x: 1 } }, { deep: true });
    flush(); scheduleCalls = 0;
    s.value.nested.x = 2;
    assert.equal(scheduleCalls, 1);
    flush();
    assert.equal(s.value.nested.x, 2);
  });

  it('deep-proxy identical nested write is a no-op (Object.is)', () => {
    const s = state({ nested: { x: 1 } }, { deep: true });
    flush(); scheduleCalls = 0;
    s.value.nested.x = 1;
    assert.equal(scheduleCalls, 0);
  });
});
