"use strict";

// T4 — `state` / `reactive` / `readonly` / `markRaw` (FR-MCORE-002).
//
// Non-deep `state` holds the value as-is: only `s.value = v` notifies.
// `state(obj, { deep: true })` and `reactive(obj)` observe nested mutation
// through a deep Proxy. Identical-value (`Object.is`) writes are no-ops and
// never call `schedule`. All notification goes through the single shared
// scheduler (`lib/scheduler.js`); reads track the current reader so the
// later computed/effect/watch items can subscribe.
//
// Dependency hook consumed by T5/T6/T7 (documented here, implemented here)://   - `getActiveConsumer()` — the reader currently collecting deps (an
//     effect/computed/watch consumer) or `null` outside a tracked run.
//   - `pushActiveConsumer(consumer)` / `popActiveConsumer()` — run a reader
//     with `consumer` on the stack. A consumer is any object (or function)
//     with an optional notification entry point: either a function itself,
//     or an object with a `job` function property (T6/T7 register their
//     scheduler jobs this way). On `track`, the consumer is added to the
//     dependency's subscriber set and the set is added to
//     `consumer.deps` (when it is an object with a `deps` Set) so the
//     reader can unsubscribe later.
//   - `trackRef(record)` — record a read of the dependency `record`
//     (internal `{ subs, notifyJob }`) against the active consumer.
//   - `getRefRecord(handle)` — internal accessor returning the dependency
//     record for a `state` holder (used by computed/effects to subscribe
//     or unsubscribe directly).
// Reads (`state` `.value` getters, deep-proxy gets, readonly-view gets)
// call `track`; writes call `schedule(record.notifyJob)` exactly once per
// real change. `record.notifyJob` fans out to current subscribers (no-op
// until T5/T6/T7 register any), which keeps the write-path `schedule`
// call-count at exactly one per mutation — the shape the T4 tests spy on.
//
// CommonJS, dependency-free pure JS. Usable outside a component: when no
// instance is current, values simply live unowned.
//
// SUPPORTED readonly contract (holder form): `readonly(stateHandle)` returns
// a live view that reflects source writes, tracks the reader via the source
// record, and throws `MUSE_CORE_READONLY_WRITE` on any write through the
// view — including `delete view.value` (delete-is-a-write; the `value`
// accessor is non-configurable so `delete` throws instead of detaching).
// The plain-object/reactive form (`readonly(obj)`) is a non-tracking
// shallow frozen snapshot: reads do not subscribe the active consumer.

var scheduler = require("./scheduler.js");

var activeConsumerStack = [];

// Brand state holders so `readonly(holder)` can wrap the source instead of
// copying it. `holderRecords` maps holder -> internal record.
var holderSet = new WeakSet();
var holderRecords = new WeakMap();

// Values (and subtrees) opted out of deep conversion.
var rawSet = new WeakSet();

function readonlyWriteError() {
  var err = new Error(
    "MUSE_CORE_READONLY_WRITE: cannot write through a readonly view"
  );
  err.name = "MUSE_CORE_READONLY_WRITE";
  err.code = "MUSE_CORE_READONLY_WRITE";
  return err;
}

function isObject(v) {
  return v !== null && (typeof v === "object" || typeof v === "function");
}

// --- dependency hook (T5/T6/T7 surface) ---------------------------------

function getActiveConsumer() {
  if (activeConsumerStack.length === 0) return null;
  return activeConsumerStack[activeConsumerStack.length - 1];
}

function pushActiveConsumer(consumer) {
  activeConsumerStack.push(consumer === undefined ? null : consumer);
  return consumer;
}

function popActiveConsumer() {
  return activeConsumerStack.pop();
}

// Record a read against the active consumer. `record` is `{ subs, notifyJob }`.
function trackRef(record) {
  var consumer = getActiveConsumer();
  if (consumer === null || consumer === undefined) return;
  if (!record || !record.subs) return;
  if (!record.subs.has(consumer)) record.subs.add(consumer);
  if (
    typeof consumer === "object" &&
    consumer !== null &&
    consumer.deps &&
    typeof consumer.deps.add === "function"
  ) {
    try {
      consumer.deps.add(record.subs);
    } catch (e) {
      void e;
    }
  }
}

function getRefRecord(handle) {
  return holderRecords.get(handle) || null;
}

// --- internals -----------------------------------------------------------

