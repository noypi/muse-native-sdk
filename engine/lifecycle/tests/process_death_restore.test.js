'use strict';
// T1 — Process-death kill-and-restore of nav/scroll/text/focus + engine state.
// Plan Interface (verbatim):
// `createProcessDeathHarness(deps: { bridgeFactory: (store: object) => object, integrationFactory: (bridge: object, registry: object) => object, registryFactory: () => object }): { killAndRelaunch(slots: { nav?: any, scroll?: any, text?: any, focus?: any, engineState?: any }): { restored: boolean, result: { identical: boolean, diff: string[] } } }` — throws `PROCESS_DEATH_BAD_SLOTS` on non-record slots; `restored:false` with `{identical:false}` when no/incompatible snapshot; never throws on corrupt/future-version store content.
// Spec refs: FR-LIFECYCLE-002, AC-LIFECYCLE-002 (TC-LIFECYCLE-002 hermetic mirror).
// Hermetic: node:test + node:assert/strict only; real activity_bridge.js +
// real restore_integration.js + real saved_state_registry.js; in-memory fake
// store (wrapped per-test for fault injection); no network/fs/secrets/live services.
// ASSUMPTIONs: (a) killAndRelaunch collects the given slots, persists them
// through the bridgeFactory store, simulates the kill with a fresh bridge +
// fresh registry over the same store, relaunches, and verifies the relaunched
// state against the given slots; (b) negative-path tests inject post-kill
// store content via a pass-through wrapping store inside bridgeFactory, since
// the harness owns its internal store; (c) the drift test tampers `text`
// reads on every registry from registryFactory — safe because the pre-kill
// side reads post-collect state from memory (`current`), while a fresh
// post-kill side reads through the registry (plan Detail: fresh bridge +
// fresh registry over same store); (d) harness deep-clones caller slots and
// never mutates the input object; (e) all slot values used here are
// JSON-cloneable (registry requirement); only `engineState` is asserted as
// opaque-bytes-shaped per plan A3.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createProcessDeathHarness } = require('../src/process_death.js');
const { createActivityBridge } = require('../src/activity_bridge.js');
const { createLifecycleMachine } = require('../src/state_machine.js');
const { createObserverHub } = require('../src/observers.js');
const { createRestoreIntegration } = require('../src/restore_integration.js');
const { createSavedStateRegistry } = require('../src/saved_state_registry.js');

function createHub() {
  const machine = createLifecycleMachine();
  const hub = createObserverHub(machine);
  return { machine, hub };
}

function realDeps(overrides = {}) {
  return {
    bridgeFactory: (store) => createActivityBridge(createHub().hub, store),
    integrationFactory: (bridge, registry) => createRestoreIntegration(bridge, registry),
    registryFactory: () => createSavedStateRegistry(),
    ...overrides,
  };
}

// Pass-through wrapping store: saves go to the harness store, loads return
// injected post-kill content (ASSUMPTION b).
function loadAs(content) {
  return (store) => ({
    save(raw) {
      store.save(raw);
    },
    load() {
      return content;
    },
  });
}

function throwingLoadStore(store) {
  return {
    save(raw) {
      store.save(raw);
    },
    load() {
      throw new Error('simulated disk failure');
    },
  };
}

// Registry whose `text` reads are tampered (ASSUMPTION c).
function tamperTextReads(realRegistry) {
  return {
    scoped(scope) {
      const inner = realRegistry.scoped(scope);
      return {
        set(key, value) {
          inner.set(key, value);
        },
        get(key) {
          if (key === 'text') {
            return { field: 'note', value: 'TAMPERED-AFTER-KILL', selection: [0, 1] };
          }
          return inner.get(key);
        },
        remove(key) {
          inner.remove(key);
        },
        keys() {
          return inner.keys();
        },
      };
    },
    snapshot() {
      return realRegistry.snapshot();
    },
    restore(data) {
      realRegistry.restore(data);
    },
    clear(scope) {
      realRegistry.clear(scope);
    },
  };
}

function baseSlots() {
  return {
    nav: { route: 'details', index: 3, params: { id: 42 } },
    scroll: { x: 0, y: 1280 },
    text: { field: 'note', value: 'hello', selection: [1, 4] },
    focus: { field: 'note' },
    engineState: { bytes: [1, 2, 3, 255], nested: { flag: true, label: 'engine' } },
  };
}

function assertBadSlotsThrows(fn, input) {
  assert.throws(
    fn,
    (err) => {
      assert.match(String((err && err.code) || ''), /PROCESS_DEATH_BAD_SLOTS/);
      assert.match(String((err && err.message) || ''), /PROCESS_DEATH_BAD_SLOTS/);
      return true;
    },
    `expected PROCESS_DEATH_BAD_SLOTS for ${JSON.stringify(input)}`
  );
}

