'use strict';
// T7 (part 2) — kill-and-restore save path over the four fixture slots,
// including the oversize-text spill path and v1 migration (Batch 4).
// Plan Interface (verbatim):
// `createRestoreIntegration(bridge: object, registry: object): { collectNavScrollTextFocus(slots: { nav: unknown, scroll: unknown, text: unknown, focus: unknown }): void, verifyRestored(expected: { nav: unknown, scroll: unknown, text: unknown, focus: unknown }): { identical: boolean, diff: string[] } }` — slots are opaque (owned by nav/lists/state subsystems; fixtures stand in); verifyRestored deep-compares and names differing slots in diff.
// Spec refs: FR-LIFECYCLE-005, AC-LIFECYCLE-012 (TC-LIFECYCLE-012), AC-LIFECYCLE-013 (TC-LIFECYCLE-013).
// Mirrors TC-LIFECYCLE-012 (restore_integration_test.cpp) and TC-LIFECYCLE-013
// (save_restore_test.cpp).
// Hermetic: node:test + node:assert/strict only; the unit under test
// (createRestoreIntegration) runs for real with a real T3 registry; the bridge
// is a documented in-test harness fake whose saveSnapshot/relaunch delegate to
// the REAL T4 size guard + T5 schema modules plus an in-memory store/storage —
// i.e. exactly the registry→size-guard→schema→store composition the plan
// assigns to the coder's bridge. A tiny limitBytes forces the spill path
// deterministically. No network/fs/secrets/live services.
// ASSUMPTIONs: slots are opaque fixtures (NAV-RESTORE seam); the v1→v2 field
// map used here (`navTitle`→`nav`) is owned by this harness's explicit
// migrations record, not by the coder's bridge; oversize is forced with
// limitBytes=100 so the test never depends on the 512000-byte default.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createRestoreIntegration } = require('../src/restore_integration.js');
const { createSavedStateRegistry } = require('../src/saved_state_registry.js');
const { createSizeGuard } = require('../src/size_guard.js');
const { wrapSnapshot, unwrapSnapshot } = require('../src/snapshot_schema.js');

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

function createMemoryStorage() {
  const map = new Map();
  return {
    write(key, value) {
      map.set(key, value);
    },
    read(key) {
      return map.get(key);
    },
    remove(key) {
      map.delete(key);
    },
    has(key) {
      return map.has(key);
    },
  };
}