function makeRecord() {
  var record = { subs: new Set(), notifyJob: null, proxies: new WeakMap() };
  record.notifyJob = function () {
    var subs = Array.from(record.subs);
    for (var i = 0; i < subs.length; i += 1) {
      var sub = subs[i];
      try {
        if (typeof sub === "function") sub();
        else if (sub && typeof sub.job === "function") sub.job();
        else if (sub && typeof sub.run === "function") sub.run();
      } catch (e) {
        void e;
      }
    }
  };
  return record;
}

function notify(record) {
  scheduler.schedule(record.notifyJob);
}

// Best-effort ownership: values created during a body run register a
// disposer that drops their subscribers, so unmount silences them. Outside
// a component (or when instance.js is unavailable) this is a no-op.
function registerOwned(record) {
  try {
    var instance = require("./instance.js");
    if (!instance || typeof instance.getCurrentInstance !== "function") return;
    var handle = instance.getCurrentInstance();
    if (!handle) return;
    instance.registerDisposer(handle, function () {
      record.subs.clear();
    });
  } catch (e) {
    void e;
  }
}

function wrapDeep(raw, record) {
  if (!isObject(raw)) return raw;
  if (rawSet.has(raw)) return raw;
  var cached = record.proxies.get(raw);
  if (cached) return cached;
  var proxy = new Proxy(raw, {
    get: function (t, k, r) {
      if (k === "__v_isReactive") return true;
      trackRef(record);
      var v = Reflect.get(t, k, r);
      if (!isObject(v)) return v;
      if (rawSet.has(v)) return v;
      return wrapDeep(v, record);
    },
    set: function (t, k, v) {
      if (rawSet.has(t)) {
        return Reflect.set(t, k, v);
      }
      var old = t[k];
      if (Object.is(old, v)) return true;
      var ok = Reflect.set(t, k, v);
      notify(record);
      return ok;
    },
    deleteProperty: function (t, k) {
      if (rawSet.has(t)) {
        return Reflect.deleteProperty(t, k);
      }
      if (!(k in t)) return true;
      var ok = Reflect.deleteProperty(t, k);
      notify(record);
      return ok;
    },
  });
  record.proxies.set(raw, proxy);
  return proxy;
}

function readonlyWrap(raw, seen) {
  if (!isObject(raw)) return raw;
  seen = seen || new WeakMap();
  var cached = seen.get(raw);
  if (cached) return cached;
  var proxy = new Proxy(raw, {
    get: function (t, k) {
      if (k === "__v_isReadonly") return true;
      var v = Reflect.get(t, k);
      return readonlyWrap(v, seen);
    },
    set: function () {
      throw readonlyWriteError();
    },
    deleteProperty: function () {
      throw readonlyWriteError();
    },
  });
  seen.set(raw, proxy);
  return proxy;
}

// --- public API ----------------------------------------------------------

function state(value, opts) {
  var deep = !!(opts && opts.deep);
  var record = makeRecord();
  var holder = {};
  var stored = value;
  registerOwned(record);
  Object.defineProperty(holder, "value", {
    configurable: true,
    enumerable: true,
    get: function () {
      trackRef(record);
      if (!deep) return stored;
      return wrapDeep(stored, record);
    },
    set: function (v) {
      if (Object.is(stored, v)) return;
      stored = v;
      notify(record);
    },
  });
  holderSet.add(holder);
  holderRecords.set(holder, record);
  return holder;
}

function reactive(obj) {
  if (!isObject(obj)) {
    throw new TypeError("reactive(obj): obj must be an object");
  }
  if (rawSet.has(obj)) return obj;
  var record = makeRecord();
  registerOwned(record);
  var proxy = wrapDeep(obj, record);
  return proxy;
}

function readonly(value) {
  // Writable-source view: `readonly(stateHandle)` reflects source writes
  // through a live getter and throws on any write through the view.
  if (holderSet.has(value)) {
    var src = value;
    var record = holderRecords.get(src);
    var view = {};
    Object.defineProperty(view, "value", {
      configurable: false,
      enumerable: true,
      get: function () {
        if (record) trackRef(record);
        return readonlyWrap(src.value);
      },
      set: function () {
        throw readonlyWriteError();
      },
    });
    return view;
  }
  if (!isObject(value)) return value;
  return readonlyWrap(value);
}

function markRaw(value) {
  if (isObject(value)) rawSet.add(value);
  return value;
}

module.exports = {
  state: state,
  reactive: reactive,
  readonly: readonly,
  markRaw: markRaw,
  // Dependency hook for T5 (computed) / T6 (effect) / T7 (watch).
  getActiveConsumer: getActiveConsumer,
  pushActiveConsumer: pushActiveConsumer,
  popActiveConsumer: popActiveConsumer,
  trackRef: trackRef,
  getRefRecord: getRefRecord,
};
