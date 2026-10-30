// loop.test.ts — T6 Flappy playable loop (AC-SAMPLE-017; TC-SAMPLE-017 unit half).
//
// Hermetic: node:test + node:assert/strict only. No network, no secrets, no
// application configuration, no live services, no device. The unit under
// test (`stepGame`/`resetGame` from `./loop.ts`) runs for real; the local
// deterministic `box2d-sample` physics it drives is an in-process
// collaborator, not a live dependency. Each test builds its own state via
// `resetGame()`; no shared mutable state, no ordering dependency.
//
// Binds ONLY to the plan interface:
//   `stepGame(state: GameState, input: "tap"|"none", dt: number): GameState`
//   `resetGame(): GameState`
// Never binds to an implementation.
//
// ASSUMPTIONS (the plan pins `status: "game-over"` + `score` only):
//   A1 (state shape): `GameState` exposes numeric `score`, `birdY` (larger =
//       higher), `birdV` (larger = moving up), and `status` in
//       `"ready" | "playing" | "game-over"`. The coder may carry extra fields
//       (world handle, pipes); tests read only these four, plus
//       determinism/purity pass-throughs that need no field knowledge.
//   A2 (lifecycle): first `tap` moves `ready` -> `playing`; `game-over` is
//       absorbing (further steps keep `status`/`score`); floor contact counts
//       as a collision, so an all-`"none"` run always terminates.
//   A3 (scoring reachability): at least one constant-period flap policy
//       passes a pipe within 60 s of simulated time at dt = 1/60.
//   A4 (invalid dt): non-positive `dt` throws instead of producing NaN state.

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';

import { stepGame, resetGame } from './loop.ts';

/**
 * @typedef {{ status: "ready" | "playing" | "game-over", score: number, birdY: number, birdV: number }} GameState
 */

const DT = 1 / 60;

/** @returns {GameState} */
function fresh() {
  return resetGame();
}

/** Advance `n` frames with a fixed input. */
/**
 * @param {GameState} state
 * @param {"tap" | "none"} input
 * @param {number} n
 * @returns {GameState}
 */
function runSteps(state, input, n, dt = DT) {
  let s = state;
  for (let i = 0; i < n; i += 1) {
    s = stepGame(s, input, dt);
  }
  return s;
}

/** Step with no input until game-over (or budget exhausted). */
/**
 * @param {GameState} state
 * @param {number} [budget]
 * @returns {GameState}
 */
function fallUntilOver(state, budget = 60 * 60) {
  let s = state;
  for (let i = 0; i < budget && s.status !== 'game-over'; i += 1) {
    s = stepGame(s, 'none', DT);
  }
  return s;
}

describe('resetGame', () => {
  it('returns an initial state with score 0 and a non-terminal status', () => {
    const s = fresh();
    assert.equal(s.score, 0);
    assert.notEqual(s.status, 'game-over');
  });

  it('restores the initial state with no retained score after a run', () => {
    const before = fresh();
    const over = fallUntilOver(stepGame(fresh(), 'tap', DT));
    assert.equal(over.status, 'game-over');
    const after = resetGame();
    assert.deepEqual(after, before);
    assert.equal(after.score, 0);
  });

  it('completes in under 1s', () => {
    const start = Date.now();
    resetGame();
    assert.ok(Date.now() - start < 1000, 'resetGame must restore initial state in under 1s');
  });
});

describe('stepGame input', () => {
  it('first tap starts the game', () => {
    const next = stepGame(fresh(), 'tap', DT);
    assert.equal(next.status, 'playing');
    assert.equal(next.score, 0);
  });

  it('tap applies an upward impulse compared with no input', () => {
    const playing = stepGame(fresh(), 'tap', DT);
    const tapped = stepGame(playing, 'tap', DT);
    const idle = stepGame(playing, 'none', DT);
    assert.ok(
      tapped.birdV > idle.birdV,
      `tap must raise vertical velocity (tap=${tapped.birdV}, none=${idle.birdV})`,
    );
    assert.ok(
      tapped.birdY >= idle.birdY,
      `tap must not leave the bird lower (tap=${tapped.birdY}, none=${idle.birdY})`,
    );
  });

  it('gravity pulls the bird down when there is no input', () => {
    const playing = stepGame(fresh(), 'tap', DT);
    const one = stepGame(playing, 'none', DT);
    const two = stepGame(one, 'none', DT);
    assert.ok(two.birdV < one.birdV, 'vertical velocity must fall under gravity with no input');
    assert.ok(two.birdY < one.birdY, 'altitude must fall under gravity with no input');
  });
});

describe('stepGame purity and determinism', () => {
  it('does not mutate its input state', () => {
    const s = stepGame(fresh(), 'tap', DT);
    const snapshot = structuredClone(s);
    stepGame(s, 'tap', DT);
    stepGame(s, 'none', DT);
    assert.deepEqual(s, snapshot);
  });

  it('is deterministic for the same input sequence', () => {
    const seq = (/** @type {GameState} */ s) =>
      runSteps(runSteps(s, 'tap', 1), 'none', 30);
    assert.deepEqual(seq(fresh()), seq(fresh()));
  });

  it('rejects a non-positive dt instead of producing NaN state', () => {
    assert.throws(() => stepGame(fresh(), 'none', 0));
    assert.throws(() => stepGame(fresh(), 'tap', -DT));
  });
});

describe('stepGame scoring and game-over', () => {
  it('increments the score when the bird passes a pipe', () => {
    // Deterministic search: constant-period flap policies at dt = 1/60,
    // 60 s of simulated time each. At least one must score (A3).
    let best = 0;
    for (let period = 2; period <= 15; period += 1) {
      let s = stepGame(fresh(), 'tap', DT);
      for (let frame = 0; frame < 60 * 60 && s.status !== 'game-over'; frame += 1) {
        s = stepGame(s, frame % period === 0 ? 'tap' : 'none', DT);
        if (s.score > best) best = s.score;
        if (best >= 1) break;
      }
      if (best >= 1) break;
    }
    assert.ok(best >= 1, 'a flap policy must pass a pipe and increment the score');
  });

  it('yields game-over with a preserved score on collision', () => {
    const over = fallUntilOver(stepGame(fresh(), 'tap', DT));
    assert.equal(over.status, 'game-over');
    assert.ok(Number.isInteger(over.score) && over.score >= 0, 'score must stay a non-negative integer');
  });

  it('keeps game-over absorbing: further steps change neither status nor score', () => {
    const over = fallUntilOver(stepGame(fresh(), 'tap', DT));
    assert.equal(over.status, 'game-over');
    const later = runSteps(over, 'tap', 30);
    assert.equal(later.status, 'game-over');
    assert.equal(later.score, over.score);
  });
});