// Documented harness fake: reproduces the plan's save path
// (registry snapshot → size-guard pack → schema wrap → store) and the relaunch
// path (store → schema unwrap → size-guard unpack) with real T4/T5 modules.
function createHarnessBridge(store, storage, limitBytes, migrations) {
  const guard = createSizeGuard(storage, limitBytes);
  return {
    saveSnapshot(payload) {
      const packed = guard.pack(payload);
      store.save(wrapSnapshot(packed));
    },
    relaunch() {
      const raw = store.load();
      if (raw === null || typeof raw === 'undefined') {
        return { restored: false, payload: null };
      }
      let unwrapped;
      try {
        unwrapped = unwrapSnapshot(raw, migrations);
      } catch (_err) {
        return { restored: false, payload: null };
      }
      if (!unwrapped || unwrapped.status === 'discarded' || unwrapped.payload === null) {
        return { restored: false, payload: null };
      }
      if (unwrapped.status === 'migrated') {
        // Legacy envelopes predate the packed form; the migrated snapshot is
        // already the usable payload.
        return { restored: true, payload: unwrapped.payload };
      }
      let payload;
      try {
        payload = guard.unpack(unwrapped.payload);
      } catch (_err) {
        return { restored: false, payload: null };
      }
      return { restored: true, payload };
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

describe('TC-LIFECYCLE-013 save/restore — kill-and-restore returns identical slots', () => {
  it('collect → save → kill → relaunch → restore verifies identical', () => {
    const store = createMemoryStore();
    const storage = createMemoryStorage();
    const registry = createSavedStateRegistry();
    const bridge = createHarnessBridge(store, storage, 1000000, undefined);
    const integration = createRestoreIntegration(bridge, registry);
    const slots = fixtureSlots();
    integration.collectNavScrollTextFocus(slots);

    // Process-kill simulation: fresh registry refilled from the relaunch path.
    const relaunched = bridge.relaunch();
    assert.equal(relaunched.restored, true);
    const freshRegistry = createSavedStateRegistry();
    freshRegistry.restore(relaunched.payload);
    const fresh = createRestoreIntegration(bridge, freshRegistry);
    assert.deepEqual(fresh.verifyRestored(slots), { identical: true, diff: [] });
  });

  it('tampering a slot after restore is reported by name', () => {
    const store = createMemoryStore();
    const storage = createMemoryStorage();
    const registry = createSavedStateRegistry();
    const bridge = createHarnessBridge(store, storage, 1000000, undefined);
    const integration = createRestoreIntegration(bridge, registry);
    const slots = fixtureSlots();
    integration.collectNavScrollTextFocus(slots);
    bridge.saveSnapshot(registry.snapshot());

    const relaunched = bridge.relaunch();
    assert.equal(relaunched.restored, true);
    const freshRegistry = createSavedStateRegistry();
    freshRegistry.restore(relaunched.payload);
    const fresh = createRestoreIntegration(bridge, freshRegistry);
    // Tamper through a second collect with a modified slot, then verify
    // against the pre-kill fixtures: only the modified slot must diff.
    const tampered = fixtureSlots();
    tampered.focus = 'other-field';
    fresh.collectNavScrollTextFocus(tampered);
    const result = fresh.verifyRestored(slots);
    assert.equal(result.identical, false);
    assert.deepEqual([...result.diff].sort(), ['focus']);
  });
});

describe('TC-LIFECYCLE-013 save/restore — oversize text slot restores via spill', () => {
  it('large text payload spills to storage and restores byte-identical', () => {
    // ASSUMPTION: limitBytes=100 forces the spill path deterministically;
    // production default (512000) is not load-bearing for this test.
    const store = createMemoryStore();
    const storage = createMemoryStorage();
    const registry = createSavedStateRegistry();
    const bridge = createHarnessBridge(store, storage, 100, undefined);
    const integration = createRestoreIntegration(bridge, registry);
    const slots = fixtureSlots();
    slots.text = { field: 'notes', value: 'x'.repeat(5000), selection: [0, 5000] };
    integration.collectNavScrollTextFocus(slots);
    bridge.saveSnapshot(registry.snapshot());

    const relaunched = bridge.relaunch();
    assert.equal(relaunched.restored, true);
    const freshRegistry = createSavedStateRegistry();
    freshRegistry.restore(relaunched.payload);
    const fresh = createRestoreIntegration(bridge, freshRegistry);
    assert.deepEqual(fresh.verifyRestored(slots), { identical: true, diff: [] });
  });
});

describe('TC-LIFECYCLE-013 save/restore — v1 snapshot migrates on restore', () => {
  it('legacy v1 envelope migrates through the supplied map and verifies', () => {
    // The v1→v2 map here (`navTitle`→`nav`) is owned by this harness's
    // explicit migrations record; the coder's bridge owns its own map.
    const migrations = {
      1: (p) => ({
        screen: {
          nav: { route: p.screen.navTitle, args: {} },
          scroll: p.screen.scroll,
          text: p.screen.text,
          focus: p.screen.focus,
        },
      }),
    };
    const store = createMemoryStore();
    const storage = createMemoryStorage();
    // Save a legacy v1 envelope directly: legacy payload wrapped at version 1.
    store.save(wrapSnapshot({ screen: { navTitle: 'details/7', scroll: { x: 0, y: 1 }, text: 'hi', focus: 'f' } }, 1));
    const harness = createHarnessBridge(store, storage, 1000000, migrations);
    const raw = store.load();
    const unwrapped = unwrapSnapshot(raw, migrations);
    assert.equal(unwrapped.status, 'migrated');
    const relaunched = harness.relaunch();
    assert.equal(relaunched.restored, true);
    assert.deepEqual(relaunched.payload, {
      screen: {
        nav: { route: 'details/7', args: {} },
        scroll: { x: 0, y: 1 },
        text: 'hi',
        focus: 'f',
      },
    });
  });
});

describe('TC-LIFECYCLE-013 save/restore — corrupt store relaunches unrestored', () => {
  it('corrupt raw in the store yields restored:false without throwing', () => {
    const store = createMemoryStore();
    const storage = createMemoryStorage();
    store.save('garbage-not-an-envelope');
    const bridge = createHarnessBridge(store, storage, 100, undefined);
    let result;
    assert.doesNotThrow(() => {
      result = bridge.relaunch();
    });
    assert.deepEqual(result, { restored: false, payload: null });
  });
});
