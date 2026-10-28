"use strict";

// T8 — compile-time-keyed DI registry (FR-STATE-006, AC-STATE-026).
//
// Symbol keys only (constexpr-id analogue; never a runtime string map on the
// hot path). Singleton entries cache the factory product so `get` after the
// first resolution is a Map lookup + return (no allocation, no construction).
// Transient entries construct a fresh value per `get`. Factories receive the
// registry for composed resolution (`(c) => ... c.get(OTHER)`).
//
// Kept extensible for T9 (scope teardown / test-double override): entries are
// stored as `{ factory, scope, instance, hasInstance }` records in a single
// Map, so later work can add construction-order tracking and disposal without
// changing this surface. This module implements ONLY the T8 surface.
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

  var registry = { bind: bind, get: get };
  return registry;
}

var scopeStates = new WeakMap();

function createScope(parent) {
  var instances = new Map();
  var overrides = new Map();
  var order = [];
  var disposed = false;

  function scopeGet(key) {
    if (disposed) {
      var desc = typeof key === "symbol"
        ? (key.description !== undefined ? key.description : String(key))
        : String(key);
      throw new Error("MUSE_STATE_SCOPE_DESTROYED: " + desc);
    }
    if (overrides.has(key)) {
      return overrides.get(key);
    }
    if (instances.has(key)) {
      return instances.get(key);
    }
    if (parent === undefined || parent === null || typeof parent.get !== "function") {
      throw new Error("MUSE_STATE_UNBOUND_SERVICE");
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
      var value;
      if (overrides.has(key)) {
        value = overrides.get(key);
      } else {
        value = instances.get(key);
      }
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
}

module.exports = {
  createRegistry: createRegistry,
  createScope: createScope,
  overrideForTest: overrideForTest,
};
