'use strict';
// FR-MCORE lane — @muse/core reactivity surface (stub skeleton).
// MCORE fills in real behavior later without renaming these exports.
// One-way dependency: nothing in core requires native/jsx/cli.

function state(initialValue) {
  return { value: initialValue };
}

function computed(fn) {
  void fn;
  return { value: undefined };
}

function effect(fn) {
  void fn;
  throw new Error('not implemented: FR-MCORE lane');
}

function batch(fn) {
  void fn;
  throw new Error('not implemented: FR-MCORE lane');
}

function onMount(fn) {
  void fn;
  throw new Error('not implemented: FR-MCORE lane');
}

function onUnmount(fn) {
  void fn;
  throw new Error('not implemented: FR-MCORE lane');
}

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

module.exports = { state, computed, effect, batch, onMount, onUnmount, spring };

// T3 wiring (preserve through T11 rewrite): banned React-hook names throw
// MUSE_CORE_UNKNOWN_IMPORT on access from the package root.
require('./lib/instance.js').defineBannedImports(module.exports);
