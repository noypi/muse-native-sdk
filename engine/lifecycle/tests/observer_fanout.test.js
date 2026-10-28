'use strict';
// T2 — subsystem observer fan-out for every applied transition (Batch 2).
// Plan Interface (verbatim):
// `createObserverHub(machine: object): { subscribe(fn: (event: {seq: number, transition: string, state: string}) => void): () => void, dispatch(transition: string): { applied: boolean, state: string } }` — each applied transition calls every subscriber exactly once in subscribe order; a throwing subscriber does not block remaining subscribers (error collected to `hub.errors`).
// Spec refs: FR-LIFECYCLE-001, AC-LIFECYCLE-001 (TC-LIFECYCLE-001).
// Mirrors TC-LIFECYCLE-001 (transition_dispatch_test.cpp) fan-out clause.
// Hermetic: node:test + node:assert/strict only; machine collaborator is a
// hand-written in-memory fake injected through the public boundary; no
// network/fs/secrets/live services.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const { createObserverHub } = require('../src/observers.js');

// Minimal fake of the T1 machine boundary: dispatch returns applied:true for
// the next legal transition, applied:false for repeats, throws for unknown.
function createFakeMachine() {
  const calls = [];
  let state = 'init';
  let appliedCount = 0;
  const seen = new Set();
  return {
    calls,
    dispatch(transition) {
      calls.push(transition);
      if (typeof transition !== 'string' || transition === '' || transition === 'bogus') {
        const err = new Error(`LIFECYCLE_UNKNOWN_TRANSITION: ${String(transition)}`);
        err.code = 'LIFECYCLE_UNKNOWN_TRANSITION';
        throw err;
      }
      if (seen.has(transition)) {
        return { applied: false, state };
      }
      seen.add(transition);
      appliedCount += 1;
      state = `state-after-${transition}`;
      return { applied: true, state };
    },
    getState() {
      return state;
    },
    fakeAppliedCount() {
      return appliedCount;
    },
  };
}

describe('TC-LIFECYCLE-001 fan-out — applied transitions reach every subscriber once in order', () => {
  let machine;
  let hub;
  beforeEach(() => {
    machine = createFakeMachine();
    hub = createObserverHub(machine);
  });

  it('exposes subscribe/dispatch and an errors collection', () => {
    assert.equal(typeof hub.subscribe, 'function');
    assert.equal(typeof hub.dispatch, 'function');
    assert.ok(Array.isArray(hub.errors), 'hub.errors must be an array');
    assert.deepEqual(hub.errors, []);
  });

  it('three subscribers each receive every applied transition exactly once in subscribe order', () => {
    const order = [];
    const seen = [[], [], []];
    const unsubs = [0, 1, 2].map((i) =>
      hub.subscribe((event) => {
        order.push(i);
        seen[i].push(event);
      })
    );
    assert.equal(unsubs.length, 3);
    const result = hub.dispatch('create');
    assert.equal(result.applied, true);
    assert.deepEqual(order, [0, 1, 2]);
    for (const events of seen) {
      assert.equal(events.length, 1);
      assert.equal(events[0].transition, 'create');
      assert.equal(typeof events[0].seq, 'number');
      assert.equal(typeof events[0].state, 'string');
    }
    hub.dispatch('start');
    assert.deepEqual(order, [0, 1, 2, 0, 1, 2]);
    for (const events of seen) {
      assert.equal(events.length, 2);
    }
  });

  it('fan-out events carry monotonic seq numbers with transition and state', () => {
    const received = [];
    hub.subscribe((event) => received.push(event));
    hub.dispatch('create');
    hub.dispatch('start');
    assert.equal(received.length, 2);
    assert.equal(received[0].transition, 'create');
    assert.equal(received[1].transition, 'start');
    assert.ok(received[1].seq > received[0].seq, 'seq must increase per applied dispatch');
    assert.equal(typeof received[0].state, 'string');
  });

  it('dispatch return value forwards the machine result {applied, state}', () => {
    const applied = hub.dispatch('create');
    assert.deepEqual(Object.keys(applied).sort(), ['applied', 'state']);
    assert.equal(applied.applied, true);
    assert.equal(applied.state, machine.getState());
    const repeated = hub.dispatch('create');
    assert.deepEqual(repeated, { applied: false, state: machine.getState() });
  });

  it('dispatch forwards the transition argument to the wrapped machine', () => {
    hub.subscribe(() => {});
    hub.dispatch('create');
    assert.deepEqual(machine.calls, ['create']);
  });
});

