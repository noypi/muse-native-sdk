'use strict';
// T9 — instance ownership/disposal and `onErrorCaptured` (Batch 4).
// Plan Interface (verbatim):
// `onErrorCaptured(fn: (err: unknown) => void): void` — registered like a
// lifecycle hook on the current instance; receives a descendant body/lifecycle
// error exactly once and stops further propagation; with no capturer the error
// reaches the root handler exactly once.
// `disposeInstance(handle: InstanceHandle): void` — stops its effects/watches,
// runs pending cleanups (QD2, exactly once each), drops all registrations
// (leak check finds none), and silences later callbacks.
// Spec refs: AC-MCORE-013 (TC-MCORE-013), AC-MCORE-020 (TC-MCORE-020, QD2),
// AC-MCORE-014 (TC-MCORE-014), AC-MCORE-015 (TC-MCORE-015), RULE-MCORE-002.
// Depends on T3/T6/T7/T8: instances own effects/watches (effects.js),
// lifecycle phases drive attach/detach (lifecycle.js).
// Hermetic: node:test + node:assert/strict only; instance/scheduler/state/
// effects/lifecycle via the modules under test; no network/secrets/fs/live
// services.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const scope = require('../lib/scope.js');
const { onErrorCaptured, disposeInstance } = scope;
const { createInstance, mount, unmount } = require('../lib/instance.js');
const { flush } = require('../lib/scheduler.js');
const { state } = require('../lib/state.js');
const { effect, watch } = require('../lib/effects.js');
const { onMounted } = require('../lib/lifecycle.js');

beforeEach(() => {
  flush();
});

// --------------------------------- TC-MCORE-013: unmount stops + leak check

describe('TC-MCORE-013 — unmount stops effects/watches with no retained registrations', () => {
  it('unmount stops effect re-runs and watch callbacks after unmount', () => {
    const s = state(1);
    let runs = 0;
    const seenCb = [];
    const holder = {};
    holder.handle = createInstance(() => {
      effect(() => { runs += 1; void s.value; });
      watch(s, (n, o) => { seenCb.push([n, o]); });
    });
    mount(holder.handle);
    assert.equal(runs, 1);
    s.value = 2;
    flush();
    assert.equal(runs, 2);
    assert.deepEqual(seenCb, [[2, 1]]);
    unmount(holder.handle);
    s.value = 3;
    flush();
    assert.equal(runs, 2);
    assert.deepEqual(seenCb, [[2, 1]]);
  });

  it('leak check: unmount drops all owned registrations', () => {
    const s = state(1);
    const holder = {};
    holder.handle = createInstance(() => {
      effect(() => { void s.value; });
      watch(s, () => {});
    });
    mount(holder.handle);
    assert.ok(holder.handle.disposers.length > 0);
    unmount(holder.handle);
    assert.equal(holder.handle.disposers.length, 0);
  });

  it('disposeInstance on a mounted instance stops effects/watches and drains registrations', () => {
    const s = state(1);
    let runs = 0;
    const seenCb = [];
    const handle = createInstance(() => {
      effect(() => { runs += 1; void s.value; });
      watch(s, (n, o) => { seenCb.push([n, o]); });
    });
    mount(handle);
    s.value = 2;
    flush();
    assert.equal(runs, 2);
    disposeInstance(handle);
    assert.equal(handle.disposers.length, 0);
    s.value = 3;
    flush();
    assert.equal(runs, 2);
    assert.deepEqual(seenCb, [[2, 1]]);
  });

  it('disposeInstance on a never-mounted instance disposes without throwing', () => {
    const s = state(1);
    let runs = 0;
    const handle = createInstance(() => {
      effect(() => { runs += 1; void s.value; });
    });
    assert.equal(runs, 1);
    disposeInstance(handle);
    assert.equal(handle.disposers.length, 0);
    s.value = 2;
    flush();
    assert.equal(runs, 1);
  });

  it('double disposeInstance is a safe no-op', () => {
    const s = state(1);
    let cleanups = 0;
    const handle = createInstance(() => {
      effect(() => { void s.value; return () => { cleanups += 1; }; });
    });
    mount(handle);
    disposeInstance(handle);
    assert.equal(cleanups, 1);
    disposeInstance(handle);
    assert.equal(cleanups, 1);
  });
});

// --------------------------------------- TC-MCORE-020: cleanup exactly-once

