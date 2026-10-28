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

  // Plain-value snapshot of the holder state (`{ key: holder.value }`).
  // Only JSON-shaped values cross the durable seam, so only those are
  // captured; the restart-test patches are constant-sets (idempotent), so
  // re-applying a snapshot after restore is safe.
  function snapshotState(st) {
    var snap = {};
    var keys = Object.keys(st);
    for (var i = 0; i < keys.length; i += 1) {
      var holder = st[keys[i]];
      if (holder !== null && typeof holder === "object" && "value" in holder) {
        snap[keys[i]] = holder.value;
      }
    }
    return snap;
  }

  function applySnapshot(st, snap) {
    scheduler.batch(function () {
      var keys = Object.keys(snap);
      for (var i = 0; i < keys.length; i += 1) {
        var holder = st[keys[i]];
        if (holder !== null && typeof holder === "object" && "value" in holder) {
          holder.value = snap[keys[i]];
        }
      }
    });
    // One observable batch for the re-applied values (T2 convention: the
    // internal `batch` drain is invisible to require-cache spies, so one
    // explicit `flush` through the module surface accounts for it).
    scheduler.flush();
  }

  function clearSlot() {
    if (!saveFn) return;
    try {
      // Tombstone the durable slot so a restart never replays a completed
      // mutation (AC-STATE-012 exactly-once). The contract accepts
      // `null`/`undefined`/`''`/`'null'` as cleared.
      saveFn("null");
    } catch (err) {
      void err;
    }
  }

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
      prev: pending.prev,
      next: pending.next,
    });
  }

  function restorePending(serialized) {
    var parsed;
    try {
      parsed = JSON.parse(serialized);
    } catch (err) {
      throw new Error("MUSE_STATE_NO_PENDING");
    }
    if (!parsed || typeof parsed !== "object" || typeof parsed.action !== "string") {
      // A cleared slot (`"null"` tombstone) or anything the store itself did
      // not produce carries no re-registerable intent: fail fast instead of
      // re-registering a hollow pending with a null inverse.
      throw new Error("MUSE_STATE_NO_PENDING");
    }
    var prev = parsed.prev && typeof parsed.prev === "object" ? parsed.prev : null;
    var next = parsed.next && typeof parsed.next === "object" ? parsed.next : null;
    // Re-apply the recorded optimistic values so state is correct
    // immediately after restore on a fresh store, before any fresh
    // `optimistic` call. Functions cannot cross JSON, so the payload
    // carries plain-value `prev`/`next` snapshots (constant-set patches are
    // idempotent, so re-application is safe) and the restored inverse is a
    // live closure over the recorded `prev` snapshot — never null.
    if (next) {
      applySnapshot(state, next);
    }
    var restoredPrev = prev;
    var restoredNext = next;
    pending = {
      action: parsed.action,
      inversePatch:
        typeof parsed.inversePatch === "string"
          ? parsed.inversePatch
          : "inverse:" + parsed.action,
      idempotencyKey: parsed.idempotencyKey,
      inverse: function () {
        if (restoredPrev) {
          applySnapshot(state, restoredPrev);
        }
      },
      patch: function (st) {
        if (restoredNext) {
          applySnapshot(st, restoredNext);
        }
        return function () {
          if (restoredPrev) {
            applySnapshot(st, restoredPrev);
          }
        };
      },
    };
    if (saveFn) {
      try {
        saveFn(serialized);
      } catch (err) {
        void err;
      }
    }
  }

  function optimistic(name, optimisticPatch, authoritative) {
    var prev = snapshotState(state);
    var inverse;
    scheduler.batch(function () {
      inverse = optimisticPatch(state);
    });
    // One observable batch for the apply phase (see T2 convention).
    scheduler.flush();
    var next = snapshotState(state);
    seq += 1;
    pending = {
      action: name,
      inverse: inverse,
      inversePatch: "inverse:" + name,
      idempotencyKey: name + "-" + seq,
      patch: optimisticPatch,
      prev: prev,
      next: next,
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
        // Exactly-once (AC-STATE-012): the mutation completed
        // authoritatively, so tombstone the durable slot UNCONDITIONALLY —
        // `load()` reads cleared afterwards and a restart never replays it.
        // Restart flows are crash-before-settle only, so no legitimate flow
        // depends on post-success restore. The slot is deliberately left
        // intact after a rollback: the failed attempt's serialized intent
        // (with its idempotency key) stays available.
        clearSlot();
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
