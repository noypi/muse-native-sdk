'use strict';
// T1 — exactly-once lifecycle transition dispatch (Batch 1).
// Plan Interface (verbatim):
// `createLifecycleMachine(): { dispatch(transition: string): { applied: boolean, state: string }, getState(): string, getSequence(): Array<{seq: number, transition: string, state: string}> }` — duplicate/out-of-order transition returns `{applied:false}` without changing state; unknown transition throws `LIFECYCLE_UNKNOWN_TRANSITION` naming the input.
// Spec refs: FR-LIFECYCLE-001, AC-LIFECYCLE-001 (TC-LIFECYCLE-001).
// Mirrors TC-LIFECYCLE-001 (transition_dispatch_test.cpp).
// Hermetic: node:test + node:assert/strict only; no network/fs/secrets/live services.
// ASSUMPTION (A2): JNI bridge modeled as pure mapping; hermetic node:test +
// node:assert/strict following packages/core/test pattern.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const { createLifecycleMachine } = require('../src/state_machine.js');

const LEGAL_SEQUENCE = ['create', 'start', 'resume', 'pause', 'stop', 'destroy'];
const CONFIG_VARIANTS = ['config:locale', 'config:orientation', 'config:darkMode'];

function assertUnknownTransitionThrows(fn, input) {
  assert.throws(
    fn,
    (err) => {
      assert.match(String((err && err.code) || ''), /LIFECYCLE_UNKNOWN_TRANSITION/);
      assert.match(String((err && err.message) || ''), /LIFECYCLE_UNKNOWN_TRANSITION/);
      assert.match(String((err && err.message) || ''), new RegExp(String(input)));
      return true;
    },
    `expected LIFECYCLE_UNKNOWN_TRANSITION naming ${JSON.stringify(input)}`
  );
}

describe('TC-LIFECYCLE-001 — legal transitions apply exactly once', () => {
  let machine;
  beforeEach(() => {
    machine = createLifecycleMachine();
  });

  it('exposes dispatch/getState/getSequence with the contracted shapes', () => {
    assert.equal(typeof machine.dispatch, 'function');
    assert.equal(typeof machine.getState, 'function');
    assert.equal(typeof machine.getSequence, 'function');
    assert.equal(typeof machine.getState(), 'string');
    assert.deepEqual(machine.getSequence(), []);
  });

  it('applies the full create→start→resume→pause→stop→destroy sequence once each', () => {
    for (const transition of LEGAL_SEQUENCE) {
      const result = machine.dispatch(transition);
      assert.equal(result.applied, true, `transition ${transition} should apply`);
      assert.equal(typeof result.state, 'string');
      assert.equal(machine.getState(), result.state);
    }
    assert.equal(machine.getSequence().length, LEGAL_SEQUENCE.length);
  });

  it('repeat dispatch of an applied transition returns applied:false with state unchanged', () => {
    const first = machine.dispatch('create');
    assert.equal(first.applied, true);
    const before = machine.getState();
    const beforeLen = machine.getSequence().length;
    const repeat = machine.dispatch('create');
    assert.deepEqual(repeat, { applied: false, state: before });
    assert.equal(machine.getState(), before);
    assert.equal(machine.getSequence().length, beforeLen);
  });

  it('every legal transition is idempotent: second dispatch leaves state and sequence unchanged', () => {
    for (const transition of LEGAL_SEQUENCE) {
      const first = machine.dispatch(transition);
      assert.equal(first.applied, true, `first ${transition} should apply`);
      const stateAfter = machine.getState();
      const lenAfter = machine.getSequence().length;
      const second = machine.dispatch(transition);
      assert.equal(second.applied, false, `repeat ${transition} must not re-apply`);
      assert.equal(machine.getState(), stateAfter);
      assert.equal(machine.getSequence().length, lenAfter);
    }
  });

  it('out-of-order transition returns applied:false without changing state', () => {
    const before = machine.getState();
    const beforeLen = machine.getSequence().length;
    const result = machine.dispatch('resume');
    assert.equal(result.applied, false);
    assert.equal(result.state, before);
    assert.equal(machine.getState(), before);
    assert.equal(machine.getSequence().length, beforeLen);
  });

  it('skipped transition after partial progress returns applied:false without changing state', () => {
    machine.dispatch('create');
    const before = machine.getState();
    const beforeLen = machine.getSequence().length;
    const result = machine.dispatch('pause');
    assert.equal(result.applied, false);
    assert.equal(machine.getState(), before);
    assert.equal(machine.getSequence().length, beforeLen);
  });
});

