'use strict';
// T2 — Configuration changes handled in place: no recreation, state unchanged.
// Plan Interface (verbatim):
// `createConfigManager(hub: { dispatch(t: string): { applied: boolean, state: string } }): { onConfigurationChanged(change: string): { applied: boolean, state: string, recreated: false } }` and bridge extension `onConfigurationChanged(change: string, idempotencyKey: string): { applied: boolean, state: string, recreated: false }` — duplicate idempotencyKey returns `{applied:false, recreated:false}` without re-dispatch; missing/empty/non-string key throws; unknown change string propagates the hub throw.
// Spec refs: FR-LIFECYCLE-003, AC-LIFECYCLE-003 (TC-LIFECYCLE-003 hermetic mirror).
// Hermetic: node:test + node:assert/strict only; real T1 machine + T2 hub as
// the dispatch collaborator; in-memory fake store; no network/fs/secrets/live services.
// ASSUMPTIONs: (a) `change` maps verbatim to the T1 `config:<change>` self-loop
// (same pass-through convention activity_bridge.js/state_machine.js already
// use for `config:<name>`); (b) bridge dedup is keyed by idempotencyKey alone
// (same convention as onActivityEvent); a missing/empty/non-string key throws
// (any error — no code pinned); (c) only strings that yield a non-variant
// transition (e.g. '') propagate the hub throw, since every `config:<name>`
// with a non-empty name is a legal self-loop; (d) opaque/malicious-looking
// change strings (slashes, dots, proto names, oversize) are treated as opaque
// config names: dispatched once as self-loops with state unchanged and no
// recreation.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createConfigManager } = require('../src/config_manager.js');
const { createActivityBridge } = require('../src/activity_bridge.js');
const { createLifecycleMachine } = require('../src/state_machine.js');
const { createObserverHub } = require('../src/observers.js');

const ALL_13 = [
  'locale',
  'layoutDirection',
  'density',
  'fontScale',
  'orientation',
  'screenSize',
  'smallestScreenSize',
  'uiMode',
  'colorMode',
  'keyboard',
  'keyboardHidden',
  'navigation',
  'screenLayout',
];

function createHub() {
  const machine = createLifecycleMachine();
  const hub = createObserverHub(machine);
  return { machine, hub };
}

function createMemoryStore() {
  let held = null;
  return {
    save(raw) {
      held = raw;
    },
    load() {
      return held;
    },
  };
}

describe('TC-LIFECYCLE-003 config in place — module shape', () => {
  it('exports createConfigManager', () => {
    assert.equal(typeof createConfigManager, 'function');
  });

  it('manager exposes onConfigurationChanged', () => {
    const { hub } = createHub();
    const manager = createConfigManager(hub);
    assert.equal(typeof manager.onConfigurationChanged, 'function');
  });

  it('bridge exposes onConfigurationChanged', () => {
    const { hub } = createHub();
    const bridge = createActivityBridge(hub, createMemoryStore());
    assert.equal(typeof bridge.onConfigurationChanged, 'function');
  });
});

describe('TC-LIFECYCLE-003 config in place — all 13 changes apply without recreation', () => {
  it('each change dispatches with applied:true, recreated:false, state unchanged', () => {
    const { machine, hub } = createHub();
    const manager = createConfigManager(hub);
    for (const change of ALL_13) {
      const before = machine.getState();
      const result = manager.onConfigurationChanged(change);
      assert.equal(result.applied, true, `change ${change} should apply`);
      assert.equal(result.recreated, false, `change ${change} must not recreate`);
      assert.equal(result.state, before, `change ${change} must not change state`);
      assert.equal(machine.getState(), before);
    }
  });

  it('the observer sees exactly one event per change', () => {
    const { hub } = createHub();
    const seen = [];
    hub.subscribe((event) => seen.push(event.transition));
    const manager = createConfigManager(hub);
    for (const change of ALL_13) {
      manager.onConfigurationChanged(change);
    }
    assert.deepEqual(
      seen,
      ALL_13.map((change) => `config:${change}`)
    );
  });
});

