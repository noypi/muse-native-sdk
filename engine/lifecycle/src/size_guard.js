'use strict';
// T4 — Bundle IPC size guard with disk spill.
// Oversize scope payloads move to injected KV storage and are replaced
// in-bundle with {__spilled: key} tombstones; unpack rehydrates them.

const BUNDLE_IPC_LIMIT_BYTES = 512000;

function byteLength(value) {
  return Buffer.byteLength(JSON.stringify(value), 'utf8');
}

function cloneJson(value) {
  return JSON.parse(JSON.stringify(value));
}

function isTombstone(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    return false;
  }
  const keys = Object.keys(value);
  return keys.length === 1 && keys[0] === '__spilled' && typeof value.__spilled === 'string';
}

function spillMissing(key) {
  const err = new Error(
    `SAVED_STATE_SPILL_MISSING: no spilled value for key ${JSON.stringify(key)}`
  );
  err.code = 'SAVED_STATE_SPILL_MISSING';
  return err;
}

function createSizeGuard(storage, limitBytes) {
  const limit = typeof limitBytes === 'undefined' ? BUNDLE_IPC_LIMIT_BYTES : limitBytes;

  function pack(snapshot) {
    const bundle = {};
    const spilled = [];
    const source = snapshot && typeof snapshot === 'object' ? snapshot : {};
    for (const scope of Object.keys(source)) {
      const value = source[scope];
      if (byteLength(value) > limit) {
        storage.write(scope, JSON.stringify(value));
        bundle[scope] = { __spilled: scope };
        spilled.push(scope);
      } else {
        bundle[scope] = cloneJson(value);
      }
    }
    return { bundle, spilled };
  }

  function unpack(packed) {
    const bundle = (packed && packed.bundle) || {};
    const out = {};
    for (const scope of Object.keys(bundle)) {
      const value = bundle[scope];
      if (isTombstone(value)) {
        const key = value.__spilled;
        const raw = storage.read(key);
        if (typeof raw === 'undefined') {
          throw spillMissing(key);
        }
        out[scope] = JSON.parse(raw);
      } else {
        out[scope] = cloneJson(value);
      }
    }
    return out;
  }

  return { pack, unpack };
}

module.exports = { createSizeGuard, BUNDLE_IPC_LIMIT_BYTES };
