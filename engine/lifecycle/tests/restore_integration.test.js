'use strict';
// T7 (part 1) — kill-and-restore integration over nav/scroll/text/focus fixture
// slots: collect/verify semantics (Batch 4).
// Plan Interface (verbatim):
// `createRestoreIntegration(bridge: object, registry: object): { collectNavScrollTextFocus(slots: { nav: unknown, scroll: unknown, text: unknown, focus: unknown }): void, verifyRestored(expected: { nav: unknown, scroll: unknown, text: unknown, focus: unknown }): { identical: boolean, diff: string[] } }` — slots are opaque (owned by nav/lists/state subsystems; fixtures stand in); verifyRestored deep-compares and names differing slots in diff.
// Spec refs: FR-LIFECYCLE-005, AC-LIFECYCLE-012 (TC-LIFECYCLE-012), AC-LIFECYCLE-013 (TC-LIFECYCLE-013).
// Mirrors TC-LIFECYCLE-012 (restore_integration_test.cpp, NAV-RESTORE seam) and
// TC-LIFECYCLE-013 (save_restore_test.cpp).
// Hermetic: node:test + node:assert/strict only; real T3 registry as the
// registry collaborator; stub bridge (save/relaunch call recording only — the
// save path composition is covered in save_restore.test.js); no network/fs/
// secrets/live services.
// ASSUMPTIONs: slots are opaque fixtures standing in for nav/lists/state
// subsystems (NAV-RESTORE seam); collect() with a non-object slots record
// throws (any error — no code pinned); collect copies slot values (later
// mutation of the caller's object is not observed); verifyRestored compares
// the four named slots by deep equality and reports every differing slot name
// in diff with `identical === (diff.length === 0)`.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createRestoreIntegration } = require('../src/restore_integration.js');
const { createSavedStateRegistry } = require('../src/saved_state_registry.js');

function createStubBridge() {
  const calls = { saveSnapshot: 0, relaunch: 0 };
  return {
    calls,
    saveSnapshot(_payload) {
      calls.saveSnapshot += 1;
    },
    relaunch() {
      calls.relaunch += 1;
      return { restored: false, payload: null };
    },
  };
}

function fixtureSlots() {
  return {
    nav: { route: 'details/7', args: { id: 7 } },
    scroll: { x: 0, y: 248 },
    text: { field: 'search', value: 'hello', selection: [3, 9] },
    focus: 'search-field',
  };
}

function setup() {
  const bridge = createStubBridge();
  const registry = createSavedStateRegistry();
  const integration = createRestoreIntegration(bridge, registry);
  return { bridge, registry, integration };
}

describe('TC-LIFECYCLE-012 restore integration — module shape', () => {
  it('exposes collectNavScrollTextFocus and verifyRestored', () => {
    const { integration } = setup();
    assert.equal(typeof integration.collectNavScrollTextFocus, 'function');
    assert.equal(typeof integration.verifyRestored, 'function');
  });
});

describe('TC-LIFECYCLE-012 restore integration — identical slots verify clean', () => {
  it('collect then verify with the same fixtures is identical with empty diff', () => {
    const { integration } = setup();
    const slots = fixtureSlots();
    integration.collectNavScrollTextFocus(slots);
    assert.deepEqual(integration.verifyRestored(fixtureSlots()), {
      identical: true,
      diff: [],
    });
  });

  it('identical holds for null, empty, and nested slot values', () => {
    const { integration } = setup();
    const slots = { nav: null, scroll: {}, text: '', focus: undefined };
    integration.collectNavScrollTextFocus(slots);
    assert.deepEqual(integration.verifyRestored({ nav: null, scroll: {}, text: '', focus: undefined }), {
      identical: true,
      diff: [],
    });
  });

  it('identical is true exactly when diff is empty', () => {
    const { integration } = setup();
    integration.collectNavScrollTextFocus(fixtureSlots());
    const result = integration.verifyRestored(fixtureSlots());
    assert.equal(result.identical, result.diff.length === 0);
  });
});

describe('TC-LIFECYCLE-012 restore integration — tampered slots are named in diff', () => {
  for (const slot of ['nav', 'scroll', 'text', 'focus']) {
    it(`tampered ${slot} reports exactly [${slot}] in diff`, () => {
      const { integration } = setup();
      integration.collectNavScrollTextFocus(fixtureSlots());
      const tampered = fixtureSlots();
      tampered[slot] = { __tampered: true };
      const result = integration.verifyRestored(tampered);
      assert.equal(result.identical, false);
      assert.deepEqual([...result.diff].sort(), [slot]);
    });
  }

  it('multiple tampered slots are all named', () => {
    const { integration } = setup();
    integration.collectNavScrollTextFocus(fixtureSlots());
    const tampered = fixtureSlots();
    tampered.nav = 'elsewhere';
    tampered.focus = 'other-field';
    const result = integration.verifyRestored(tampered);
    assert.equal(result.identical, false);
    assert.deepEqual([...result.diff].sort(), ['focus', 'nav']);
  });

  it('a nested change inside a slot names that slot', () => {
    const { integration } = setup();
    integration.collectNavScrollTextFocus(fixtureSlots());
    const tampered = fixtureSlots();
    tampered.scroll = { x: 0, y: 249 };
    const result = integration.verifyRestored(tampered);
    assert.equal(result.identical, false);
    assert.deepEqual([...result.diff].sort(), ['scroll']);
  });

  it('re-collecting overwrites: verify against the older fixtures diffs everything that changed', () => {
    const { integration } = setup();
    const first = fixtureSlots();
    integration.collectNavScrollTextFocus(first);
    const second = fixtureSlots();
    second.text = { field: 'search', value: 'hello world', selection: [3, 15] };
    integration.collectNavScrollTextFocus(second);
    assert.deepEqual(integration.verifyRestored(second), { identical: true, diff: [] });
    const stale = integration.verifyRestored(first);
    assert.equal(stale.identical, false);
    assert.deepEqual([...stale.diff].sort(), ['text']);
  });
});

describe('TC-LIFECYCLE-013 restore integration — collect boundary cases', () => {
  it('collect with a non-object slots record throws', () => {
    // ASSUMPTION: plan silent on validation; rejecting a non-record loudly is
    // most defensible (no error code pinned).
    const { integration } = setup();
    assert.throws(() => integration.collectNavScrollTextFocus(null));
    assert.throws(() => integration.collectNavScrollTextFocus(undefined));
    assert.throws(() => integration.collectNavScrollTextFocus('nav-only'));
  });

  it('collect copies values: later caller mutation is not observed', () => {
    // ASSUMPTION: collect takes a snapshot copy (consistent with the T3
    // registry cloning on set); aliasing the caller would corrupt restore.
    const { integration } = setup();
    const slots = fixtureSlots();
    integration.collectNavScrollTextFocus(slots);
    slots.nav.args.id = 999;
    slots.scroll.y = -1;
    assert.deepEqual(integration.verifyRestored(fixtureSlots()), {
      identical: true,
      diff: [],
    });
  });
});
