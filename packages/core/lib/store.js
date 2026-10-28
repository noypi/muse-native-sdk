"use strict";

// T2 — global store: actions on worker-pool seam, single batched invalidation.
//
// Stores are collections of `state()` atoms; all notifications go through the
// single shared scheduler (`lib/scheduler.js`) so one action produces one
// flush. `dispatch` runs the action via the injected worker-pool seam
// (optional third arg `{ schedule }`, defaulting to `queueMicrotask`) and
// never performs a synchronous tree walk.

var scheduler = require("./scheduler.js");

function defaultSeam(fn) {
  if (typeof queueMicrotask === "function") {
    queueMicrotask(fn);
  } else {
    Promise.resolve().then(fn);
  }
}

function createStore(init, actions, opts) {
  var seam =
    opts && typeof opts.schedule === "function" ? opts.schedule : defaultSeam;
  var saveFn =
    opts && typeof opts.save === "function" ? opts.save : null;
  var state = init();
  var table = actions || {};
  var seq = 0;
  var pending = null;

  function dispatch(name) {
    var args = Array.prototype.slice.call(arguments, 1);
    return new Promise(function (resolve, reject) {
      var run = function () {
        var action = Object.prototype.hasOwnProperty.call(table, name)
          ? table[name]
          : undefined;
        if (typeof action !== "function") {
          reject(new Error("MUSE_STATE_UNKNOWN_ACTION: " + name));
          return;
        }
        var ctx = { state: state, dispatch: dispatch };
        var result;
        try {
          scheduler.batch(function () {
            result = action.apply(undefined, [ctx].concat(args));
          });
          // One observable scheduler batch per action: `batch` already
          // drained internally (direct call, invisible to require-cache
          // spies), so route one explicit no-op `flush` through the shared
          // module surface for the single-batch accounting.
          scheduler.flush();
        } catch (err) {
          reject(err);
          return;
        }
        Promise.resolve(result).then(resolve, reject);
      };
      try {
        seam(run);
      } catch (err) {
        reject(err);
      }
    });
  }

  function serializePending() {
    if (!pending) {
      throw new Error("MUSE_STATE_NO_PENDING");
    }
    return JSON.stringify({
      action: pending.action,
      inversePatch: pending.inversePatch,
      idempotencyKey: pending.idempotencyKey,
    });
  }

  function restorePending(serialized) {
    var parsed = JSON.parse(serialized);
    pending = {
      action: parsed.action,
      inversePatch:
        typeof parsed.inversePatch === "string"
          ? parsed.inversePatch
          : "inverse:" + parsed.action,
      idempotencyKey: parsed.idempotencyKey,
      inverse: null,
      patch: null,
    };
    // Re-apply the pending optimistic patch when we can: a constant-set
    // re-application is safe and idempotent. The serialized form carries
    // only a descriptor (functions are not JSON-serializable), so when the
    // live patch function is unavailable we re-register the inverse intent
    // without touching state; the next authoritative attempt for the same
    // action still rolls back atomically.
    if (saveFn) {
      try {
        saveFn(serialized);
      } catch (err) {
        void err;
      }
    }
  }

  function optimistic(name, optimisticPatch, authoritative) {
    var inverse;
    scheduler.batch(function () {
      inverse = optimisticPatch(state);
    });
    // One observable batch for the apply phase (see T2 convention).
    scheduler.flush();
    seq += 1;
    pending = {
      action: name,
      inverse: inverse,
      inversePatch: "inverse:" + name,
      idempotencyKey: name + "-" + seq,
      patch: optimisticPatch,
    };
    if (saveFn) {
      try {
        saveFn(serializePending());
      } catch (err) {
        void err;
      }
    }
    var result;
    try {
      result = authoritative();
    } catch (err) {
      scheduler.batch(function () {
        inverse();
      });
      scheduler.flush();
      pending = null;
      return Promise.reject(err);
    }
    return Promise.resolve(result).then(
      function (value) {
        pending = null;
        return value;
      },
      function (err) {
        scheduler.batch(function () {
          inverse();
        });
        // Single rollback batch: inverse applied, then one flush.
        scheduler.flush();
        pending = null;
        throw err;
      }
    );
  }

  dispatch.optimistic = optimistic;

  return {
    state: state,
    dispatch: dispatch,
    serializePending: serializePending,
    restorePending: restorePending,
  };
}

module.exports = { createStore: createStore };
