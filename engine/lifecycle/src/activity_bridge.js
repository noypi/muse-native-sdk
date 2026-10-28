'use strict';
// T6 — Activity→C++ bridge mapping with relaunch-restore wiring.
// Maps Activity callbacks (onCreate/onStart/onResume/onPause/onStop/onDestroy
// + `config:<name>` variants) to T1 transitions through the T2 observer hub,
// enforcing exactly-once delivery per idempotency key. Save/relaunch persists
// a T5 versioned envelope of the T4-packed payload through the store.

const { createSizeGuard } = require('./size_guard.js');
const { wrapSnapshot, unwrapSnapshot } = require('./snapshot_schema.js');

const ACTIVITY_TO_TRANSITION = {
  onCreate: 'create',
  onStart: 'start',
  onResume: 'resume',
  onPause: 'pause',
  onStop: 'stop',
  onDestroy: 'destroy',
};

const INITIAL_STATE = 'init';

// Documented v1→v2 field map: v1 stored the visible title as a flat `title`
// field; v2 nests per-screen UI state under `screen`, preserving other keys.
// Legacy envelopes predate the T4 packed form, so the migrated snapshot is
// already the usable payload.
const MIGRATIONS = {
  1(payload) {
    const copy = { ...payload };
    if (
      Object.prototype.hasOwnProperty.call(copy, 'title') &&
      !Object.prototype.hasOwnProperty.call(copy, 'screen')
    ) {
      const { title, ...rest } = copy;
      return { ...rest, screen: { title } };
    }
    return copy;
  },
};

function isConfigEvent(event) {
  return (
    typeof event === 'string' &&
    event.startsWith('config:') &&
    event.length > 'config:'.length
  );
}

function toTransition(event) {
  if (Object.prototype.hasOwnProperty.call(ACTIVITY_TO_TRANSITION, event)) {
    return ACTIVITY_TO_TRANSITION[event];
  }
  if (isConfigEvent(event)) {
    return event;
  }
  // Unknown Activity event: pass through so the hub/machine rejects it with
  // LIFECYCLE_UNKNOWN_TRANSITION instead of being swallowed here.
  return event;
}

function checkKey(idempotencyKey) {
  if (typeof idempotencyKey !== 'string' || idempotencyKey.length === 0) {
    const err = new Error(
      'ACTIVITY_BRIDGE_MISSING_KEY: onActivityEvent requires a non-empty string idempotencyKey'
    );
    err.code = 'ACTIVITY_BRIDGE_MISSING_KEY';
    throw err;
  }
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
  };
}

function isPlainRecord(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function createActivityBridge(hub, store) {
  const seenKeys = new Set();
  let lastState = INITIAL_STATE;
  const storage = createMemoryStorage();
  const guard = createSizeGuard(storage);

  function onActivityEvent(event, idempotencyKey) {
    checkKey(idempotencyKey);
    if (seenKeys.has(idempotencyKey)) {
      return { applied: false, state: lastState };
    }
    const result = hub.dispatch(toTransition(event));
    seenKeys.add(idempotencyKey);
    if (result && typeof result.state === 'string') {
      lastState = result.state;
    }
    return { applied: result.applied, state: result.state };
  }

  function saveSnapshot(payload) {
    const packed = guard.pack(payload);
    // Spilled scope values live in the guard's per-instance storage, which is
    // wiped by process death. Persist a copy inside the store envelope so a
    // fresh bridge over the same store can rehydrate and restore byte-identical.
    const spill = {};
    const spilled = Array.isArray(packed.spilled) ? packed.spilled : [];
    for (const scope of spilled) {
      const raw = storage.read(scope);
      if (typeof raw !== 'undefined') {
        spill[scope] = raw;
      }
    }
    store.save(wrapSnapshot({ ...packed, spill }));
  }

  function relaunch() {
    const unrestored = { restored: false, payload: null };
    let raw;
    try {
      raw = store.load();
    } catch (_err) {
      return unrestored;
    }
    if (raw === null || typeof raw === 'undefined') {
      return unrestored;
    }
    let unwrapped;
    try {
      unwrapped = unwrapSnapshot(raw, MIGRATIONS);
    } catch (_err) {
      return unrestored;
    }
    if (!unwrapped || unwrapped.status === 'discarded' || unwrapped.payload === null) {
      return unrestored;
    }
    if (unwrapped.status === 'migrated') {
      // Legacy envelopes predate the packed form; the migrated snapshot is
      // already the usable payload.
      return { restored: true, payload: unwrapped.payload };
    }
    // Rehydrate the guard's storage from the envelope so spilled scopes
    // survive process death (fresh bridge instance, same store).
    const packedPayload = unwrapped.payload;
    if (isPlainRecord(packedPayload.spill)) {
      for (const key of Object.keys(packedPayload.spill)) {
        storage.write(key, packedPayload.spill[key]);
      }
    }
    try {
      const { bundle, spilled } = packedPayload;
      return { restored: true, payload: guard.unpack({ bundle, spilled }) };
    } catch (_err) {
      return unrestored;
    }
  }

  return { onActivityEvent, saveSnapshot, relaunch };
}

module.exports = { createActivityBridge };
