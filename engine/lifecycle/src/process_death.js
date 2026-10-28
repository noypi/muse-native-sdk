'use strict';
// T1 — Process-death kill-and-restore harness (FR-LIFECYCLE-002).
// Composes the real activity_bridge save/relaunch envelope with the real
// restore_integration collect/verify path and the real saved-state
// registry/snapshot/size-guard machinery (via the bridge). `engineState`
// round-trips through the snapshot envelope alongside the four T7 slots.

const KNOWN_SLOTS = ['nav', 'scroll', 'text', 'focus', 'engineState'];
const FOUR_SLOTS = ['nav', 'scroll', 'text', 'focus'];
const SCOPE = 'screen';
const ENGINE_KEY = 'engineState';

function isPlainRecord(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function badSlots() {
  const err = new Error('PROCESS_DEATH_BAD_SLOTS: slots must be an object record');
  err.code = 'PROCESS_DEATH_BAD_SLOTS';
  return err;
}

function deepClone(value) {
  if (typeof structuredClone === 'function') {
    return structuredClone(value);
  }
  if (value === null || typeof value !== 'object') {
    return value;
  }
  if (Array.isArray(value)) {
    return value.map(deepClone);
  }
  const out = {};
  for (const key of Object.keys(value)) {
    out[key] = deepClone(value[key]);
  }
  return out;
}

function deepEqual(a, b) {
  if (Object.is(a, b)) {
    return true;
  }
  if (typeof a !== typeof b) {
    return false;
  }
  if (a === null || b === null) {
    return false;
  }
  if (typeof a !== 'object') {
    return Number.isNaN(a) && Number.isNaN(b);
  }
  const aIsArray = Array.isArray(a);
  const bIsArray = Array.isArray(b);
  if (aIsArray !== bIsArray) {
    return false;
  }
  if (aIsArray) {
    if (a.length !== b.length) {
      return false;
    }
    for (let i = 0; i < a.length; i += 1) {
      if (!deepEqual(a[i], b[i])) {
        return false;
      }
    }
    return true;
  }
  const all = new Set([...Object.keys(a), ...Object.keys(b)]);
  for (const key of all) {
    const aHas = Object.prototype.hasOwnProperty.call(a, key);
    const bHas = Object.prototype.hasOwnProperty.call(b, key);
    const aVal = aHas ? a[key] : undefined;
    const bVal = bHas ? b[key] : undefined;
    if (typeof aVal === 'undefined' && typeof bVal === 'undefined') {
      continue;
    }
    if (!deepEqual(aVal, bVal)) {
      return false;
    }
  }
  return true;
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

function readEngineState(registry) {
  try {
    const scope = registry.scoped(SCOPE);
    if (scope && typeof scope.get === 'function') {
      return scope.get(ENGINE_KEY);
    }
  } catch (_err) {
    // Treat registry read errors as missing.
  }
  return undefined;
}

function createProcessDeathHarness(deps) {
  const bridgeFactory = deps.bridgeFactory;
  const integrationFactory = deps.integrationFactory;
  const registryFactory = deps.registryFactory;
  const store = createMemoryStore();

  function killAndRelaunch(slots) {
    if (!isPlainRecord(slots)) {
      throw badSlots();
    }
    const expected = deepClone(slots);
    const four = {};
    for (const slot of FOUR_SLOTS) {
      four[slot] = deepClone(expected[slot]);
    }
    const wantEngine = Object.prototype.hasOwnProperty.call(expected, ENGINE_KEY)
      ? deepClone(expected[ENGINE_KEY])
      : undefined;

    // Pre-kill side: collect into a fresh registry and persist through a
    // fresh bridge over the harness store.
    const registry = registryFactory();
    const bridge = bridgeFactory(store);
    if (Object.prototype.hasOwnProperty.call(expected, ENGINE_KEY)) {
      try {
        if (typeof wantEngine === 'undefined') {
          registry.scoped(SCOPE).remove(ENGINE_KEY);
        } else {
          registry.scoped(SCOPE).set(ENGINE_KEY, wantEngine);
        }
      } catch (_err) {
        // Registry set errors (e.g. unencodable) leave the snapshot path to
        // report unrestored; collect still mirrors the four slots.
      }
    }
    const integration = integrationFactory(bridge, registry);
    integration.collectNavScrollTextFocus(four);
    // Ensure the engineState key is included even if collect saved first:
    // re-persist the full snapshot so the envelope carries all five slots.
    if (Object.prototype.hasOwnProperty.call(expected, ENGINE_KEY)) {
      try {
        if (typeof bridge.saveSnapshot === 'function' && typeof registry.snapshot === 'function') {
          bridge.saveSnapshot(registry.snapshot());
        }
      } catch (_err) {
        // Save errors surface as unrestored below; do not throw here.
      }
    }

    // Kill simulation: fresh bridge + fresh registry over the same store.
    const freshBridge = bridgeFactory(store);
    const freshRegistry = registryFactory();
    let relaunched;
    try {
      relaunched = freshBridge.relaunch();
    } catch (_err) {
      relaunched = { restored: false, payload: null };
    }
    if (!relaunched || relaunched.restored !== true) {
      return { restored: false, result: { identical: false, diff: presentSlots(expected) } };
    }
    try {
      if (typeof freshRegistry.restore === 'function') {
        freshRegistry.restore(relaunched.payload);
      }
    } catch (_err) {
      return { restored: false, result: { identical: false, diff: presentSlots(expected) } };
    }
    const freshIntegration = integrationFactory(freshBridge, freshRegistry);
    let fourResult;
    try {
      fourResult = freshIntegration.verifyRestored(four);
    } catch (_err) {
      return { restored: false, result: { identical: false, diff: presentSlots(expected) } };
    }
    const diff = Array.isArray(fourResult.diff) ? [...fourResult.diff] : [];
    if (Object.prototype.hasOwnProperty.call(expected, ENGINE_KEY)) {
      const actualEngine = readEngineState(freshRegistry);
      if (!deepEqual(actualEngine, wantEngine)) {
        diff.push(ENGINE_KEY);
      }
    }
    return { restored: true, result: { identical: diff.length === 0, diff } };
  }

  function presentSlots(expected) {
    const out = [];
    for (const slot of KNOWN_SLOTS) {
      if (Object.prototype.hasOwnProperty.call(expected, slot)) {
        out.push(slot);
      }
    }
    return out;
  }

  return { killAndRelaunch };
}

module.exports = { createProcessDeathHarness };
