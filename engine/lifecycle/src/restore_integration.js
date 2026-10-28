'use strict';
// T7 — Restore integration over nav/scroll/text/focus slots.
// Collects opaque slot values into the T3 registry (per-screen scope) and
// deep-compares them on verify, naming every differing slot in diff.

const SLOTS = ['nav', 'scroll', 'text', 'focus'];
const SCOPE = 'screen';

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
  const aKeys = Object.keys(a);
  const bKeys = Object.keys(b);
  // Treat explicit `undefined` and a missing key as equal so JSON-backed
  // registry round-trips (which drop undefined) still verify clean.
  const all = new Set([...aKeys, ...bKeys]);
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

function readSlot(registry, slot) {
  try {
    const scope = registry.scoped(SCOPE);
    if (scope && typeof scope.get === 'function') {
      return { present: true, value: scope.get(slot) };
    }
  } catch (_err) {
    // Fall through to undefined on registry read errors.
  }
  return { present: false, value: undefined };
}

function createRestoreIntegration(bridge, registry) {
  let current = {};
  let hasCollected = false;

  function collectNavScrollTextFocus(slots) {
    if (!slots || typeof slots !== 'object') {
      throw new Error('RESTORE_INTEGRATION_BAD_SLOTS: slots must be an object record');
    }
    const next = {};
    for (const slot of SLOTS) {
      next[slot] = deepClone(slots[slot]);
    }
    current = next;
    hasCollected = true;
    // Mirror into the registry so registry.snapshot() carries the payload
    // across the kill-and-restore bridge path.
    if (registry && typeof registry.scoped === 'function') {
      const scope = registry.scoped(SCOPE);
      for (const slot of SLOTS) {
        const value = current[slot];
        if (typeof value === 'undefined') {
          scope.remove(slot);
        } else {
          scope.set(slot, value);
        }
      }
    }
    // Persist through the bridge so a kill immediately after collect can
    // relaunch without an explicit save call (explicit saves stay idempotent).
    if (bridge && typeof bridge.saveSnapshot === 'function' && registry && typeof registry.snapshot === 'function') {
      bridge.saveSnapshot(registry.snapshot());
    }
  }

  function verifyRestored(expected) {
    const want = expected && typeof expected === 'object' ? expected : {};
    const diff = [];
    for (const slot of SLOTS) {
      let actual;
      if (hasCollected && Object.prototype.hasOwnProperty.call(current, slot)) {
        actual = current[slot];
      } else {
        const read = readSlot(registry, slot);
        actual = read.value;
      }
      const hasWant = Object.prototype.hasOwnProperty.call(want, slot);
      const wanted = hasWant ? want[slot] : undefined;
      // Normalize registry-missing (undefined return) the same as an
      // explicitly collected undefined.
      if (!deepEqual(actual, wanted)) {
        diff.push(slot);
      }
    }
    return { identical: diff.length === 0, diff };
  }

  return { collectNavScrollTextFocus, verifyRestored };
}

module.exports = { createRestoreIntegration };
