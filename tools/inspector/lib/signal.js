'use strict';
// Signal inspector view (FR-DX-003 / AC-DX-011 / TC-DX-011).
// Read-only adapter over `packages/core/lib/state.js` holder records
// (`getRefRecord` subs sets); history is a caller-supplied append-only
// log (STATE-DEVTOOLS feed shape), inspector stores nothing mutable.
// Unknown seq rejects with a structured NFR-DX-002 diagnostic.

function badSeqError(detail) {
  const err = new Error(`unknown signal seq in inspector view (DX-003 / AC-DX-011): ${detail}`);
  err.code = 'E_INSPECTOR_BAD_SEQ';
  err.cause = `unknown signal seq in inspector view (DX-003 / AC-DX-011): ${detail}`;
  err.action = 'Step to a seq present in the caller-supplied signal history (DX-003).';
  return err;
}

function copyHistory(history) {
  return history.map((entry) => ({ seq: entry.seq, value: entry.value }));
}

function readSubs(handle) {
  try {
    const stateLib = require('../../../packages/core/lib/state.js');
    if (!stateLib || typeof stateLib.getRefRecord !== 'function') return null;
    return stateLib.getRefRecord(handle);
  } catch (e) {
    void e;
    return null;
  }
}

function describeSignal(handle, opts) {
  const raw = opts && opts.history !== undefined ? opts.history : [];
  const history = Array.isArray(raw) ? raw : [];
  const snapshot = copyHistory(history);

  let lastInvalidation = null;
  if (snapshot.length > 0) {
    let atSeq = snapshot[0].seq;
    for (let i = 1; i < snapshot.length; i += 1) {
      if (snapshot[i].seq > atSeq) atSeq = snapshot[i].seq;
    }
    lastInvalidation = { handle, atSeq };
  }

  const edges = [];
  const record = readSubs(handle);
  if (record && record.subs && typeof record.subs.forEach === 'function') {
    record.subs.forEach((sub) => {
      edges.push({ from: sub, to: handle });
    });
  }

  return { edges, lastInvalidation, history: snapshot };
}

function stepSignal(history, seq) {
  if (!Array.isArray(history)) {
    throw badSeqError('history must be an array');
  }
  for (let i = 0; i < history.length; i += 1) {
    const entry = history[i];
    if (entry && entry.seq === seq) {
      return { seq: entry.seq, value: entry.value };
    }
  }
  throw badSeqError(String(seq));
}

module.exports = { describeSignal, stepSignal };
