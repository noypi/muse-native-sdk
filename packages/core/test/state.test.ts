'use strict';
// T4 — state/reactive/readonly/markRaw contract (Batch 2).
// Plan Interface (verbatim):
// `state<T>(value: T, opts?: { deep?: boolean }): { value: T }` — `deep`
// defaults `false`; `reactive<T extends object>(obj: T): T` — deep by default;
// `readonly<T>(value: T): T` — any write through the view throws
// `MUSE_CORE_READONLY_WRITE`; `markRaw<T>(value: T): T` — opts the value (and
// subtree) out of conversion. Identical-value (`Object.is`) writes are no-ops:
// no `schedule`, no flush, no notification.
// Spec refs: AC-MCORE-003, AC-MCORE-004, AC-MCORE-005
// (TC-MCORE-003/004/005 → AC-MCORE-003/004/005).
// Hermetic: node:test + node:assert/strict only; scheduler observed via a
// require-cache spy (no network/secrets/fs/live services). Usable outside a
// component: every case runs at top level with no instance.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

// Observe write-path notification through the single shared scheduler (A4):
// patch the scheduler's `schedule` BEFORE loading the state module so the
// state's destructured reference sees the spy.
const schedulerPath = require.resolve('../lib/scheduler.js');
const scheduler = require(schedulerPath);
let scheduleCalls = 0;
const origSchedule = scheduler.schedule;
scheduler.schedule = function (job) { scheduleCalls += 1; return origSchedule.call(this, job); };

const { state, reactive, readonly, markRaw } = require('../lib/state.js');
const { flush } = scheduler;

beforeEach(() => {
  scheduleCalls = 0;
  flush();
  scheduleCalls = 0;
});

describe('TC-MCORE-003 — { value } reads/writes + flush notification (AC-MCORE-003)', () => {
  it('state returns a { value } holder readable and writable outside a component', () => {
    const s = state(1);
    assert.equal(s.value, 1);
    s.value = 2;
    assert.equal(s.value, 2);
  });

  it('a changed write schedules exactly one notification drained by flush', () => {
    const s = state(0);
    scheduleCalls = 0;
    s.value = 1;
    assert.equal(scheduleCalls, 1);
    assert.doesNotThrow(() => { flush(); });
    assert.equal(s.value, 1);
  });

  it('identical-value (Object.is) writes are no-ops: no schedule, no flush, no notification', () => {
    const s = state(1);
    flush(); scheduleCalls = 0;
    s.value = 1;
    assert.equal(scheduleCalls, 0);
    s.value = NaN;
    assert.equal(scheduleCalls, 1); // NaN -> NaN is a change only the first time
    flush(); scheduleCalls = 0;
    s.value = NaN;
    assert.ok(Object.is(s.value, NaN));
    assert.equal(scheduleCalls, 0);
  });

  it('identical object-identity write is a no-op; a new identity notifies', () => {
    const obj = { a: 1 };
    const s = state(obj);
    flush(); scheduleCalls = 0;
    s.value = obj;
    assert.equal(scheduleCalls, 0);
    s.value = { a: 1 };
    assert.equal(scheduleCalls, 1);
  });
});

describe('TC-MCORE-004 — deep defaults false vs deep true nested mutation (AC-MCORE-004)', () => {
  it('non-deep state (default) ignores nested mutation: no schedule', () => {
    const s = state({ nested: { x: 1 } });
    flush(); scheduleCalls = 0;
    s.value.nested.x = 2;
    assert.equal(s.value.nested.x, 2);
    assert.equal(scheduleCalls, 0);
  });

  it('explicit { deep: false } behaves like the default: nested mutation notifies nothing', () => {
    const s = state({ nested: { x: 1 } }, { deep: false });
    flush(); scheduleCalls = 0;
    s.value.nested = { x: 99 };
    assert.equal(s.value.nested.x, 99);
    assert.equal(scheduleCalls, 0);
  });

  it('state(obj, { deep: true }) observes nested mutation: schedules notification', () => {
    const s = state({ nested: { x: 1 } }, { deep: true });
    flush(); scheduleCalls = 0;
    s.value.nested.x = 2;
    assert.equal(scheduleCalls, 1);
    flush();
    assert.equal(s.value.nested.x, 2);
  });

  it('deep state identical nested write is a no-op (Object.is)', () => {
    const s = state({ nested: { x: 1 } }, { deep: true });
    flush(); scheduleCalls = 0;
    s.value.nested.x = 1;
    assert.equal(scheduleCalls, 0);
  });
});

describe('TC-MCORE-005 — reactive-deep default, readonly-write throws, markRaw never notifies (AC-MCORE-005)', () => {
  it('reactive observes nested mutation by default', () => {
    const r = reactive({ nested: { x: 1 } });
    flush(); scheduleCalls = 0;
    r.nested.x = 5;
    assert.equal(scheduleCalls, 1);
    flush();
    assert.equal(r.nested.x, 5);
  });

  it('reactive identical nested write is a no-op', () => {
    const r = reactive({ count: 1 });
    flush(); scheduleCalls = 0;
    r.count = 1;
    assert.equal(scheduleCalls, 0);
  });

  it('any write through a readonly view throws MUSE_CORE_READONLY_WRITE', () => {
    const src = state({ count: 1 }, { deep: true });
    const ro = readonly(src);
    assert.throws(() => { ro.value = 2; }, /MUSE_CORE_READONLY_WRITE/);
    assert.throws(() => { ro.value.count = 2; }, /MUSE_CORE_READONLY_WRITE/);
    assert.equal(src.value.count, 1);
  });

  it('readonly view reflects source writes but grants no writes itself', () => {
    const src = state(10);
    const ro = readonly(src);
    assert.equal(ro.value, 10);
    src.value = 11;
    flush();
    assert.equal(ro.value, 11);
    assert.throws(() => { ro.value = 12; }, /MUSE_CORE_READONLY_WRITE/);
  });

  it('delete ro.value on a holder readonly view throws TypeError (non-configurable accessor)', () => {
    // Pin Stage 3 MINOR fix (configurable: false): delete on the holder view
    // surfaces as a strict-mode TypeError, NOT MUSE_CORE_READONLY_WRITE.
    // The named-error contract covers set-paths; delete is pinned separately.
    const src = state(10);
    const ro = readonly(src);
    assert.throws(() => { delete ro.value; }, TypeError);
    assert.equal(ro.value, 10);
  });

  it('markRaw opts the value (and subtree) out of conversion: nested writes never notify', () => {
    const raw = markRaw({ nested: { x: 1 } });
    const s = state({ inner: raw }, { deep: true });
    flush(); scheduleCalls = 0;
    raw.nested.x = 2;
    assert.equal(scheduleCalls, 0);
    s.value.inner.nested.x = 3;
    assert.equal(scheduleCalls, 0);
    flush();
    assert.equal(raw.nested.x, 3);
  });

  it('markRaw returns the identical value (no wrapping copy)', () => {
    const obj = { a: 1 };
    assert.equal(markRaw(obj), obj);
  });
});
