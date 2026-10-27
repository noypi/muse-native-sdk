'use strict';
// T10 — binding runtime: two-way write-through, one-way read-only (Batch 5).
// Plan Interface (verbatim):
// `bindValue(stateHandle: { value: T }, ir: { mode: "two-way", target: string, reads: string[], writes: string[] }): { pushInputValue(v: T): void; pullStateValue(): T; dispose(): void }` — `pushInputValue` writes to the bound state in the SAME flush (no stale echo back); `bindReadonly(source: { value: T }, ir: { mode: "one-way", expr: string }, onChange: (v: unknown) => void): { dispose(): void }` — never writes back; user changes delivered only via `onChange`. After `dispose()` (wired to instance unmount) neither direction observes the other. `ir` is the verbatim shape from `packages/compiler/lib/binding.js` (required interface, no compiler edits).
// Spec refs: AC-MCORE-016 (TC-MCORE-016), AC-MCORE-017 (TC-MCORE-017).
// Depends on T2/T4/T9: state + scheduler flush + disposal wiring.
// Hermetic: node:test + node:assert/strict only; state/scheduler/effects via
// the modules under test; `ir` objects are inline literals matching the
// verbatim compiler shape (the compiler package is never required —
// out-of-scope code). No network/secrets/fs/live services.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const binding = require('../lib/binding.js');
const { bindValue, bindReadonly } = binding;
const { state } = require('../lib/state.js');
const { flush } = require('../lib/scheduler.js');
const { effect } = require('../lib/effects.js');

beforeEach(() => {
  flush();
});

// Verbatim compiler shapes (see ASSUMPTION on `kind` in unit-tests/T10.md).
function twoWayIr(name) {
  return { kind: 'binding', mode: 'two-way', target: name, reads: [name], writes: [name] };
}

function oneWayIr(expr) {
  return { kind: 'binding', mode: 'one-way', expr };
}

// ----------------- TC-MCORE-016: two-way same-flush write-through ----------

describe('TC-MCORE-016 — two-way binding writes through in the same flush', () => {
  it('pushInputValue writes to the bound state synchronously (same flush, no flush() needed)', () => {
    const s = state('a');
    const handle = bindValue(s, twoWayIr('name'));
    handle.pushInputValue('b');
    assert.equal(s.value, 'b');
    handle.dispose();
  });

  it('pullStateValue returns the pushed value and later programmatic writes render in the control', () => {
    const s = state('a');
    const handle = bindValue(s, twoWayIr('name'));
    handle.pushInputValue('b');
    assert.equal(handle.pullStateValue(), 'b');
    s.value = 'c';
    assert.equal(handle.pullStateValue(), 'c');
    handle.dispose();
  });

  it('no stale echo: one push notifies state readers exactly once per flush', () => {
    const s = state('a');
    const handle = bindValue(s, twoWayIr('name'));
    let runs = 0;
    let last;
    const stop = effect(() => {
      runs += 1;
      last = s.value;
    });
    assert.equal(runs, 1);
    handle.pushInputValue('b');
    flush();
    assert.equal(last, 'b');
    assert.equal(runs, 2);
    stop();
    handle.dispose();
  });

  it('no echo write-back: a programmatic write notifies exactly once (no bounce)', () => {
    const s = state('a');
    const handle = bindValue(s, twoWayIr('name'));
    let runs = 0;
    const stop = effect(() => {
      runs += 1;
      void s.value;
    });
    assert.equal(runs, 1);
    s.value = 'z';
    flush();
    assert.equal(runs, 2);
    assert.equal(handle.pullStateValue(), 'z');
    stop();
    handle.dispose();
  });

  it('after dispose() neither direction observes the other', () => {
    const s = state('a');
    const handle = bindValue(s, twoWayIr('name'));
    handle.dispose();
    handle.pushInputValue('b');
    assert.equal(s.value, 'a');
    s.value = 'c';
    flush();
    handle.pushInputValue('d');
    assert.equal(s.value, 'c');
  });
});

// ----------------- TC-MCORE-017: one-way never writes back -----------------

describe('TC-MCORE-017 — one-way binding never writes back, onChange-only, dead after unmount', () => {
  it('binding setup writes nothing to the source', () => {
    const src = state('x');
    const seen = [];
    const handle = bindReadonly(src, oneWayIr('label'), (v) => { seen.push(v); });
    flush();
    assert.equal(src.value, 'x');
    handle.dispose();
  });

  it('source changes are delivered to onChange and the source is never written back', () => {
    const src = state('x');
    const seen = [];
    const handle = bindReadonly(src, oneWayIr('label'), (v) => { seen.push(v); });
    src.value = 'y';
    flush();
    assert.deepEqual(seen, ['y']);
    assert.equal(src.value, 'y');
    handle.dispose();
  });

  it('the one-way handle exposes no input-write path (dispose only)', () => {
    const src = state('x');
    const handle = bindReadonly(src, oneWayIr('label'), () => {});
    assert.equal(typeof handle.dispose, 'function');
    assert.equal(handle.pushInputValue, undefined);
    assert.equal(handle.pullStateValue, undefined);
    handle.dispose();
  });

  it('after dispose() source writes no longer reach onChange', () => {
    const src = state('x');
    const seen = [];
    const handle = bindReadonly(src, oneWayIr('label'), (v) => { seen.push(v); });
    handle.dispose();
    src.value = 'y';
    flush();
    assert.deepEqual(seen, []);
  });
});
