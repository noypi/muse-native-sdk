"use strict";

// T9 — instance ownership/disposal and `onErrorCaptured` (FR-MCORE-006).
//
// Thin delivery layer over the T12 seam in `lib/instance.js`:
// - `onErrorCaptured(fn)` registers `fn` on the current instance via
//   `registerErrorCapturer`; every `instance.js` throw path (`createInstance`
//   body execution, `runHooks`/`mount` hook invocation) already consults the
//   registry nearest-ancestor-first, swallowing exactly-once when a capturer
//   handles and rethrowing the identical object to the boundary caller when
//   uncaptured. This module performs no firing itself.
// - `disposeInstance(handle)` drains `handle.disposers` LIFO, each entry at
//   most once (mirroring `unmount` disposal without firing lifecycle phases),
//   continuing past individual failures and rethrowing the first error; the
//   registry is emptied so the leak check finds none, and the owned
//   effect/watch disposers silence later callbacks via their own guards.
//
// Wired into T3 `unmount` by sharing the same registry: `unmount` already
// drains `disposers`, so effects/watches stop with no retained registrations.
// Two instances own independent registries by construction.
//
// CommonJS, dependency-free pure JS.

var instance = require("./instance.js");

function onErrorCaptured(fn) {
  if (typeof fn !== "function") {
    throw new TypeError("onErrorCaptured(fn): fn must be a function");
  }
  var handle = instance.getCurrentInstance();
  if (handle === null || handle === undefined) {
    throw new Error(
      "onErrorCaptured(fn): no current instance (registers on the current instance during the body run)"
    );
  }
  instance.registerErrorCapturer(handle, fn);
}

function disposeInstance(handle) {
  if (!handle || typeof handle !== "object" || !Array.isArray(handle.disposers)) {
    throw new TypeError("disposeInstance(handle): handle must be created by createInstance");
  }
  var firstError = null;
  var disposers = handle.disposers;
  for (var i = disposers.length - 1; i >= 0; i -= 1) {
    var entry = disposers[i];
    if (!entry || entry.disposed) continue;
    entry.disposed = true;
    try {
      entry.fn();
    } catch (err) {
      if (firstError === null) firstError = err;
    }
  }
  disposers.length = 0;
  if (firstError !== null && firstError !== undefined) throw firstError;
}

module.exports = {
  onErrorCaptured: onErrorCaptured,
  disposeInstance: disposeInstance,
};
