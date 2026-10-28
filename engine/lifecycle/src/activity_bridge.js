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
  };
}

function createActivityBridge(hub, store) {
  const seenKeys = new Set();
  let lastState = INITIAL_STATE;
  const guard = createSizeGuard(createMemoryStorage());

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
    store.save(wrapSnapshot(packed));
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
    try {
      return { restored: true, payload: guard.unpack(unwrapped.payload) };
    } catch (_err) {
      return unrestored;
    }
  }

  return { onActivityEvent, saveSnapshot, relaunch };
}

module.exports = { createActivityBridge };
