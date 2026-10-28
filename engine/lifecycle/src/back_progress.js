'use strict';
// T2 — predictive-back progress stream driving nav transition directly with single-consumer arbitration.
// FR-LIFECYCLE-007, AC-LIFECYCLE-018 (required deps: FR-LIFECYCLE-001, RULE-LIFECYCLE-001).
// Synchronous direct-drive: applier is invoked inline before onProgress returns
// (no queue/promise/JS-bridge hop). Claim contention (BACK_DOUBLE_CONSUME) is
// reported as { applied: false }, never thrown; out-of-range progress throws
// BACK_BAD_PROGRESS.

function badValue(code, message) {
  const err = new Error(`${code}: ${message}`);
  err.code = code;
  return err;
}

function checkApplier(applier) {
  if (typeof applier !== 'function') {
    throw badValue(
      'BACK_BAD_APPLIER',
      'createBackProgress requires a function applier'
    );
  }
}

function checkProgress(value) {
  if (typeof value !== 'number' || Number.isNaN(value) || value < 0 || value > 1) {
    throw badValue(
      'BACK_BAD_PROGRESS',
      `progress must be a number in [0, 1], got ${String(value)}`
    );
  }
}

function createBackProgress(applier) {
  checkApplier(applier);
  let owner = null;

  function claim(nextOwner) {
    owner = nextOwner;
  }

  function release(prevOwner) {
    if (owner === null) {
      return;
    }
    if (prevOwner !== owner) {
      return;
    }
    owner = null;
  }

  function onProgress(value, claimant) {
    checkProgress(value);
    if (owner !== null && claimant !== owner) {
      return { applied: false };
    }
    applier(value);
    return { applied: true };
  }

  return { onProgress, claim, release };
}

module.exports = { createBackProgress };
