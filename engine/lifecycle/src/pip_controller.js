'use strict';
// T4 — PiP controller with aspect/source-rect/actions, state transitions, playback continuity.
// FR-LIFECYCLE-008, AC-LIFECYCLE-020 (required deps: FR-LIFECYCLE-001, RULE-LIFECYCLE-003).
// State machine: fullscreen -> entering -> pip -> exiting -> fullscreen (RULE-LIFECYCLE-003);
// entering/exiting are synchronous transients, so getState() exposes the observable
// endpoints 'fullscreen' and 'pip'. Illegal transitions are no-ops returning the
// current-state shape. Playback hook fires across enter/pip/exit (continuity).

function badValue(code, message) {
  const err = new Error(`${code}: ${message}`);
  err.code = code;
  return err;
}

function checkAspectRatio(aspectRatio) {
  if (typeof aspectRatio !== 'number' || Number.isNaN(aspectRatio) || aspectRatio <= 0) {
    throw badValue(
      'PIP_BAD_ASPECT',
      `enter requires a positive aspectRatio, got ${String(aspectRatio)}`
    );
  }
}

function createPipController(hooks) {
  let state = 'fullscreen';
  let actionIds = new Set();

  function notify(mode) {
    if (hooks && typeof hooks.onPlayback === 'function') {
      hooks.onPlayback(mode);
    }
  }

  function enter(params) {
    // Illegal transition: enter outside fullscreen is a no-op.
    if (state !== 'fullscreen') {
      return { entered: false };
    }
    const aspectRatio = params ? params.aspectRatio : undefined;
    checkAspectRatio(aspectRatio);
    const actions = params && Array.isArray(params.actions) ? params.actions : [];
    // Synchronous transient through 'entering' per RULE-LIFECYCLE-003.
    state = 'entering';
    actionIds = new Set();
    for (const action of actions) {
      if (action && typeof action.id === 'string' && action.id.length > 0) {
        actionIds.add(action.id);
      }
    }
    state = 'pip';
    notify('pip');
    return { entered: true };
  }

  function dispatchAction(actionId) {
    if (state !== 'pip') {
      return { dispatched: false };
    }
    if (!actionIds.has(actionId)) {
      // PIP_UNKNOWN_ACTION: reported as { dispatched: false }, never thrown.
      return { dispatched: false };
    }
    notify('pip');
    return { dispatched: true };
  }

  function exit() {
    // Illegal transition: exit outside pip is a no-op staying fullscreen.
    if (state !== 'pip') {
      return { restored: false };
    }
    // Synchronous transient through 'exiting' per RULE-LIFECYCLE-003.
    state = 'exiting';
    actionIds = new Set();
    state = 'fullscreen';
    notify('fullscreen');
    return { restored: true };
  }

  function getState() {
    return state;
  }

  return { enter, dispatchAction, exit, getState };
}

module.exports = { createPipController };