describe('TC-LIFECYCLE-003 config in place — bridge idempotency keys', () => {
  it('duplicate idempotencyKey returns applied:false, recreated:false without re-dispatch', () => {
    const { hub } = createHub();
    let deliveries = 0;
    hub.subscribe(() => {
      deliveries += 1;
    });
    const bridge = createActivityBridge(hub, createMemoryStore());
    const first = bridge.onConfigurationChanged('orientation', 'cfg-1');
    assert.equal(first.applied, true);
    assert.equal(first.recreated, false);
    const second = bridge.onConfigurationChanged('orientation', 'cfg-1');
    assert.equal(second.applied, false);
    assert.equal(second.recreated, false);
    assert.equal(deliveries, 1);
  });

  it('same key with a different change is still a duplicate (key-keyed dedup)', () => {
    // ASSUMPTION (b): dedup keyed by idempotencyKey alone.
    const { hub } = createHub();
    let deliveries = 0;
    hub.subscribe(() => {
      deliveries += 1;
    });
    const bridge = createActivityBridge(hub, createMemoryStore());
    assert.equal(bridge.onConfigurationChanged('orientation', 'cfg-shared').applied, true);
    const retry = bridge.onConfigurationChanged('locale', 'cfg-shared');
    assert.equal(retry.applied, false);
    assert.equal(retry.recreated, false);
    assert.equal(deliveries, 1);
  });

  it('missing, empty, or non-string idempotencyKey throws', () => {
    // ASSUMPTION (b): unusable key fails loudly; no error code pinned.
    const { hub } = createHub();
    const bridge = createActivityBridge(hub, createMemoryStore());
    assert.throws(() => bridge.onConfigurationChanged('orientation', undefined));
    assert.throws(() => bridge.onConfigurationChanged('orientation', ''));
    assert.throws(() => bridge.onConfigurationChanged('orientation', 42));
  });
});

describe('TC-LIFECYCLE-003 config in place — unknown change propagates the hub throw', () => {
  it('manager propagates the hub throw for an unknown change string', () => {
    // ASSUMPTION (c): '' maps to `config:` which the machine rejects.
    const { hub } = createHub();
    const manager = createConfigManager(hub);
    assert.throws(
      () => manager.onConfigurationChanged(''),
      (err) => {
        assert.match(String((err && err.code) || ''), /LIFECYCLE_UNKNOWN_TRANSITION/);
        return true;
      }
    );
  });

  it('bridge propagates the hub throw for an unknown change string', () => {
    const { hub } = createHub();
    const bridge = createActivityBridge(hub, createMemoryStore());
    assert.throws(() => bridge.onConfigurationChanged('', 'cfg-unknown-1'));
  });
});

describe('TC-LIFECYCLE-003 config in place — interleaved Activity sequence', () => {
  it('full create→destroy interleaved with config events dispatches exactly once per transition', () => {
    const { machine, hub } = createHub();
    const seen = [];
    hub.subscribe((event) => seen.push(event.transition));
    const bridge = createActivityBridge(hub, createMemoryStore());
    const sequence = [
      ['onCreate', 'act-create'],
      ['config:orientation', 'cfg-orientation'],
      ['onStart', 'act-start'],
      ['config:locale', 'cfg-locale'],
      ['onResume', 'act-resume'],
      ['config:fontScale', 'cfg-fontscale'],
      ['onPause', 'act-pause'],
      ['onStop', 'act-stop'],
      ['onDestroy', 'act-destroy'],
    ];
    for (const [event, key] of sequence) {
      const result =
        typeof bridge.onConfigurationChanged === 'function' && event.startsWith('config:')
          ? bridge.onConfigurationChanged(event.slice('config:'.length), key)
          : bridge.onActivityEvent(event, key);
      assert.equal(result.applied, true, `event ${event} should apply`);
      assert.equal(result.recreated === false || typeof result.recreated === 'undefined', true);
    }
    assert.deepEqual(seen, sequence.map(([event]) =>
      event.startsWith('config:') ? event : { onCreate: 'create', onStart: 'start', onResume: 'resume', onPause: 'pause', onStop: 'stop', onDestroy: 'destroy' }[event]
    ));
    assert.equal(machine.getState(), 'destroyed');
  });
});

describe('TC-LIFECYCLE-003 config in place — opaque change strings', () => {
  it('malicious-looking change strings stay opaque self-loops with state unchanged', () => {
    // ASSUMPTION (d): no validation beyond the hub; opaque names never recreate.
    const { machine, hub } = createHub();
    const manager = createConfigManager(hub);
    for (const change of ['../escape', '__proto__', 'a'.repeat(1000)]) {
      const before = machine.getState();
      const result = manager.onConfigurationChanged(change);
      assert.equal(result.applied, true);
      assert.equal(result.recreated, false);
      assert.equal(machine.getState(), before);
    }
  });
});
