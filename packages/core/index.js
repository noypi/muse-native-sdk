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

module.exports = { state, computed, effect, batch, onMount, onUnmount };