describe('TC-LIFECYCLE-001 — config variants never alter state', () => {
  let machine;
  beforeEach(() => {
    machine = createLifecycleMachine();
  });

  for (const variant of CONFIG_VARIANTS) {
    it(`config variant '${variant}' applies once without changing state`, () => {
      const before = machine.getState();
      const beforeLen = machine.getSequence().length;
      const result = machine.dispatch(variant);
      assert.equal(result.applied, true);
      assert.equal(machine.getState(), before);
      assert.equal(result.state, before);
      assert.equal(machine.getSequence().length, beforeLen + 1);
    });

    it(`repeated config variant '${variant}' returns applied:false with sequence unchanged`, () => {
      const first = machine.dispatch(variant);
      assert.equal(first.applied, true);
      const before = machine.getState();
      const beforeLen = machine.getSequence().length;
      const repeat = machine.dispatch(variant);
      assert.equal(repeat.applied, false);
      assert.equal(machine.getState(), before);
      assert.equal(machine.getSequence().length, beforeLen);
    });
  }

  it('config variant mid-sequence leaves the lifecycle state untouched', () => {
    machine.dispatch('create');
    machine.dispatch('start');
    const before = machine.getState();
    const result = machine.dispatch('config:locale');
    assert.equal(result.applied, true);
    assert.equal(machine.getState(), before);
    const next = machine.dispatch('resume');
    assert.equal(next.applied, true);
  });
});

describe('TC-LIFECYCLE-001 — unknown transitions throw LIFECYCLE_UNKNOWN_TRANSITION', () => {
  let machine;
  beforeEach(() => {
    machine = createLifecycleMachine();
  });

  for (const input of ['bogus', 'CREATE', 'destroy-now', 'config:', '']) {
    it(`unknown transition ${JSON.stringify(input)} throws naming the input`, () => {
      assertUnknownTransitionThrows(() => machine.dispatch(input), input);
    });
  }

  it('throwing on unknown input leaves state and sequence unchanged', () => {
    machine.dispatch('create');
    const before = machine.getState();
    const beforeLen = machine.getSequence().length;
    assertUnknownTransitionThrows(() => machine.dispatch('bogus-transition'), 'bogus-transition');
    assert.equal(machine.getState(), before);
    assert.equal(machine.getSequence().length, beforeLen);
  });

  it('non-string input throws LIFECYCLE_UNKNOWN_TRANSITION without leaking internals', () => {
    for (const input of [null, undefined, 42]) {
      assert.throws(
        () => machine.dispatch(input),
        (err) => {
          assert.match(String((err && err.message) || ''), /LIFECYCLE_UNKNOWN_TRANSITION/);
          return true;
        }
      );
    }
  });
});

describe('TC-LIFECYCLE-001 — monotonic sequence log for observability', () => {
  it('sequence entries carry monotonically increasing seq numbers with transition and state', () => {
    const machine = createLifecycleMachine();
    for (const transition of LEGAL_SEQUENCE) {
      machine.dispatch(transition);
    }
    const seq = machine.getSequence();
    assert.equal(seq.length, LEGAL_SEQUENCE.length);
    seq.forEach((entry, index) => {
      assert.equal(entry.seq, index + 1);
      assert.equal(entry.transition, LEGAL_SEQUENCE[index]);
      assert.equal(typeof entry.state, 'string');
    });
    assert.equal(seq[seq.length - 1].state, machine.getState());
  });

  it('unapplied dispatches never append to the sequence log', () => {
    const machine = createLifecycleMachine();
    machine.dispatch('create');
    const len = machine.getSequence().length;
    machine.dispatch('create');
    machine.dispatch('pause');
    assert.equal(machine.getSequence().length, len);
  });
});
