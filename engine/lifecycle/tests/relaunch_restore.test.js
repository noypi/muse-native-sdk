'use strict';
// T6 — Activity→C++ bridge mapping with relaunch-restore wiring (Batch 4).
// Plan Interface (verbatim):
// `createActivityBridge(hub: object, store: { save(raw: object): void, load(): object | null }): { onActivityEvent(event: string, idempotencyKey: string): { applied: boolean, state: string }, saveSnapshot(payload: object): void, relaunch(): { restored: boolean, payload: object | null } }` — duplicate idempotencyKey returns `{applied:false}` without re-dispatch; relaunch with no/incompatible snapshot returns `{restored:false}`.
// Spec refs: FR-LIFECYCLE-004, AC-LIFECYCLE-005 (TC-LIFECYCLE-005).
// Mirrors TC-LIFECYCLE-005 (relaunch_restore_test.cpp).
// Hermetic: node:test + node:assert/strict only; real T1 machine + T2 hub as the
// hub collaborator; in-memory fake store; no network/fs/secrets/live services.
// ASSUMPTIONs: Activity events are `onCreate/onStart/onResume/onPause/onStop/
// onDestroy` mapping to T1 `create/start/resume/pause/stop/destroy`, and
// `config:<name>` events pass through unchanged to the T1 config self-loops
// (plan names the mapping endpoints, not the wire strings); dedup is keyed by
// idempotencyKey alone (same key + different event is still a no-op); a
// missing/empty/non-string key throws (any error — no code pinned); unknown
// events propagate the hub's throw; idempotency keys live per bridge instance
// (a fresh bridge after process death accepts keys the dead bridge saw);
// saveSnapshot persists a versioned+packed payload per T4/T5, so a hand-written
// `{version:99,...}` raw counts as incompatible on relaunch; exact v1→v2 field
// migration shape is coder-owned (v1 test asserts safe handling only).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createActivityBridge } = require('../src/activity_bridge.js');
const { createLifecycleMachine } = require('../src/state_machine.js');
const { createObserverHub } = require('../src/observers.js');

function createHub() {
  const machine = createLifecycleMachine();
  const hub = createObserverHub(machine);
  return { machine, hub };
}

function createMemoryStore(initial) {
  let held = typeof initial === 'undefined' ? null : initial;
  return {
    save(raw) {
      held = raw;
    },
    load() {
      return held;
    },
  };
}

const ACTIVITY_SEQUENCE = [
  ['onCreate', 'create'],
  ['onStart', 'start'],
  ['onResume', 'resume'],
  ['onPause', 'pause'],
  ['onStop', 'stop'],
  ['onDestroy', 'destroy'],
];

describe('TC-LIFECYCLE-005 activity bridge — module shape', () => {
  it('exports createActivityBridge', () => {
    assert.equal(typeof createActivityBridge, 'function');
  });

  it('bridge exposes onActivityEvent/saveSnapshot/relaunch', () => {
    const { hub } = createHub();
    const bridge = createActivityBridge(hub, createMemoryStore());
    assert.equal(typeof bridge.onActivityEvent, 'function');
    assert.equal(typeof bridge.saveSnapshot, 'function');
    assert.equal(typeof bridge.relaunch, 'function');
  });
});

describe('TC-LIFECYCLE-005 activity bridge — full Activity sequence dispatches exactly once', () => {
  it('each Activity callback reaches the machine once with a fresh key', () => {
    const { hub } = createHub();
    const seen = [];
    hub.subscribe((event) => seen.push(event.transition));
    const bridge = createActivityBridge(hub, createMemoryStore());
    for (const [activity] of ACTIVITY_SEQUENCE) {
      const result = bridge.onActivityEvent(activity, `key-${activity}`);
      assert.equal(result.applied, true);
      assert.equal(typeof result.state, 'string');
    }
    assert.deepEqual(seen, ACTIVITY_SEQUENCE.map(([, transition]) => transition));
  });

  it('config variant events forward unchanged without changing state', () => {
    // ASSUMPTION: `config:<name>` passes through to the T1 config self-loop.
    const { hub, machine } = createHub();
    const bridge = createActivityBridge(hub, createMemoryStore());
    const before = machine.getState();
    const result = bridge.onActivityEvent('config:locale', 'key-config-1');
    assert.equal(result.applied, true);
    assert.equal(machine.getState(), before);
  });

  it('unknown Activity event propagates the hub throw instead of swallowing it', () => {
    const { hub } = createHub();
    const bridge = createActivityBridge(hub, createMemoryStore());
    assert.throws(() => bridge.onActivityEvent('onSomethingElse', 'key-unknown-1'));
  });
});

