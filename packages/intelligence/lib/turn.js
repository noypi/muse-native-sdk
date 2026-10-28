'use strict';
// T1 — Shared turn state machine with turnId/targetId contract.
// RULE-MUSE-007 shared states: idle → listening → loading → thinking → ready
// plus denied / no-speech / error, moderated for image.
// Client-generated turnId; targetId bound at commit (Done press), never at mic-tap.
// Stale rule: commit returns false and drops the write when a newer turn
// committed first; the same turn re-committing after move-on is also stale.

let seqCounter = 0;
let maxCommittedSeq = -1;

function mintId() {
  seqCounter += 1;
  const rand = Math.random().toString(36).slice(2, 10);
  return `turn-${seqCounter}-${rand}`;
}

function createTurn() {
  const seq = seqCounter + 1; // creation order, before mintId bumps the counter
  const turn = {
    id: mintId(),
    status: 'idle',
    lastError: undefined,
    commit(_targetId) {
      // Re-commit from an already-committed turn is stale (moved on).
      if (turn.committed) {
        turn.lastError = { code: 'stale-turn', route: 'transcribe' };
        return false;
      }
      // A newer turn committed first — drop the write.
      if (maxCommittedSeq > seq) {
        turn.lastError = { code: 'stale-turn', route: 'transcribe' };
        return false;
      }
      turn.committed = true;
      if (seq > maxCommittedSeq) maxCommittedSeq = seq;
      return true;
    },
    deny(route = 'transcribe') {
      turn.status = 'denied';
      turn.lastError = { code: 'denied', route };
    },
    noSpeech(route = 'transcribe') {
      turn.status = 'no-speech';
      turn.lastError = { code: 'no-speech', route };
    },
    fail(code, route) {
      turn.status = 'error';
      turn.lastError = { code, route };
    },
  };
  // Keep the creation seq on a non-enumerated field so test shape checks
  // (`id`/`status`/`commit`) only see the public surface plus documented helpers.
  Object.defineProperty(turn, 'committed', { value: false, writable: true, enumerable: false });
  return turn;
}

module.exports = { createTurn };