describe('TC-MCORE-020 — cleanup runs once on dispose and never after', () => {
  it('cleanup runs once before the next invocation, once on unmount, never after', () => {
    const s = state(1);
    const cleanups = [];
    let runs = 0;
    const handle = createInstance(() => {
      effect(() => {
        runs += 1;
        const v = s.value;
        return () => { cleanups.push(v); };
      });
    });
    mount(handle);
    assert.equal(runs, 1);
    assert.deepEqual(cleanups, []);
    s.value = 2;
    flush();
    assert.equal(runs, 2);
    assert.deepEqual(cleanups, [1]);
    unmount(handle);
    assert.deepEqual(cleanups, [1, 2]);
    s.value = 3;
    flush();
    assert.equal(runs, 2);
    assert.deepEqual(cleanups, [1, 2]);
  });

  it('disposeInstance runs the pending cleanup exactly once', () => {
    const s = state(1);
    let cleanups = 0;
    const handle = createInstance(() => {
      effect(() => { void s.value; return () => { cleanups += 1; }; });
    });
    mount(handle);
    assert.equal(cleanups, 0);
    disposeInstance(handle);
    assert.equal(cleanups, 1);
    s.value = 2;
    flush();
    assert.equal(cleanups, 1);
  });
});

// ------------------------------------- TC-MCORE-014: instance independence

describe('TC-MCORE-014 — two instances own independent state and effects', () => {
  it('unmounting one instance leaves the other running with state unchanged', () => {
    function makeComponent() {
      const rec = { s: state(0), runs: 0 };
      rec.handle = createInstance(() => {
        effect(() => { rec.runs += 1; void rec.s.value; });
      });
      return rec;
    }
    const a = makeComponent();
    const b = makeComponent();
    mount(a.handle);
    mount(b.handle);
    assert.equal(a.runs, 1);
    assert.equal(b.runs, 1);
    a.s.value = 1;
    b.s.value = 10;
    flush();
    assert.equal(a.runs, 2);
    assert.equal(b.runs, 2);
    unmount(a.handle);
    assert.equal(b.s.value, 10);
    b.s.value = 11;
    flush();
    assert.equal(b.runs, 3);
    assert.equal(b.s.value, 11);
    a.s.value = 2;
    flush();
    assert.equal(a.runs, 2);
  });
});

// --------------------------------- TC-MCORE-015: error capture exactly-once

describe('TC-MCORE-015 — onErrorCaptured receives a descendant error exactly once', () => {
  it('a descendant body error reaches the ancestor capturer exactly once and stops', () => {
    const boom = new Error('boom-body');
    const captured = [];
    let outerThrew = null;
    let childCreated = false;
    createInstance(() => {
      onErrorCaptured((err) => { captured.push(err); });
      try {
        createInstance(() => {
          childCreated = true;
          throw boom;
        });
      } catch (err) {
        outerThrew = err;
      }
    });
    assert.equal(childCreated, true);
    assert.equal(captured.length, 1);
    assert.equal(captured[0], boom);
    assert.equal(outerThrew, null);
  });

  it('only the nearest capturer receives the error (propagation stops)', () => {
    const boom = new Error('boom-nested');
    const outer = [];
    const inner = [];
    createInstance(() => {
      onErrorCaptured((err) => { outer.push(err); });
      createInstance(() => {
        onErrorCaptured((err) => { inner.push(err); });
        createInstance(() => { throw boom; });
      });
    });
    assert.equal(inner.length, 1);
    assert.equal(inner[0], boom);
    assert.equal(outer.length, 0);
  });

  it('a descendant lifecycle error is captured and mount does not throw', () => {
    const boom = new Error('boom-hook');
    const captured = [];
    let child = null;
    createInstance(() => {
      onErrorCaptured((err) => { captured.push(err); });
      child = createInstance(() => {
        onMounted(() => { throw boom; });
      });
    });
    assert.doesNotThrow(() => { mount(child); });
    assert.equal(captured.length, 1);
    assert.equal(captured[0], boom);
  });

  it('without a capturer the descendant lifecycle error reaches the root (mount caller) exactly once', () => {
    const boom = new Error('boom-root');
    const child = createInstance(() => {
      onMounted(() => { throw boom; });
    });
    assert.throws(() => { mount(child); }, (err) => err === boom);
  });

  it('without a capturer a descendant body error reaches the root (creation caller) exactly once', () => {
    const boom = new Error('boom-root-body');
    let throws = 0;
    try {
      createInstance(() => { throw boom; });
    } catch (err) {
      throws += 1;
      assert.equal(err, boom);
    }
    assert.equal(throws, 1);
  });

  it('registering outside a body run throws', () => {
    assert.throws(() => { onErrorCaptured(() => {}); });
  });
});
