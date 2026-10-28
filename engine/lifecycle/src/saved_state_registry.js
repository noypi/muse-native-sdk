'use strict';
// T3 — SavedStateHandle-equivalent scoped registry.
// Per-screen scopes with isolated keys; snapshot/restore round-trips plain
// JSON values across the Bundle bridge.

function checkEncodable(scope, key, value) {
  if (typeof value === 'function' || typeof value === 'symbol' || typeof value === 'undefined') {
    throw unencodable(scope, key);
  }
  let encoded;
  try {
    encoded = JSON.stringify(value);
  } catch (_err) {
    throw unencodable(scope, key);
  }
  if (typeof encoded === 'undefined') {
    throw unencodable(scope, key);
  }
  return encoded;
}

function unencodable(scope, key) {
  const err = new Error(
    `SAVED_STATE_UNENCODABLE: value for scope ${JSON.stringify(scope)} key ${JSON.stringify(key)} is not JSON-serializable`
  );
  err.code = 'SAVED_STATE_UNENCODABLE';
  return err;
}

function cloneJson(value) {
  return JSON.parse(JSON.stringify(value));
}

function createSavedStateRegistry() {
  const scopes = new Map();

  function scopeMap(scope, create) {
    let map = scopes.get(scope);
    if (!map && create) {
      map = new Map();
      scopes.set(scope, map);
    }
    return map;
  }

  function scoped(scope) {
    return {
      set(key, value) {
        const encoded = checkEncodable(scope, key, value);
        scopeMap(scope, true).set(key, JSON.parse(encoded));
      },
      get(key) {
        const map = scopeMap(scope, false);
        if (!map || !map.has(key)) {
          return undefined;
        }
        return cloneJson(map.get(key));
      },
      remove(key) {
        const map = scopeMap(scope, false);
        if (!map) {
          return;
        }
        map.delete(key);
        if (map.size === 0) {
          scopes.delete(scope);
        }
      },
      keys() {
        const map = scopeMap(scope, false);
        return map ? Array.from(map.keys()) : [];
      },
    };
  }

  function snapshot() {
    const out = {};
    for (const [scope, map] of scopes) {
      if (map.size === 0) {
        continue;
      }
      out[scope] = cloneJson(Object.fromEntries(map));
    }
    return out;
  }

  function restore(data) {
    scopes.clear();
    if (!data || typeof data !== 'object') {
      return;
    }
    for (const scope of Object.keys(data)) {
      const record = data[scope];
      if (!record || typeof record !== 'object') {
        continue;
      }
      const map = new Map();
      for (const key of Object.keys(record)) {
        map.set(key, cloneJson(record[key]));
      }
      if (map.size > 0) {
        scopes.set(scope, map);
      }
    }
  }

  function clear(scope) {
    if (typeof scope === 'undefined') {
      scopes.clear();
      return;
    }
    scopes.delete(scope);
  }

  return { scoped, snapshot, restore, clear };
}

module.exports = { createSavedStateRegistry };
