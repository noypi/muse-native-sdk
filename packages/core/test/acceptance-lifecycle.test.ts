'use strict';
// Acceptance/integration: cross-module lifecycle in one flow (tester-authored).
// Covers AC-MCORE-001/003/006/007/008/009/011/013/016/017/020 together:
// component mount → state write → computed → effect/watch → binding
// write-through → unmount disposal. Hermetic: node:test + node:assert/strict
// only; exercises the public lib surface, no mocks, no network/secrets/fs.
const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createInstance, mount, unmount } = require('../lib/instance.js');
const { state } = require('../lib/state.js');
const { computed } = require('../lib/computed.js');
const { effect, watch, watchEffect } = require('../lib/effects.js');
const { flush } = require('../lib/scheduler.js');
const { onBeforeMount, onMounted, onBeforeUnmount, onUnmounted } = require('../lib/lifecycle.js');
const { bindValue, bindReadonly } = require('../lib/binding.js');

describe('acceptance: full component lifecycle in one flow', () => {
  it('mount → write → computed → observers → binding → unmount disposal', () => {
    let bodyRuns = 0;
    const hooks = [];
    const seen = {};
    let refs = null;

    const h = createInstance(() => {
      bodyRuns++;
      onBeforeMount(() => hooks.push('beforeMount'));
      onMounted(() => hooks.push('mounted'));
      onBeforeUnmount(() => hooks.push('beforeUnmount'));
      onUnmounted(() => hooks.push('unmounted'));

      const count = state(1);
      const doubled = computed(() => count.value * 2);
      const effectLog = [];
      const cleanups = [];
      effect(() => {
        effectLog.push(doubled.value);
        return () => cleanups.push(doubled.value);
      });
      const watchCalls = [];
      watch(count, (nv, ov) => watchCalls.push([nv, ov]));
      let watchEffectRuns = 0;
      watchEffect(() => { void count.value; watchEffectRuns++; });

      const twoWay = bindValue(count, { kind: 'binding', mode: 'two-way', target: 'input', reads: ['v'], writes: ['v'] });
      const roCalls = [];
      const oneWay = bindReadonly(doubled, { kind: 'binding', mode: 'one-way', expr: 'doubled' }, (v) => roCalls.push(v));

      refs = { count, doubled, effectLog, cleanups, watchCalls, twoWay, oneWay, roCalls, getWatchEffectRuns: () => watchEffectRuns };
    });
    assert.equal(bodyRuns, 1);

    mount(h);
    flush();
    assert.deepEqual(hooks, ['beforeMount', 'mounted']);
    assert.equal(bodyRuns, 1); // AC-MCORE-001: mount does not re-run body
    assert.deepEqual(refs.effectLog, [2]); // effect immediate run (AC-MCORE-008)
    assert.equal(refs.getWatchEffectRuns(), 1);

    // Input-originated write: state lands synchronously (same flush, AC-MCORE-016);
    // computed-derived views settle post-flush per the flush-settled contract
    // (unit tests always flush before asserting computed values, AC-MCORE-007).
    refs.twoWay.pushInputValue(5);
    assert.equal(refs.count.value, 5);
    assert.equal(refs.twoWay.pullStateValue(), 5);
    flush();
    assert.equal(refs.doubled.value, 10); // computed sees update, no glitch (AC-MCORE-007)
    assert.deepEqual(refs.effectLog, [2, 10]); // effect re-ran once after flush
    assert.deepEqual(refs.watchCalls, [[5, 1]]); // watch (newValue, oldValue) (AC-MCORE-008)
    assert.equal(refs.getWatchEffectRuns(), 2);
    assert.deepEqual(refs.roCalls, [10]); // one-way onChange-only delivery (AC-MCORE-017)

    // Programmatic write renders in the control (AC-MCORE-016 reverse direction)
    refs.count.value = 7;
    assert.equal(refs.twoWay.pullStateValue(), 7);
    flush();
    assert.equal(refs.doubled.value, 14);

    // Three synchronous writes → one flush, one rerun each (AC-MCORE-009)
    const n0 = refs.effectLog.length;
    const w0 = refs.watchCalls.length;
    refs.count.value = 11; refs.count.value = 12; refs.count.value = 13;
    flush();
    assert.equal(refs.effectLog.length, n0 + 1);
    assert.equal(refs.watchCalls.length, w0 + 1);
    assert.deepEqual(refs.watchCalls[refs.watchCalls.length - 1], [13, 7]);

    // One-way binding never writes back (AC-MCORE-017): no push path
    assert.equal(typeof refs.oneWay.pushInputValue, 'undefined');

    // Unmount: disposal stops everything, cleanup exactly once (AC-MCORE-013/020)
    const cleanCount = refs.cleanups.length;
    unmount(h);
    assert.deepEqual(hooks, ['beforeMount', 'mounted', 'beforeUnmount', 'unmounted']);
    assert.equal(refs.cleanups.length, cleanCount + 1); // pending cleanup once on dispose
    const e1 = refs.effectLog.length, w1 = refs.watchCalls.length, r1 = refs.roCalls.length;
    refs.count.value = 99;
    flush();
    assert.equal(refs.effectLog.length, e1); // no effect after unmount
    assert.equal(refs.watchCalls.length, w1); // no watch after unmount
    assert.equal(refs.roCalls.length, r1); // binding dead after unmount
    assert.equal(bodyRuns, 1); // body never re-ran
    refs.twoWay.dispose();
    refs.oneWay.dispose();
  });
});
