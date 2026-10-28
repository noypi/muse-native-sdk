'use strict';
// T5 — Rotate / light-dark / locale / font-scale / keyboard each complete with
// no recreate, no lost scroll/text, no async jank.
// Plan Interface (verbatim):
// none — integration test over T1+T2+T3+T4 surfaces only; asserts
// `recreated === false`, `verifyRestored(...).identical === true`, and
// synchronous completion (publish returns after all subscribers ran;
// management path performs zero async hops).
// Spec refs: FR-LIFECYCLE-006, AC-LIFECYCLE-016 (TC-LIFECYCLE-016 in-place half;
// device-lab jank out of scope); reuses FR-LIFECYCLE-002 restore +
// FR-LIFECYCLE-003 in-place behavior, cites no new ACs for them.
// Hermetic: node:test + node:assert/strict only; real T2 config_manager + real
// T4 config_signals + real restore_integration + real activity_bridge +
// real saved_state_registry; in-memory fake store; no network/fs/secrets/live
// services. Device-lab frame timing explicitly out of scope (not asserted).
// ASSUMPTIONs: (a) process-continuation is the same integration/registry
// instance (no recreation) — verifyRestored reads the collected slots, so
// identity proves nothing was lost across the in-place config path; (b) slot
// values are JSON-cloneable (registry requirement); (c) synchronous
// completion means both onConfigurationChanged and publish return plain
// (non-thenable) values with all subscribers already run — no promise or
// deferred hop is observable from the public return values; (d) the oversize
// text regression shape (600000-char) follows relaunch_restore.test.js.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createConfigManager, getInvalidation } = require('../src/config_manager.js');
const { createConfigSignalStore } = require('../src/config_signals.js');
const { createActivityBridge } = require('../src/activity_bridge.js');
const { createLifecycleMachine } = require('../src/state_machine.js');
const { createObserverHub } = require('../src/observers.js');
const { createRestoreIntegration } = require('../src/restore_integration.js');
const { createSavedStateRegistry } = require('../src/saved_state_registry.js');

// Five AC-016 scenarios: rotate=orientation, light/dark=uiMode, locale,
// fontScale, keyboard.
const FIVE_SCENARIOS = ['orientation', 'uiMode', 'locale', 'fontScale', 'keyboard'];

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

function createStack() {
  const machine = createLifecycleMachine();
  const hub = createObserverHub(machine);
  const store = createMemoryStore();
  const bridge = createActivityBridge(hub, store);
  const registry = createSavedStateRegistry();
  const integration = createRestoreIntegration(bridge, registry);
  const manager = createConfigManager(hub);
  const signals = createConfigSignalStore();
  return { machine, hub, store, bridge, registry, integration, manager, signals };
}

function baseSlots() {
  return {
    nav: { route: 'details', index: 3, params: { id: 42 } },
    scroll: { x: 0, y: 1280 },
    text: { field: 'note', value: 'hello-preserve', selection: [1, 4] },
    focus: { field: 'note' },
  };
}

function assertSyncResult(value, label) {
  assert.ok(value && typeof value === 'object', `${label} must return a plain object`);
  assert.equal(typeof value.then, 'undefined', `${label} must not return a promise (sync completion)`);
}

function runScenario(change) {
  const stack = createStack();
  const slots = baseSlots();
  stack.integration.collectNavScrollTextFocus(slots);
  const before = stack.machine.getState();

  let delivered = 0;
  let deliveredEvent = null;
  stack.signals.subscribe((event) => {
    delivered += 1;
    deliveredEvent = event;
  });

  const handled = stack.manager.onConfigurationChanged(change);
  const invalidation = getInvalidation(change);
  const published = stack.signals.publish(change, invalidation);
  const verification = stack.integration.verifyRestored(slots);

  return { stack, slots, before, handled, invalidation, published, delivered, deliveredEvent, verification };
}

