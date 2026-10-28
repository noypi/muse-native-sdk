'use strict';
// T3 — back-gesture end-to-end sync including root back-to-home.
// FR-LIFECYCLE-007, AC-LIFECYCLE-019 (required dep: FR-INPUT-006 contract).
// Pure composition over the T1 dispatcher surface ({ handleBack }) and the T2
// progress surface ({ onProgress }): swipe feeds each step through onProgress
// 1:1 in order (frames = applied count); offScreen is true iff every step
// applied and the dispatcher consumed the completion. Unconsumed input yields
// { offScreen: false } / { wentHome: true } and never throws.

function safeHandleBack(dispatcher, eventId) {
  if (!dispatcher || typeof dispatcher.handleBack !== 'function') {
    return null;
  }
  try {
    return dispatcher.handleBack(eventId);
  } catch (_err) {
    return null;
  }
}

function safeApplyStep(progress, value) {
  if (!progress || typeof progress.onProgress !== 'function') {
    return false;
  }
  try {
    const result = progress.onProgress(value, 'engine');
    return !!(result && result.applied === true);
  } catch (_err) {
    return false;
  }
}

function createBackGesture(dispatcher, progress) {
  function swipe(destinationId, steps) {
    if (!Array.isArray(steps) || steps.length === 0) {
      return { offScreen: false, frames: 0 };
    }
    let frames = 0;
    for (const step of steps) {
      if (safeApplyStep(progress, step)) {
        frames += 1;
      }
    }
    if (frames !== steps.length) {
      return { offScreen: false, frames };
    }
    const done = safeHandleBack(dispatcher, String(destinationId));
    if (done && done.consumed === true) {
      return { offScreen: true, frames };
    }
    return { offScreen: false, frames };
  }

  function pressBackFromRoot() {
    const done = safeHandleBack(dispatcher, 'root');
    if (done && typeof done.wentHome === 'boolean') {
      return { wentHome: done.wentHome };
    }
    return { wentHome: true };
  }

  return { swipe, pressBackFromRoot };
}

module.exports = { createBackGesture };
