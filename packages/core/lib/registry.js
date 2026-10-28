"use strict";

// T8 — compile-time-keyed DI registry (FR-STATE-006, AC-STATE-026) plus T9
// scope teardown / test-double override (AC-STATE-027, AC-STATE-028).
//
// Symbol keys only (constexpr-id analogue; never a runtime string map on the
// hot path). Singleton entries cache the factory product so `get` after the
// first resolution is a Map lookup + return (no allocation, no construction).
// Transient entries construct a fresh value per `get`. Factories receive the
// registry for composed resolution (`(c) => ... c.get(OTHER)`).
//
// Kept extensible for T9 (scope teardown / test-double override): entries are
// stored as `{ factory, scope, instance, hasInstance }` records in a single
// Map. `scopeOf(key)` exposes the binding scope so scopes can avoid freezing
// transient parent bindings into singletons. This module implements the
// registry plus scope teardown and override surface.
//
// CommonJS, dependency-free pure JS (matches `lib/scope.js` idiom).

function createRegistry() {
  var entries = new Map();

  function bind(key, factory, scope) {
    if (typeof key !== "symbol") {
      throw new TypeError("bind(key, factory, scope): key must be a symbol");
    }
    if (typeof factory !== "function") {
      throw new TypeError("bind(key, factory, scope): factory must be a function");
    }
    if (scope !== "singleton" && scope !== "transient") {
      throw new TypeError('bind(key, factory, scope): scope must be "singleton" or "transient"');
    }
    entries.set(key, { factory: factory, scope: scope, instance: undefined, hasInstance: false });
  }

  function get(key) {
    var entry = entries.get(key);
    if (entry === undefined) {
      throw new Error("MUSE_STATE_UNBOUND_SERVICE");
    }
    if (entry.scope === "transient") {
      return entry.factory(registry);
    }
    if (entry.hasInstance) {
      return entry.instance;
    }
    var value = entry.factory(registry);
    entry.instance = value;
    entry.hasInstance = true;
    return value;
  }

  var registry = { bind: bind, get: get, scopeOf: scopeOf };

  function scopeOf(key) {
    var entry = entries.get(key);
    return entry === undefined ? undefined : entry.scope;
  }

  return registry;
}

var scopeStates = new WeakMap();

function createScope(parent) {
  var instances = new Map();
  var overrides = new Map();
  var order = [];
  var disposed = false;

  function trackKey(key) {
    if (order.indexOf(key) === -1) {
      order.push(key);
    }
  }

  function scopeGet(key) {
    if (disposed) {
      var desc = typeof key === "symbol"
        ? (key.description !== undefined ? key.description : String(key))
        : String(key);
      throw new Error("MUSE_STATE_SCOPE_DESTROYED: " + desc);
    }
    if (overrides.has(key)) {
      // An override shadows the parent binding but still enters teardown
      // order, so a disposable double is disposed and never leaks — even
      // when the override is installed before any `get`.
      trackKey(key);
      return overrides.get(key);
    }
    if (instances.has(key)) {
      return instances.get(key);
    }
    if (parent === undefined || parent === null || typeof parent.get !== "function") {
      throw new Error("MUSE_STATE_UNBOUND_SERVICE");
    }
    // Transient parent bindings resolve fresh per `get`: memoizing them
    // would silently convert transient scope into singleton scope. The
    // registry parent exposes its binding scope via `scopeOf`; parents
    // without it (plain scopes) keep the snapshot semantics.
    var parentScope = undefined;
    if (typeof parent.scopeOf === "function") {
      try {
        parentScope = parent.scopeOf(key);
      } catch (err) {
        void err;
      }
    }
    if (parentScope === "transient") {
      return parent.get(key);
    }
    var value = parent.get(key);
    instances.set(key, value);
    order.push(key);
    return value;
  }

  function scopeDispose() {
    if (disposed) return;
    disposed = true;
    var firstError = null;
    for (var i = order.length - 1; i >= 0; i -= 1) {
      var key = order[i];
      // Drain every value `get` could have returned for this key: the
      // shadowing double first (most recent binding), then the originally
      // constructed value, so neither leaks undisposed. A double identical
      // to the constructed value is disposed once.
      var seen = [];
      var candidates = [];
      if (overrides.has(key)) {
        candidates.push(overrides.get(key));
      }
      if (instances.has(key)) {
        candidates.push(instances.get(key));
      }
      for (var j = 0; j < candidates.length; j += 1) {
        var value = candidates[j];
        if (seen.indexOf(value) !== -1) continue;
        seen.push(value);
        try {
          if (value !== null && value !== undefined) {
            if (typeof value.dispose === "function") {
              value.dispose();
            }
          }
        } catch (err) {
          if (firstError === null) firstError = err;
        }
      }
    }
    order.length = 0;
    instances.clear();
    overrides.clear();
    if (firstError !== null && firstError !== undefined) throw firstError;
  }

  var scope = { get: scopeGet, dispose: scopeDispose };
  scopeStates.set(scope, { instances: instances, overrides: overrides, order: order });
  return scope;
}

function overrideForTest(scope, key, double) {
  if (typeof key !== "symbol") {
    throw new TypeError("overrideForTest(scope, key, double): key must be a symbol");
  }
  var state = scopeStates.get(scope);
  if (state === undefined) {
    throw new TypeError("overrideForTest(scope, key, double): scope must be created by createScope");
  }
  state.overrides.set(key, double);
  // The override enters teardown order immediately, so `dispose` drains it
  // even when no `get` ever ran for the key (override-before-get).
  if (state.order.indexOf(key) === -1) {
    state.order.push(key);
  }
}

module.exports = {
  createRegistry: createRegistry,
  createScope: createScope,
  overrideForTest: overrideForTest,
};
