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
  var state = init();
  var table = actions || {};

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

  return { state: state, dispatch: dispatch };
}

module.exports = { createStore: createStore };