describe('TC-LIFECYCLE-016 in-place preservation — five scenarios keep state with no recreate', () => {
  for (const change of FIVE_SCENARIOS) {
    it(`${change}: recreated===false, machine unchanged, slots identical`, () => {
      const { before, handled, verification, stack } = runScenario(change);
      assert.equal(handled.recreated, false, `${change} must not recreate`);
      assert.equal(handled.applied, true, `${change} must apply`);
      assert.equal(handled.state, before, `${change} must not change machine state`);
      assert.equal(stack.machine.getState(), before);
      assert.equal(verification.identical, true, `${change} must preserve slots`);
      assert.deepEqual(verification.diff, []);
    });
  }

  it('orientation (rotate): publish delivers the T3 invalidation synchronously', () => {
    const { published, delivered, deliveredEvent, invalidation } = runScenario('orientation');
    assert.equal(delivered, 1);
    assert.equal(published.notified, 1);
    assert.equal(deliveredEvent.change, 'orientation');
    assert.equal(deliveredEvent.invalidation, invalidation);
  });

  it('uiMode (light/dark): publish delivers the T3 invalidation synchronously', () => {
    const { published, delivered, deliveredEvent, invalidation } = runScenario('uiMode');
    assert.equal(delivered, 1);
    assert.equal(published.notified, 1);
    assert.equal(deliveredEvent.change, 'uiMode');
    assert.equal(deliveredEvent.invalidation, invalidation);
  });
});

describe('TC-LIFECYCLE-016 in-place preservation — oversize text across fontScale', () => {
  it('600000-char text survives a fontScale change byte-identical', () => {
    const stack = createStack();
    const big = 'x'.repeat(600000);
    const slots = { ...baseSlots(), text: { field: 'note', value: big, selection: [0, 5] } };
    stack.integration.collectNavScrollTextFocus(slots);
    const before = stack.machine.getState();
    const handled = stack.manager.onConfigurationChanged('fontScale');
    const invalidation = getInvalidation('fontScale');
    let delivered = 0;
    stack.signals.subscribe(() => {
      delivered += 1;
    });
    const published = stack.signals.publish('fontScale', invalidation);
    const verification = stack.integration.verifyRestored(slots);
    assert.equal(handled.recreated, false);
    assert.equal(stack.machine.getState(), before);
    assert.equal(published.notified, 1);
    assert.equal(delivered, 1);
    assert.equal(verification.identical, true);
    assert.deepEqual(verification.diff, []);
  });
});

describe('TC-LIFECYCLE-016 in-place preservation — synchronous completion (no async jank)', () => {
  it('manager path performs zero async hops (plain non-promise result)', () => {
    const stack = createStack();
    stack.integration.collectNavScrollTextFocus(baseSlots());
    const handled = stack.manager.onConfigurationChanged('keyboard');
    assertSyncResult(handled, 'onConfigurationChanged');
    assert.equal(handled.recreated, false);
  });

  it('publish returns after all subscribers ran (no deferred hop)', () => {
    const stack = createStack();
    stack.integration.collectNavScrollTextFocus(baseSlots());
    const order = [];
    stack.signals.subscribe(() => {
      order.push('first');
    });
    stack.signals.subscribe(() => {
      order.push('second');
    });
    const published = stack.signals.publish('locale', getInvalidation('locale'));
    assertSyncResult(published, 'publish');
    assert.deepEqual(order, ['first', 'second']);
    assert.equal(published.notified, 2);
  });
});

describe('TC-LIFECYCLE-016 in-place preservation — boundary and error cases', () => {
  it('a mutated slot after config is detected and names that slot', () => {
    const { verification } = runScenario('locale');
    assert.equal(verification.identical, true);
    const stack = createStack();
    const slots = baseSlots();
    stack.integration.collectNavScrollTextFocus(slots);
    stack.manager.onConfigurationChanged('locale');
    const tampered = { ...slots, scroll: { x: 0, y: 9999 } };
    const drifted = stack.integration.verifyRestored(tampered);
    assert.equal(drifted.identical, false);
    assert.deepEqual(drifted.diff, ['scroll']);
  });

  it('untrusted change names are rejected by publish and preserved slots stay identical', () => {
    // Security: T4 validation fails loudly; the collected slots are unaffected.
    const stack = createStack();
    const slots = baseSlots();
    stack.integration.collectNavScrollTextFocus(slots);
    assert.throws(
      () => stack.signals.publish('__proto__', { targets: ['layout'] }),
      (err) => err && err.code === 'CONFIG_UNKNOWN_CHANGE'
    );
    const verification = stack.integration.verifyRestored(slots);
    assert.equal(verification.identical, true);
    assert.deepEqual(verification.diff, []);
  });
});
