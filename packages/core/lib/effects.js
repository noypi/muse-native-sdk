"use strict";

// T6 — `effect` with single cleanup (FR-MCORE-004, QD2).
//
// `effect(fn)` runs `fn` immediately, re-runs after a flush where a tracked
// dep changed, and treats a function return as the ONE cleanup: run before
// the next invocation and on dispose. Any other non-`undefined` return
// throws `MUSE_CORE_CLEANUP_ARITY` synchronously from the invocation that
// produced it. The returned disposer runs the pending cleanup exactly once
// and stops future runs.
//
// Dependency tracking reuses T4's hook (`lib/state.js`): each run executes
// under `pushActiveConsumer(consumer)` so state/reactive/readonly/computed
// reads subscribe the effect, and re-runs unsubscribe first so stale deps
// are dropped. Notification goes through T2's single shared scheduler
// (`lib/scheduler.js`): dep writes `schedule` a stable job, so multiple
// writes before a flush coalesce to one re-run by job-identity dedup.
// Effect registrations are owned by the current instance (`lib/instance.js`)
// for disposal on unmount (T9 drives the unmount half).
//
// T7 extension point: `watch`/`watchEffect` land in this same file. The
// shared helpers below — `cleanupArityError`, `unsubscribeConsumer`,
// `runTracked`, `registerOwned` — are written for reuse by the T7
// subscribe/dispose paths; T7 adds its own scheduling (post-flush
// callbacks, old/new snapshots) on top of them.

var scheduler = require("./scheduler.js");
var stateHooks = require("./state.js");

function cleanupArityError(received) {
  var err = new Error(
    "MUSE_CORE_CLEANUP_ARITY: effect fn must return undefined or exactly " +
      "one cleanup function, received " +
      describeReturn(received)
  );
  err.name = "MUSE_CORE_CLEANUP_ARITY";
  err.code = "MUSE_CORE_CLEANUP_ARITY";
  return err;
}

function describeReturn(received) {
  if (received === null) return "null";
  if (Array.isArray(received)) return "an array";
  var t = typeof received;
  if (t === "object" || t === "function") return "a " + t;
  return JSON.stringify(received);
}

// Drop every upstream subscription collected by `consumer` (a
// `{ deps: Set }` in the T4 shape, holding upstream `subs` sets).
function unsubscribeConsumer(consumer) {
  consumer.deps.forEach(function (subs) {
    try {
      subs.delete(consumer);
    } catch (e) {
      void e;
    }
  });
  consumer.deps.clear();
}

// Run `fn` with `consumer` collecting deps (T4 hook), restoring the stack
// even when `fn` throws. Returns whatever `fn` returned.
function runTracked(consumer, fn) {
  stateHooks.pushActiveConsumer(consumer);
  try {
    return fn();
  } finally {
    stateHooks.popActiveConsumer();
  }
}

// Best-effort ownership: register `disposeFn` on the current instance so
// unmount disposes it (T9). Outside a component (or when instance.js is
// unavailable) this is a no-op. Matches the `registerOwned` pattern in
// `lib/state.js`.
function registerOwned(disposeFn) {
  try {
    var instance = require("./instance.js");
    if (!instance || typeof instance.getCurrentInstance !== "function") return;
    var handle = instance.getCurrentInstance();
    if (!handle) return;
    instance.registerDisposer(handle, disposeFn);
  } catch (e) {
    void e;
  }
}

function effect(fn) {
  if (typeof fn !== "function") {
    throw new TypeError("effect(fn): fn must be a function");
  }

  var disposed = false;
  var cleanup = null;

  // Consumer registered on upstream dependency records (T4 shape).
  var consumer = { deps: new Set(), job: null };

  // Stable scheduler job: dedup by identity coalesces many writes into one
  // re-run per flush (AC-MCORE-009 effect angle). Guarded by `disposed` so
  // a job queued before dispose stays silent after it.
  function runJob() {
    if (disposed) return;
    invoke();
  }

  consumer.job = function () {
    if (disposed) return;
    scheduler.schedule(runJob);
  };

  function invoke() {
    // Pending cleanup runs exactly once before the next invocation.
    if (cleanup !== null) {
      var pending = cleanup;
      cleanup = null;
      pending();
    }
    unsubscribeConsumer(consumer);
    var ret = runTracked(consumer, fn);
    if (ret === undefined) {
      cleanup = null;
    } else if (typeof ret === "function") {
      cleanup = ret;
    } else {
      throw cleanupArityError(ret);
    }
  }

  function dispose() {
    if (disposed) return;
    disposed = true;
    unsubscribeConsumer(consumer);
    // Pending cleanup from the last invocation runs exactly once here;
    // clearing first keeps double-dispose (and a queued runJob) silent.
    if (cleanup !== null) {
      var pending = cleanup;
      cleanup = null;
      pending();
    }
  }

  registerOwned(dispose);

  // Immediate run: validation (incl. `MUSE_CORE_CLEANUP_ARITY`) surfaces
  // synchronously from this `effect()` call per the T6 contract.
  invoke();

  return dispose;
}

function watch(source, cb, opts) {
  var isGetter = typeof source === "function";
  var isRef =
    !isGetter &&
    source !== null &&
    (typeof source === "object" || typeof source === "function") &&
    "value" in Object(source);
  if (!isGetter && !isRef) {
    throw new TypeError("watch(source, cb): source must be a { value } handle or a getter function");
  }
  if (typeof cb !== "function") {
    throw new TypeError("watch(source, cb): cb must be a function");
  }
  var immediate = !!(opts && opts.immediate);
  var flushMode = opts && opts.flush;
  if (flushMode !== undefined && flushMode !== "post") {
    throw new TypeError('watch(source, cb): opts.flush must be "post"');
  }

  var disposed = false;
  var consumer = { deps: new Set(), job: null };
  // Value at the last callback invocation (setup value when none fired yet).
  var lastNotified;

  function readValue() {
    unsubscribeConsumer(consumer);
    if (isGetter) return runTracked(consumer, source);
    return runTracked(consumer, function () {
      return source.value;
    });
  }

  function checkJob() {
    if (disposed) return;
    var next = readValue();
    if (Object.is(next, lastNotified)) return;
    var prev = lastNotified;
    lastNotified = next;
    cb(next, prev);
  }

  consumer.job = function () {
    if (disposed) return;
    scheduler.schedule(checkJob);
  };

  function dispose() {
    if (disposed) return;
    disposed = true;
    unsubscribeConsumer(consumer);
  }

  registerOwned(dispose);

  lastNotified = readValue();
  if (immediate) {
    // Synchronous setup call per AC-MCORE-008; no flush-time duplicate:
    // `lastNotified` already equals the delivered newValue, so a flush
    // with no intervening change is a no-op.
    cb(lastNotified, undefined);
  }

  return dispose;
}

function watchEffect(fn) {
  if (typeof fn !== "function") {
    throw new TypeError("watchEffect(fn): fn must be a function");
  }

  var disposed = false;
  var consumer = { deps: new Set(), job: null };

  function runJob() {
    if (disposed) return;
    invoke();
  }

  consumer.job = function () {
    if (disposed) return;
    scheduler.schedule(runJob);
  };

  function invoke() {
    unsubscribeConsumer(consumer);
    runTracked(consumer, fn);
  }

  function dispose() {
    if (disposed) return;
    disposed = true;
    unsubscribeConsumer(consumer);
  }

  registerOwned(dispose);

  invoke();

  return dispose;
}

module.exports = { effect: effect, watch: watch, watchEffect: watchEffect };
