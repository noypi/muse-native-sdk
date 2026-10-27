'use strict';
// @muse/core public surface (T11 wiring — FR-MCORE-001..007).
// Re-exports the settled runtime modules; T11 owns only this wiring.
// One-way dependency: nothing in core requires native/jsx/cli.

var scheduler = require('./lib/scheduler.js');
var stateMod = require('./lib/state.js');
var computedMod = require('./lib/computed.js');
var effectsMod = require('./lib/effects.js');
var lifecycleMod = require('./lib/lifecycle.js');
var instanceMod = require('./lib/instance.js');
var scopeMod = require('./lib/scope.js');
var bindingMod = require('./lib/binding.js');

function spring(initial) {
  var start = Number(initial);
  if (!Number.isFinite(start)) start = 0;
  var velocity = 0;
  var handle = {
    value: start,
    animateTo: function (target, opts) {
      var t = Number(target);
      if (!Number.isFinite(t)) t = 0;
      handle.value = t;
      if (opts !== null && typeof opts === 'object' && opts.velocity !== undefined) {
        var v = Number(opts.velocity);
        velocity = Number.isFinite(v) ? v : 0;
      }
    },
    getVelocity: function () {
      return velocity;
    },
  };
  return handle;
}

module.exports = {
  // T4 — state primitives.
  state: stateMod.state,
  reactive: stateMod.reactive,
  readonly: stateMod.readonly,
  markRaw: stateMod.markRaw,
  // T5 — computed.
  computed: computedMod.computed,
  // T6/T7 — effects.
  effect: effectsMod.effect,
  watch: effectsMod.watch,
  watchEffect: effectsMod.watchEffect,
  // T2 — batching.
  batch: scheduler.batch,
  // T8 — mount/unmount-only lifecycle.
  onBeforeMount: lifecycleMod.onBeforeMount,
  onMounted: lifecycleMod.onMounted,
  onBeforeUnmount: lifecycleMod.onBeforeUnmount,
  onUnmounted: lifecycleMod.onUnmounted,
  // T9 — ownership/disposal + capture.
  onErrorCaptured: scopeMod.onErrorCaptured,
  disposeInstance: scopeMod.disposeInstance,
  // T10 — binding runtime.
  bindValue: bindingMod.bindValue,
  bindReadonly: bindingMod.bindReadonly,
  // T3 — component instance model.
  createInstance: instanceMod.createInstance,
  mount: instanceMod.mount,
  unmount: instanceMod.unmount,
  getCurrentInstance: instanceMod.getCurrentInstance,
  // Out-of-scope pre-existing surface (not part of FR-MCORE; kept, not redefined).
  spring: spring,
};

// T3 wiring (preserve through T11 rewrite): banned React-hook names throw
// MUSE_CORE_UNKNOWN_IMPORT on access from the package root.
require('./lib/instance.js').defineBannedImports(module.exports);

// T8/T11 wiring: unknown lifecycle names throw MUSE_CORE_UNKNOWN_LIFECYCLE
// on access from the package root (mirrors lib/lifecycle.js getters).
(function defineUnknownLifecycleImports(target) {
  var names = ['onUpdated', 'onBeforeUpdate'];
  for (var i = 0; i < names.length; i += 1) {
    (function (name) {
      try {
        Object.defineProperty(target, name, {
          configurable: true,
          enumerable: true,
          get: function () {
            var err = new Error(
              "MUSE_CORE_UNKNOWN_LIFECYCLE: '" +
                name +
                "' is not exported by '@muse/core' — unknown lifecycle hook"
            );
            err.name = 'MUSE_CORE_UNKNOWN_LIFECYCLE';
            err.code = 'MUSE_CORE_UNKNOWN_LIFECYCLE';
            throw err;
          },
          set: function () {
            var err = new Error(
              "MUSE_CORE_UNKNOWN_LIFECYCLE: '" +
                name +
                "' is not exported by '@muse/core' — unknown lifecycle hook"
            );
            err.name = 'MUSE_CORE_UNKNOWN_LIFECYCLE';
            err.code = 'MUSE_CORE_UNKNOWN_LIFECYCLE';
            throw err;
          },
        });
      } catch (defineErr) {
        void defineErr;
      }
    })(names[i]);
  }
})(module.exports);