describe('TC-LIFECYCLE-005 activity bridge — idempotency keys', () => {
  it('duplicate callback with the same key returns applied:false without re-dispatch', () => {
    const { hub } = createHub();
    let deliveries = 0;
    hub.subscribe(() => {
      deliveries += 1;
    });
    const bridge = createActivityBridge(hub, createMemoryStore());
    const first = bridge.onActivityEvent('onCreate', 'idem-1');
    assert.equal(first.applied, true);
    const second = bridge.onActivityEvent('onCreate', 'idem-1');
    assert.deepEqual(second.applied, false);
    assert.equal(deliveries, 1);
  });

  it('same key with a different event is still a duplicate (key-keyed dedup)', () => {
    // ASSUMPTION: dedup is keyed by idempotencyKey alone, per the Interface line.
    const { hub } = createHub();
    let deliveries = 0;
    hub.subscribe(() => {
      deliveries += 1;
    });
    const bridge = createActivityBridge(hub, createMemoryStore());
    assert.equal(bridge.onActivityEvent('onCreate', 'idem-shared').applied, true);
    const retry = bridge.onActivityEvent('onStart', 'idem-shared');
    assert.equal(retry.applied, false);
    assert.equal(deliveries, 1);
  });

  it('different keys for the same event both reach the hub', () => {
    const { hub } = createHub();
    const bridge = createActivityBridge(hub, createMemoryStore());
    assert.equal(bridge.onActivityEvent('onCreate', 'idem-a').applied, true);
    // Second dispatch reaches the hub; the machine reports it as unapplied
    // (duplicate transition) but the bridge must not dedupe it by key.
    const second = bridge.onActivityEvent('onCreate', 'idem-b');
    assert.equal(second.applied, false);
    assert.equal(typeof second.state, 'string');
  });

  it('missing, empty, or non-string idempotency key throws', () => {
    // ASSUMPTION: a call without a usable key must fail loudly (accepting it
    // would silently break the exactly-once guarantee); no error code pinned.
    const { hub } = createHub();
    const bridge = createActivityBridge(hub, createMemoryStore());
    assert.throws(() => bridge.onActivityEvent('onCreate', undefined));
    assert.throws(() => bridge.onActivityEvent('onCreate', ''));
    assert.throws(() => bridge.onActivityEvent('onCreate', 42));
  });

  it('a fresh bridge after process death accepts keys the dead bridge saw', () => {
    // ASSUMPTION: idempotency keys are per-instance memory wiped by process
    // death; redelivery after relaunch must apply again.
    const { hub } = createHub();
    const store = createMemoryStore();
    const before = createActivityBridge(hub, store);
    assert.equal(before.onActivityEvent('onCreate', 'idem-kill').applied, true);
    const after = createActivityBridge(hub, store);
    const retry = after.onActivityEvent('onStart', 'idem-kill');
    assert.equal(retry.applied, true);
  });
});

describe('TC-LIFECYCLE-005 activity bridge — saveSnapshot / relaunch round trip', () => {
  it('process-kill simulation relaunches into the saved payload', () => {
    const store = createMemoryStore();
    const first = createActivityBridge(createHub().hub, store);
    const payload = { screen: { tab: 'home', index: 2 } };
    first.saveSnapshot(payload);
    const second = createActivityBridge(createHub().hub, store);
    const result = second.relaunch();
    assert.equal(result.restored, true);
    assert.deepEqual(result.payload, payload);
  });

  it('saveSnapshot persists through the store so load() is non-null', () => {
    const store = createMemoryStore();
    const bridge = createActivityBridge(createHub().hub, store);
    bridge.saveSnapshot({ screen: { tab: 'details' } });
    assert.notEqual(store.load(), null);
  });
});

describe('TC-LIFECYCLE-005 activity bridge — oversize payload survives relaunch through the real bridge', () => {
  it('600000-char text slot saved via bridge A restores byte-identical via fresh bridge B over the same store', () => {
    // Regression for review BLOCKER: spilled scopes must survive process death
    // through the production bridge (store envelope carries the spill map, so
    // a fresh bridge over the same store can rehydrate). Pre-fix this returned
    // {restored:false, payload:null} (SAVED_STATE_SPILL_MISSING internally).
    const store = createMemoryStore();
    const first = createActivityBridge(createHub().hub, store);
    const bigText = 'x'.repeat(600000);
    const payload = { screen: { tab: 'notes', text: bigText } };
    first.saveSnapshot(payload);

    // Process-kill simulation: fresh bridge instance, same store.
    const second = createActivityBridge(createHub().hub, store);
    const result = second.relaunch();
    assert.equal(result.restored, true);
    assert.deepEqual(result.payload, payload);
    assert.equal(result.payload.screen.text, bigText);
    assert.equal(result.payload.screen.text.length, 600000);
  });
});

describe('TC-LIFECYCLE-005 activity bridge — relaunch without a usable snapshot', () => {
  it('empty store relaunches unrestored without throwing', () => {
    const bridge = createActivityBridge(createHub().hub, createMemoryStore());
    let result;
    assert.doesNotThrow(() => {
      result = bridge.relaunch();
    });
    assert.deepEqual(result, { restored: false, payload: null });
  });

  it('corrupt snapshot relaunches unrestored without throwing', () => {
    const store = createMemoryStore('not-an-envelope');
    const bridge = createActivityBridge(createHub().hub, store);
    let result;
    assert.doesNotThrow(() => {
      result = bridge.relaunch();
    });
    assert.deepEqual(result, { restored: false, payload: null });
  });

  it('incompatible (future-version) snapshot relaunches unrestored without throwing', () => {
    // ASSUMPTION: the save path stores a T5 versioned envelope at top level,
    // so a hand-written future-version envelope is incompatible by definition.
    const store = createMemoryStore({ version: 99, payload: { screen: {} } });
    const bridge = createActivityBridge(createHub().hub, store);
    let result;
    assert.doesNotThrow(() => {
      result = bridge.relaunch();
    });
    assert.deepEqual(result, { restored: false, payload: null });
  });

  it('legacy v1 envelope is handled safely without throwing', () => {
    // ASSUMPTION: exact v1→v2 field migration is coder-owned (plan names a
    // "documented map" without pinning it); the contract here is only that a
    // legacy envelope never crashes relaunch and yields a well-formed result.
    const store = createMemoryStore({ version: 1, payload: { title: 'Home' } });
    const bridge = createActivityBridge(createHub().hub, store);
    let result;
    assert.doesNotThrow(() => {
      result = bridge.relaunch();
    });
    assert.equal(typeof result.restored, 'boolean');
    if (result.restored) {
      assert.equal(typeof result.payload, 'object');
      assert.notEqual(result.payload, null);
    } else {
      assert.equal(result.payload, null);
    }
  });
});