describe('TC-LIFECYCLE-002 process-death harness — module shape', () => {
  it('exports createProcessDeathHarness', () => {
    assert.equal(typeof createProcessDeathHarness, 'function');
  });

  it('harness exposes killAndRelaunch', () => {
    const harness = createProcessDeathHarness(realDeps());
    assert.equal(typeof harness.killAndRelaunch, 'function');
  });
});

describe('TC-LIFECYCLE-002 process-death harness — kill-and-restore round trip', () => {
  it('restores nav/scroll/text/focus/engineState byte-identical', () => {
    const harness = createProcessDeathHarness(realDeps());
    const outcome = harness.killAndRelaunch(baseSlots());
    assert.equal(outcome.restored, true);
    assert.equal(outcome.result.identical, true);
    assert.deepEqual(outcome.result.diff, []);
  });

  it('round-trips a nested engineState with opaque-bytes shape', () => {
    const harness = createProcessDeathHarness(realDeps());
    const slots = baseSlots();
    slots.engineState = { bytes: Array.from({ length: 256 }, (_, i) => i % 256), tree: { a: [1, { b: 'x' }] } };
    const outcome = harness.killAndRelaunch(slots);
    assert.equal(outcome.restored, true);
    assert.equal(outcome.result.identical, true);
    assert.deepEqual(outcome.result.diff, []);
  });

  it('does not mutate the caller slots object', () => {
    // ASSUMPTION (d): the harness clones; the caller object is untouched.
    const harness = createProcessDeathHarness(realDeps());
    const slots = baseSlots();
    const before = structuredClone(slots);
    harness.killAndRelaunch(slots);
    assert.deepEqual(slots, before);
  });

  it('a drifted slot after restore yields identical:false naming that slot', () => {
    // ASSUMPTION (c): post-kill side reads through a fresh registry.
    const harness = createProcessDeathHarness(
      realDeps({ registryFactory: () => tamperTextReads(createSavedStateRegistry()) })
    );
    const outcome = harness.killAndRelaunch(baseSlots());
    assert.equal(outcome.restored, true);
    assert.equal(outcome.result.identical, false);
    assert.deepEqual(outcome.result.diff, ['text']);
  });
});

describe('TC-LIFECYCLE-002 process-death harness — bad slots input', () => {
  it('throws PROCESS_DEATH_BAD_SLOTS on non-record slots', () => {
    const harness = createProcessDeathHarness(realDeps());
    for (const bad of [null, undefined, 'slots', 42, ['nav']]) {
      assertBadSlotsThrows(() => harness.killAndRelaunch(bad), bad);
    }
  });
});

describe('TC-LIFECYCLE-002 process-death harness — unusable snapshot never throws', () => {
  it('empty store yields restored:false with identical:false without throwing', () => {
    const harness = createProcessDeathHarness(
      realDeps({ bridgeFactory: (store) => createActivityBridge(createHub().hub, loadAs(null)(store)) })
    );
    let outcome;
    assert.doesNotThrow(() => {
      outcome = harness.killAndRelaunch(baseSlots());
    });
    assert.equal(outcome.restored, false);
    assert.equal(outcome.result.identical, false);
    assert.equal(Array.isArray(outcome.result.diff), true);
  });

  it('corrupt store content yields restored:false with identical:false without throwing', () => {
    const harness = createProcessDeathHarness(
      realDeps({
        bridgeFactory: (store) => createActivityBridge(createHub().hub, loadAs('not-an-envelope')(store)),
      })
    );
    let outcome;
    assert.doesNotThrow(() => {
      outcome = harness.killAndRelaunch(baseSlots());
    });
    assert.equal(outcome.restored, false);
    assert.equal(outcome.result.identical, false);
    assert.equal(Array.isArray(outcome.result.diff), true);
  });

  it('future-version store content yields restored:false with identical:false without throwing', () => {
    const harness = createProcessDeathHarness(
      realDeps({
        bridgeFactory: (store) =>
          createActivityBridge(createHub().hub, loadAs({ version: 99, payload: { screen: {} } })(store)),
      })
    );
    let outcome;
    assert.doesNotThrow(() => {
      outcome = harness.killAndRelaunch(baseSlots());
    });
    assert.equal(outcome.restored, false);
    assert.equal(outcome.result.identical, false);
    assert.equal(Array.isArray(outcome.result.diff), true);
  });

  it('a throwing store yields restored:false without propagating the error', () => {
    const harness = createProcessDeathHarness(
      realDeps({
        bridgeFactory: (store) => createActivityBridge(createHub().hub, throwingLoadStore(store)),
      })
    );
    let outcome;
    assert.doesNotThrow(() => {
      outcome = harness.killAndRelaunch(baseSlots());
    });
    assert.equal(outcome.restored, false);
    assert.equal(outcome.result.identical, false);
  });
});
