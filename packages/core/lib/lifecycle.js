"use strict";

// T8 — mount/unmount-only lifecycle hooks (FR-MCORE-005).
//
// Registers on the current instance during the body run via T3's
// `registerHook(handle, phase, fn)`; `mount`/`unmount` in
// `lib/instance.js` fire the phases in order around attach/detach, so this
// module performs no firing itself and schedules nothing. No hook fires on
// state updates — hooks live only in the mount/unmount phase lists.
//
// Require-time half of AC-MCORE-012 (mirrors T3's `defineBannedImports`
// pattern for AC-MCORE-002): accessing any unknown lifecycle hook name on
// this module throws `MUSE_CORE_UNKNOWN_LIFECYCLE` naming the import.
// (Typecheck half — `index.d.ts` omitting the names — is T11's file.)
//
// CommonJS, dependency-free pure JS.

var instance = require('./instance.js');

var UNKNOWN_LIFECYCLE_HOOKS = ['onUpdated', 'onBeforeUpdate'];

function unknownLifecycleError(name) {
  var err = new Error(
    "MUSE_CORE_UNKNOWN_LIFECYCLE: '" +
      name +
      "' is not exported by '@muse/core' — unknown lifecycle hook"
  );
  err.name = 'MUSE_CORE_UNKNOWN_LIFECYCLE';
  err.code = 'MUSE_CORE_UNKNOWN_LIFECYCLE';
  return err;
}

function registerCurrent(phase, fn) {
  if (typeof fn !== 'function') {
    throw new TypeError(fn + ': fn must be a function');
  }
  var handle = instance.getCurrentInstance();
  if (handle === null || handle === undefined) {
    throw new Error(
      'registerHook: no current instance (hooks register on the current instance during the body run)'
    );
  }
  instance.registerHook(handle, phase, fn);
}

function onBeforeMount(fn) {
  registerCurrent('beforeMount', fn);
}

function onMounted(fn) {
  registerCurrent('mounted', fn);
}

function onBeforeUnmount(fn) {
  registerCurrent('beforeUnmount', fn);
}

function onUnmounted(fn) {
  registerCurrent('unmounted', fn);
}

module.exports = {
  onBeforeMount: onBeforeMount,
  onMounted: onMounted,
  onBeforeUnmount: onBeforeUnmount,
  onUnmounted: onUnmounted,
};

// Throwing getters for unknown lifecycle names so that accessing — or
// destructuring — them throws the named error. All other properties are
// untouched.
for (var i = 0; i < UNKNOWN_LIFECYCLE_HOOKS.length; i += 1) {
  (function (name) {
    try {
      Object.defineProperty(module.exports, name, {
        configurable: true,
        enumerable: true,
        get: function () {
          throw unknownLifecycleError(name);
        },
        set: function () {
          throw unknownLifecycleError(name);
        },
      });
    } catch (defineErr) {
      void defineErr;
    }
  })(UNKNOWN_LIFECYCLE_HOOKS[i]);
}
