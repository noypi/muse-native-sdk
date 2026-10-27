'use strict';
// T8 — mount/unmount-only lifecycle hooks (Batch 4).
// Plan Interface (verbatim):
// `onBeforeMount(fn: () => void): void` — before subtree attach;
// `onMounted(fn: () => void): void` — after attach;
// `onBeforeUnmount(fn: () => void): void` — before detach;
// `onUnmounted(fn: () => void): void` — after detach.
// Each fires exactly once per instance. Importing any other lifecycle hook name
// (e.g. `onUpdated`, `onBeforeUpdate`) fails typecheck and throws
// `MUSE_CORE_UNKNOWN_LIFECYCLE` naming the import.
// Spec refs: AC-MCORE-011 (TC-MCORE-011), AC-MCORE-012 (TC-MCORE-012),
// RULE-MCORE-004.
// Hermetic: node:test + node:assert/strict only; instance/scheduler/state via
// the modules under test; no network/secrets/fs/live services.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const lifecycle = require('../lib/lifecycle.js');
const { onBeforeMount, onMounted, onBeforeUnmount, onUnmounted } = lifecycle;
const { createInstance, mount, unmount } = require('../lib/instance.js');
const { flush } = require('../lib/scheduler.js');
const { state } = require('../lib/state.js');

beforeEach(() => {
  flush();
});

// ------------------------------------------------------- TC-MCORE-011: order

describe('TC-MCORE-011 — four hooks fire exactly once each in phase order', () => {
  it('exposes the four mount/unmount hooks as functions', () => {
    assert.equal(typeof onBeforeMount, 'function');
    assert.equal(typeof onMounted, 'function');
    assert.equal(typeof onBeforeUnmount, 'function');
    assert.equal(typeof onUnmounted, 'function');
  });

  it('fires beforeMount+mounted on mount and beforeUnmount+unmounted on unmount, in order', () => {
    const order = [];
    const handle = createInstance(() => {
      onBeforeMount(() => { order.push('beforeMount'); });
      onMounted(() => { order.push('mounted'); });
      onBeforeUnmount(() => { order.push('beforeUnmount'); });
      onUnmounted(() => { order.push('unmounted'); });
    });
    assert.deepEqual(order, []);
    mount(handle);
    assert.deepEqual(order, ['beforeMount', 'mounted']);
    unmount(handle);
    assert.deepEqual(order, ['beforeMount', 'mounted', 'beforeUnmount', 'unmounted']);
  });

  it('observes attach state: before-hooks see detached, after-hooks see attached', () => {
    const seen = {};
    let handle;
    handle = createInstance(() => {
      onBeforeMount(() => { seen.beforeMount = handle.attached; });
      onMounted(() => { seen.mounted = handle.attached; });
      onBeforeUnmount(() => { seen.beforeUnmount = handle.attached; });
      onUnmounted(() => { seen.unmounted = handle.attached; });
    });
    mount(handle);
    assert.equal(seen.beforeMount, false);
    assert.equal(seen.mounted, true);
    unmount(handle);
    assert.equal(seen.beforeUnmount, true);
    assert.equal(seen.unmounted, false);
  });

  it('fires each registration exactly once (two onMounted registrations each run once)', () => {
    let a = 0;
    let b = 0;
    const handle = createInstance(() => {
      onMounted(() => { a += 1; });
      onMounted(() => { b += 1; });
    });
    mount(handle);
    assert.equal(a, 1);
    assert.equal(b, 1);
    unmount(handle);
    assert.equal(a, 1);
    assert.equal(b, 1);
  });

  it('double mount does not refire hooks (mount is idempotent)', () => {
    let count = 0;
    const handle = createInstance(() => {
      onBeforeMount(() => { count += 1; });
      onMounted(() => { count += 1; });
    });
    mount(handle);
    mount(handle);
    assert.equal(count, 2);
    unmount(handle);
  });

  it('registering outside a body run throws (hooks register on the current instance)', () => {
    assert.throws(() => { onMounted(() => {}); });
  });
});

// ------------------------------------------------- TC-MCORE-012: no update /
// unknown hooks

describe('TC-MCORE-012 — no hook fires on state update', () => {
  it('state writes + flush after mount fire no hook', () => {
    const s = state(1);
    const calls = [];
    const handle = createInstance(() => {
      onBeforeMount(() => { calls.push('beforeMount'); });
      onMounted(() => { calls.push('mounted'); });
      onBeforeUnmount(() => { calls.push('beforeUnmount'); });
      onUnmounted(() => { calls.push('unmounted'); });
      void s.value;
    });
    mount(handle);
    assert.deepEqual(calls, ['beforeMount', 'mounted']);
    s.value = 2;
    flush();
    s.value = 3;
    flush();
    assert.deepEqual(calls, ['beforeMount', 'mounted']);
    unmount(handle);
    assert.deepEqual(calls, ['beforeMount', 'mounted', 'beforeUnmount', 'unmounted']);
  });
});

describe('TC-MCORE-012 — unknown lifecycle hook imports fail named', () => {
  for (const name of ['onUpdated', 'onBeforeUpdate']) {
    it(`accessing '${name}' throws MUSE_CORE_UNKNOWN_LIFECYCLE naming the import`, () => {
      assert.throws(
        () => { void lifecycle[name]; },
        (err) => {
          assert.equal(err && err.name, 'MUSE_CORE_UNKNOWN_LIFECYCLE');
          assert.match(String(err && err.message), new RegExp(name));
          assert.match(String(err && err.message), /MUSE_CORE_UNKNOWN_LIFECYCLE/);
          return true;
        }
      );
    });
  }
});
