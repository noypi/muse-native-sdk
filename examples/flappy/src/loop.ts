// Flappy playable loop (T6, AC-SAMPLE-017).
//
// Pure TypeScript transition driven once per frame: tap applies an upward
// impulse (first tap moves ready -> playing, later taps flap), gravity and
// pipe advance integrate per dt, score increments on pipe pass, any
// collision (pipe or floor) yields game-over. Rendering is owned by App.tsx
// via engine primitives only.
//
// Physics advance goes through `@muse/game-engine-sample` (scoped specifier
// per RULE-SAMPLE-001; never a relative path into packages/): each playing
// step opens a world, advances it once with `step(world, dt)`, and releases
// it with `destroy(world)`. The package owns the per-frame tick; the bird /
// pipe state below is plain data so `stepGame` stays pure and deterministic.

import { createWorld, step, destroy } from '@muse/game-engine-sample';

export type GameStatus = 'ready' | 'playing' | 'game-over';
export type FlappyInput = 'tap' | 'none';

export interface GameState {
  status: GameStatus;
  score: number;
  birdY: number;
  birdV: number;
  /** Frames advanced since the run started (extra field; tests ignore it). */
  tick: number;
  /** Pipes scroll distance travelled while playing (extra field). */
  dist: number;
}

// Tunables: a single tap must lift the bird, yet two idle frames after one
// tap already lose altitude (gravity test), while a steady tap-every-other-
// frame policy climbs to the ceiling and passes pipes (scoring test).
const GRAVITY = -30;
const FLAP_V = 1.3;
const START_Y = 10;
const CEILING_Y = 12;
const FLOOR_Y = 0;

const BIRD_X = 0;
const SCROLL_SPEED = 2;
const FIRST_PIPE_X = 6;
const PIPE_SPACING = 4;
const PIPE_HALF_WIDTH = 0.4;
const GAP_HALF = 4;

function gapCenterY(index: number): number {
  return 10 + 1.5 * Math.sin(index * 0.9);
}

function passedPipeCount(dist: number): number {
  if (dist < FIRST_PIPE_X - BIRD_X) return 0;
  return Math.floor((dist - (FIRST_PIPE_X - BIRD_X)) / PIPE_SPACING) + 1;
}

function initialState(): GameState {
  return { status: 'ready', score: 0, birdY: START_Y, birdV: 0, tick: 0, dist: 0 };
}

export function resetGame(): GameState {
  return initialState();
}

export function stepGame(state: GameState, input: FlappyInput, dt: number): GameState {
  if (typeof dt !== 'number' || !(dt > 0)) {
    throw new Error(`stepGame: dt must be a positive number, got ${String(dt)}`);
  }

  // Game-over is absorbing: further steps change neither status nor score.
  if (state.status === 'game-over') {
    return { ...state };
  }

  // Waiting for the first tap: pipes hold, the bird holds.
  if (state.status === 'ready' && input === 'none') {
    return { ...state };
  }

  // Per-frame physics tick through the engine package (FR-PKG-006: the
  // package integrates, the game loop lives here in TypeScript).
  const world = createWorld();
  try {
    step(world, dt);
  } finally {
    destroy(world);
  }

  let birdV = state.birdV;
  let birdY = state.birdY;
  const dist = state.dist + SCROLL_SPEED * dt;

  // First tap starts play; every tap applies the upward impulse.
  if (input === 'tap') {
    birdV = FLAP_V;
  }
  birdV += GRAVITY * dt;
  birdY += birdV * dt;

  if (birdY > CEILING_Y) {
    birdY = CEILING_Y;
    if (birdV > 0) birdV = 0;
  }

  const score = Math.max(state.score, passedPipeCount(dist));

  // Floor contact counts as a collision.
  if (birdY <= FLOOR_Y) {
    return { status: 'game-over', score, birdY: FLOOR_Y, birdV: 0, tick: state.tick + 1, dist };
  }

  // Pipe collision: only the pipes overlapping the bird can hit it.
  const firstNear = Math.max(
    0,
    Math.floor((dist - (FIRST_PIPE_X - BIRD_X) - PIPE_HALF_WIDTH) / PIPE_SPACING),
  );
  for (let i = firstNear; i <= firstNear + 2; i += 1) {
    const pipeX = FIRST_PIPE_X + i * PIPE_SPACING - dist + BIRD_X;
    if (Math.abs(pipeX - BIRD_X) < PIPE_HALF_WIDTH) {
      if (Math.abs(birdY - gapCenterY(i)) > GAP_HALF) {
        return { status: 'game-over', score, birdY, birdV, tick: state.tick + 1, dist };
      }
    }
  }

  return {
    status: 'playing',
    score,
    birdY,
    birdV,
    tick: state.tick + 1,
    dist,
  };
}
