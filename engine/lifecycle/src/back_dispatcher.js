'use strict';
// T1 — per-destination back-callback registry with dispose-pop and back-to-home fallback.
// FR-LIFECYCLE-007, AC-LIFECYCLE-017 (required dep: FR-LIFECYCLE-001).

function badValue(code, message) {
  const err = new Error(`${code}: ${message}`);
  err.code = code;
  return err;
}

function checkDestinationId(destinationId) {
  if (typeof destinationId !== 'string' || destinationId.length === 0) {
    throw badValue(
      'BACK_BAD_DESTINATION',
      'register requires a non-empty string destinationId'
    );
  }
}

function checkCallback(callback) {
  if (typeof callback !== 'function') {
    throw badValue(
      'BACK_BAD_CALLBACK',
      'register requires a function callback'
    );
  }
}

function checkEventId(eventId) {
  if (typeof eventId !== 'string' || eventId.length === 0) {
    throw badValue(
      'BACK_BAD_EVENT',
      'handleBack requires a non-empty string eventId'
    );
  }
}

function createBackDispatcher(hub) {
  const stack = [];
  const byId = new Map();

  function register(destinationId, callback) {
    checkDestinationId(destinationId);
    checkCallback(callback);
    if (byId.has(destinationId)) {
      throw badValue(
        'BACK_DUPLICATE_DESTINATION',
        `destination already registered: ${destinationId}`
      );
    }
    const entry = { destinationId, callback };
    stack.push(entry);
    byId.set(destinationId, entry);
    let unregistered = false;
    return {
      unregister() {
        if (unregistered) {
          return;
        }
        unregistered = true;
        if (byId.get(destinationId) !== entry) {
          return;
        }
        byId.delete(destinationId);
        const index = stack.indexOf(entry);
        if (index !== -1) {
          stack.splice(index, 1);
        }
      },
    };
  }

  function handleBack(eventId) {
    checkEventId(eventId);
    if (stack.length === 0) {
      return { consumed: false, destinationId: null, wentHome: true };
    }
    const entry = stack.pop();
    byId.delete(entry.destinationId);
    if (hub && typeof hub.dispatch === 'function') {
      try {
        hub.dispatch(eventId);
      } catch (_err) {
        // Hub dispatch rides on FR-LIFECYCLE-001 exact-once semantics; a hub
        // failure must not break back consumption.
      }
    }
    entry.callback();
    return { consumed: true, destinationId: entry.destinationId, wentHome: false };
  }

  return { register, handleBack };
}

module.exports = { createBackDispatcher };
