'use strict';
// T1 — exactly-once lifecycle transition dispatch.
// Legal table: create -> start -> resume -> pause -> stop -> destroy,
// plus `config:<name>` self-loops that never change state.

const LEGAL_SEQUENCE = ['create', 'start', 'resume', 'pause', 'stop', 'destroy'];

const STATE_AFTER = {
  create: 'created',
  start: 'started',
  resume: 'resumed',
  pause: 'paused',
  stop: 'stopped',
  destroy: 'destroyed',
};

const INITIAL_STATE = 'init';

function unknownTransitionError(input) {
  const err = new Error(`LIFECYCLE_UNKNOWN_TRANSITION: ${String(input)}`);
  err.code = 'LIFECYCLE_UNKNOWN_TRANSITION';
  return err;
}

function isConfigVariant(transition) {
  return (
    typeof transition === 'string' &&
    transition.startsWith('config:') &&
    transition.length > 'config:'.length
  );
}

function createLifecycleMachine() {
  let state = INITIAL_STATE;
  let nextIndex = 0;
  const appliedLegal = new Set();
  const appliedConfig = new Set();
  const sequence = [];

  function append(transition) {
    sequence.push({ seq: sequence.length + 1, transition, state });
  }

  function dispatch(transition) {
    if (typeof transition !== 'string' || transition.length === 0) {
      throw unknownTransitionError(transition);
    }
    if (isConfigVariant(transition)) {
      if (appliedConfig.has(transition)) {
        return { applied: false, state };
      }
      appliedConfig.add(transition);
      append(transition);
      return { applied: true, state };
    }
    if (!Object.prototype.hasOwnProperty.call(STATE_AFTER, transition)) {
      throw unknownTransitionError(transition);
    }
    if (appliedLegal.has(transition) || LEGAL_SEQUENCE[nextIndex] !== transition) {
      return { applied: false, state };
    }
    appliedLegal.add(transition);
    nextIndex += 1;
    state = STATE_AFTER[transition];
    append(transition);
    return { applied: true, state };
  }

  function getState() {
    return state;
  }

  function getSequence() {
    return sequence.map((entry) => ({ ...entry }));
  }

  return { dispatch, getState, getSequence };
}

module.exports = { createLifecycleMachine };
