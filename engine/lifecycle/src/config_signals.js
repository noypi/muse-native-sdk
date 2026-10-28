'use strict';
// T4 (FR-LIFECYCLE-006, AC-LIFECYCLE-015) — O(1) per-subscriber config propagation.
// Synchronous fan-out over a subscriber-array snapshot (same pattern as
// observers.js); cost is one call per subscriber with no per-subscriber
// lookup/rebuild. `publish` validates against the frozen 13 and throws
// CONFIG_UNKNOWN_CHANGE otherwise; crashing subscribers are isolated
// (counted as notified, error recorded, others still called).

// Frozen `android:configChanges` 13-tuple per plan A2 (same list as
// config_manager.js; duplicated to keep this store dependency-free).
const CONFIG_CHANGES = Object.freeze([
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
]);

const KNOWN_CHANGES = new Set(CONFIG_CHANGES);

function unknownChangeError() {
  const err = new Error('CONFIG_UNKNOWN_CHANGE');
  err.code = 'CONFIG_UNKNOWN_CHANGE';
  return err;
}

function createConfigSignalStore() {
  const subscribers = [];
  const errors = [];

  function subscribe(fn) {
    if (typeof fn !== 'function') {
      throw new TypeError('CONFIG_SIGNAL_BAD_SUBSCRIBER');
    }
    subscribers.push(fn);
    let active = true;
    return function unsubscribe() {
      if (!active) {
        return;
      }
      active = false;
      const index = subscribers.indexOf(fn);
      if (index !== -1) {
        subscribers.splice(index, 1);
      }
    };
  }

  function publish(change, invalidation) {
    if (typeof change !== 'string' || !KNOWN_CHANGES.has(change)) {
      throw unknownChangeError();
    }
    const event = { change, invalidation };
    const snapshot = subscribers.slice();
    for (const fn of snapshot) {
      try {
        fn(event);
      } catch (err) {
        errors.push(err);
      }
    }
    return { notified: snapshot.length };
  }

  return { subscribe, publish, errors };
}

module.exports = { createConfigSignalStore, CONFIG_CHANGES };