describe('TC-LIFECYCLE-001 fan-out — unapplied dispatches notify nobody', () => {
  it('duplicate (unapplied) dispatch calls no subscriber', () => {
    const machine = createFakeMachine();
    const hub = createObserverHub(machine);
    let calls = 0;
    hub.subscribe(() => {
      calls += 1;
    });
    assert.equal(hub.dispatch('create').applied, true);
    assert.equal(calls, 1);
    const repeated = hub.dispatch('create');
    assert.equal(repeated.applied, false);
    assert.equal(calls, 1);
  });

  it('no subscriber registered means dispatch still applies to the machine without fan-out', () => {
    const machine = createFakeMachine();
    const hub = createObserverHub(machine);
    const result = hub.dispatch('create');
    assert.equal(result.applied, true);
    assert.deepEqual(machine.calls, ['create']);
  });
});

describe('TC-LIFECYCLE-001 fan-out — subscribe returns unsubscribe', () => {
  it('unsubscribe stops delivery to that subscriber only', () => {
    const machine = createFakeMachine();
    const hub = createObserverHub(machine);
    let first = 0;
    let second = 0;
    const unsubFirst = hub.subscribe(() => {
      first += 1;
    });
    hub.subscribe(() => {
      second += 1;
    });
    hub.dispatch('create');
    assert.equal(first, 1);
    assert.equal(second, 1);
    assert.equal(typeof unsubFirst, 'function');
    unsubFirst();
    hub.dispatch('start');
    assert.equal(first, 1);
    assert.equal(second, 2);
  });

  it('unsubscribe is idempotent: calling twice does not throw or disturb others', () => {
    // ASSUMPTION: plan is silent on double-unsubscribe; most defensible is a safe no-op.
    const machine = createFakeMachine();
    const hub = createObserverHub(machine);
    let calls = 0;
    const unsub = hub.subscribe(() => {
      calls += 1;
    });
    unsub();
    unsub();
    hub.dispatch('create');
    assert.equal(calls, 0);
  });
});

describe('TC-LIFECYCLE-001 fan-out — throwing subscriber does not block others', () => {
  it('a throwing subscriber does not prevent remaining subscribers; error collected to hub.errors', () => {
    const machine = createFakeMachine();
    const hub = createObserverHub(machine);
    const order = [];
    hub.subscribe(() => {
      order.push('thrower');
      throw new Error('boom');
    });
    hub.subscribe(() => {
      order.push('second');
    });
    hub.subscribe(() => {
      order.push('third');
    });
    const result = hub.dispatch('create');
    assert.equal(result.applied, true);
    assert.deepEqual(order, ['thrower', 'second', 'third']);
    assert.equal(hub.errors.length, 1);
  });

  it('multiple throwing subscribers each contribute one collected error, all survivors still run', () => {
    const machine = createFakeMachine();
    const hub = createObserverHub(machine);
    let survivor = 0;
    hub.subscribe(() => {
      throw new Error('first-boom');
    });
    hub.subscribe(() => {
      survivor += 1;
    });
    hub.subscribe(() => {
      throw new Error('second-boom');
    });
    hub.dispatch('create');
    assert.equal(survivor, 1);
    assert.equal(hub.errors.length, 2);
  });

  it('errors on later dispatches accumulate without re-notifying for failures', () => {
    const machine = createFakeMachine();
    const hub = createObserverHub(machine);
    hub.subscribe(() => {
      throw new Error('always-boom');
    });
    hub.dispatch('create');
    hub.dispatch('start');
    assert.equal(hub.errors.length, 2);
  });
});

describe('TC-LIFECYCLE-001 fan-out — machine throw propagates without fan-out', () => {
  it('unknown transition throw from the machine reaches the caller and notifies nobody', () => {
    // ASSUMPTION: plan says dispatch forwards to machine and fans out only when
    // applied:true; a machine throw therefore propagates with zero notifications.
    const machine = createFakeMachine();
    const hub = createObserverHub(machine);
    let calls = 0;
    hub.subscribe(() => {
      calls += 1;
    });
    assert.throws(
      () => hub.dispatch('bogus'),
      (err) => {
        assert.match(String((err && err.message) || ''), /LIFECYCLE_UNKNOWN_TRANSITION/);
        return true;
      }
    );
    assert.equal(calls, 0);
    assert.deepEqual(hub.errors, []);
  });
});
