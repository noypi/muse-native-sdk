'use strict';
// T1 — per-destination back-callback registry (FR-LIFECYCLE-007, AC-LIFECYCLE-017).
// Plan Interface (verbatim):
// `createBackDispatcher(hub): { register(destinationId: string, callback: Function): { unregister(): void }, handleBack(eventId: string): { consumed: boolean, destinationId: string|null, wentHome: boolean } }` — errors: `BACK_DUPLICATE_DESTINATION` on double-register same destinationId; `BACK_UNKNOWN_EVENT` shape `{ code: string }` never thrown for unconsumed back (returns `{ consumed: false, wentHome: true }` instead)
// Spec refs: FR-LIFECYCLE-007, AC-LIFECYCLE-017 (required dep: FR-LIFECYCLE-001).
// Hermetic: node:test + node:assert/strict only; hub collaborator is a
// hand-written in-memory fake injected through the public boundary; no
// network/fs/secrets/live services.
// A3: state_machine exact-once semantics and observer hub are correct and unchanged.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const { createBackDispatcher } = require('../src/back_dispatcher.js');

function createFakeHub() {
  const calls = [];
  return {
    calls,
    dispatch(event) {
      calls.push(event);
      return { applied: true };
    },
  };
}

describe('AC-LIFECYCLE-017 — register/handleBack stack order (LIFO)', () => {
  let hub;
  let dispatcher;
  beforeEach(() => {
    hub = createFakeHub();
    dispatcher = createBackDispatcher(hub);
  });

  it('register A,B,C then handleBack consumes C then B then A then wentHome', () => {
    const seen = [];
    dispatcher.register('A', () => seen.push('A'));
    dispatcher.register('B', () => seen.push('B'));
    dispatcher.register('C', () => seen.push('C'));
    let r = dispatcher.handleBack('e1');
    assert.deepEqual(r, { consumed: true, destinationId: 'C', wentHome: false });
    assert.deepEqual(seen, ['C']);
    r = dispatcher.handleBack('e2');
    assert.deepEqual(r, { consumed: true, destinationId: 'B', wentHome: false });
    r = dispatcher.handleBack('e3');
    assert.deepEqual(r, { consumed: true, destinationId: 'A', wentHome: false });
    assert.deepEqual(seen, ['C', 'B', 'A']);
    r = dispatcher.handleBack('e4');
    assert.deepEqual(r, { consumed: false, destinationId: null, wentHome: true });
  });

  it('handleBack invokes the top callback exactly once per event', () => {
    let calls = 0;
    dispatcher.register('A', () => { calls += 1; });
    const r = dispatcher.handleBack('e1');
    assert.equal(r.consumed, true);
    assert.equal(r.destinationId, 'A');
    assert.equal(calls, 1);
  });

  it('empty stack returns unconsumed wentHome instead of throwing BACK_UNKNOWN_EVENT', () => {
    const r = dispatcher.handleBack('e1');
    assert.deepEqual(r, { consumed: false, destinationId: null, wentHome: true });
  });
});

describe('AC-LIFECYCLE-017 — dispose-pop reroutes correctly', () => {
  it('unregister of the middle entry reroutes to the new top', () => {
    const hub = createFakeHub();
    const dispatcher = createBackDispatcher(hub);
    const seen = [];
    dispatcher.register('A', () => seen.push('A'));
    const unB = dispatcher.register('B', () => seen.push('B'));
    dispatcher.register('C', () => seen.push('C'));
    unB.unregister();
    const r = dispatcher.handleBack('e1');
    assert.deepEqual(r, { consumed: true, destinationId: 'C', wentHome: false });
    const r2 = dispatcher.handleBack('e2');
    assert.deepEqual(r2, { consumed: true, destinationId: 'A', wentHome: false });
    assert.deepEqual(seen, ['C', 'A']);
  });

  it('unregister of the top entry exposes the entry below', () => {
    const hub = createFakeHub();
    const dispatcher = createBackDispatcher(hub);
    dispatcher.register('A', () => {});
    const unB = dispatcher.register('B', () => {});
    unB.unregister();
    const r = dispatcher.handleBack('e1');
    assert.deepEqual(r, { consumed: true, destinationId: 'A', wentHome: false });
  });

  it('double-unregister is a no-op (ASSUMPTION A1: plan pins duplicate-register throw but is silent on double-unregister; most defensible is safe no-op)', () => {
    const hub = createFakeHub();
    const dispatcher = createBackDispatcher(hub);
    const un = dispatcher.register('A', () => {});
    un.unregister();
    un.unregister();
    const r = dispatcher.handleBack('e1');
    assert.deepEqual(r, { consumed: false, destinationId: null, wentHome: true });
  });
});

describe('AC-LIFECYCLE-017 — duplicate registration throws BACK_DUPLICATE_DESTINATION', () => {
  it('double-register of the same destinationId throws BACK_DUPLICATE_DESTINATION', () => {
    const hub = createFakeHub();
    const dispatcher = createBackDispatcher(hub);
    dispatcher.register('A', () => {});
    assert.throws(
      () => dispatcher.register('A', () => {}),
      (err) => {
        assert.equal(err && err.code, 'BACK_DUPLICATE_DESTINATION');
        return true;
      }
    );
  });

  it('re-register after unregister succeeds (slot freed)', () => {
    // ASSUMPTION A3: plan is silent on re-register after dispose-pop; most
    // defensible is that unregister frees the destinationId slot.
    const hub = createFakeHub();
    const dispatcher = createBackDispatcher(hub);
    const un = dispatcher.register('A', () => {});
    un.unregister();
    dispatcher.register('A', () => {});
    const r = dispatcher.handleBack('e1');
    assert.deepEqual(r, { consumed: true, destinationId: 'A', wentHome: false });
  });
});

describe('AC-LIFECYCLE-017 — input validation (security coverage)', () => {
  it('non-string destinationId is rejected', () => {
    const hub = createFakeHub();
    const dispatcher = createBackDispatcher(hub);
    assert.throws(() => dispatcher.register(42, () => {}));
    assert.throws(() => dispatcher.register(null, () => {}));
    assert.throws(() => dispatcher.register('', () => {}));
  });

  it('non-function callback is rejected', () => {
    const hub = createFakeHub();
    const dispatcher = createBackDispatcher(hub);
    assert.throws(() => dispatcher.register('A', null));
    assert.throws(() => dispatcher.register('A', 'not-a-function'));
  });

  it('non-string eventId is rejected', () => {
    const hub = createFakeHub();
    const dispatcher = createBackDispatcher(hub);
    dispatcher.register('A', () => {});
    assert.throws(() => dispatcher.handleBack(null));
    assert.throws(() => dispatcher.handleBack(42));
  });

  it('register returns an unregister handle', () => {
    const hub = createFakeHub();
    const dispatcher = createBackDispatcher(hub);
    const handle = dispatcher.register('A', () => {});
    assert.equal(typeof handle.unregister, 'function');
  });
});
