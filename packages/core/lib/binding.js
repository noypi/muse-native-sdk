"use strict";

// T10 — binding runtime: two-way write-through, one-way read-only (FR-MCORE-007).
//
// `bindValue(stateHandle, ir)` wires a two-way compiler IR
// (`{ mode: "two-way", target, reads, writes }`, with or without the verbatim
// compiler `kind: "binding"` field) to a `state` holder: programmatic
// `stateHandle.value = v` writes render via `pullStateValue()`, and
// `pushInputValue(v)` writes land in state synchronously (same flush). A
// `pushing` guard flag suppresses the echo write-back of a just-pushed value
// so each push/programmatic write notifies readers exactly once.
//
// `bindReadonly(source, ir, onChange)` wires a one-way compiler IR
// (`{ mode: "one-way", expr }`, `kind`-tolerant) to a `state`/computed
// source: source changes are delivered to `onChange` post-flush via `watch`
// and the binding never writes back to the source.
//
// Both handles expose idempotent `dispose()` (also registered on the current
// instance when inside a component body, so unmount severs both directions):
// after `dispose()`, pushes are inert and source writes no longer deliver.
//
// CommonJS, dependency-free pure JS (intra-package `effects`/`instance`
// requires only, matching the `lib/state.js` ownership pattern).

var effects = require("./effects.js");

function assertStateHandle(handle, name) {
  if (
    handle === null ||
    handle === undefined ||
    (typeof handle !== "object" && typeof handle !== "function") ||
    !("value" in Object(handle))
  ) {
    throw new TypeError(name + ": must be a { value } handle");
  }
}

function assertTwoWayIr(ir) {
  if (ir === null || ir === undefined || typeof ir !== "object") {
    throw new TypeError('bindValue(stateHandle, ir): ir must be a { mode: "two-way", ... } object');
  }
  if (ir.mode !== "two-way") {
    throw new TypeError('bindValue(stateHandle, ir): ir.mode must be "two-way"');
  }
}

function assertOneWayIr(ir) {
  if (ir === null || ir === undefined || typeof ir !== "object") {
    throw new TypeError('bindReadonly(source, ir, onChange): ir must be a { mode: "one-way", ... } object');
  }
  if (ir.mode !== "one-way") {
    throw new TypeError('bindReadonly(source, ir, onChange): ir.mode must be "one-way"');
  }
}

// Best-effort ownership: register `disposeFn` on the current instance so
// unmount severs the binding. Outside a component this is a no-op (mirrors
// `lib/state.js` / `lib/effects.js`).
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

function bindValue(stateHandle, ir) {
  assertStateHandle(stateHandle, "bindValue(stateHandle, ir): stateHandle");
  assertTwoWayIr(ir);
  // `ir` is the verbatim compiler shape (required interface, no compiler
  // edits): only `mode` is consulted; extra fields such as `kind`,
  // `target`, `reads`, `writes` are tolerated and never evaluated.
  void ir;

  var disposed = false;
  var pushing = false;

  function pushInputValue(v) {
    if (disposed) return;
    if (pushing) return;
    pushing = true;
    try {
      stateHandle.value = v;
    } finally {
      pushing = false;
    }
  }

  function pullStateValue() {
    return stateHandle.value;
  }

  function dispose() {
    if (disposed) return;
    disposed = true;
  }

  registerOwned(dispose);

  return {
    pushInputValue: pushInputValue,
    pullStateValue: pullStateValue,
    dispose: dispose,
  };
}

function bindReadonly(source, ir, onChange) {
  assertStateHandle(source, "bindReadonly(source, ir, onChange): source");
  assertOneWayIr(ir);
  // Verbatim compiler shape: only `mode` is consulted; `kind`/`expr` are
  // tolerated and never evaluated.
  void ir;
  if (typeof onChange !== "function") {
    throw new TypeError("bindReadonly(source, ir, onChange): onChange must be a function");
  }

  var disposed = false;
  var stop = effects.watch(source, function (next) {
    if (disposed) return;
    onChange(next);
  });

  function dispose() {
    if (disposed) return;
    disposed = true;
    stop();
  }

  registerOwned(dispose);

  return { dispose: dispose };
}

module.exports = {
  bindValue: bindValue,
  bindReadonly: bindReadonly,
};
