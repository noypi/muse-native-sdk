// Flappy daily-seed loop (T6, AC-SAMPLE-032).
//
// Daily-seed mode derives the seed from the calendar date (YYYYMMDD); the
// best score persists on-device; restart is a synchronous reset (< 1s); the
// share payload is a local share-sheet payload (no upload backend).

const BEST_KEY = 'flappy.best';

let memoryBest = 0;

function persistBest(value: number): void {
  try {
    // Device-persistence hook: localStorage on web, in-memory fallback.
    const storage = (globalThis as { localStorage?: { setItem(k: string, v: string): void } }).localStorage;
    if (storage) storage.setItem(BEST_KEY, String(value));
    else memoryBest = value;
  } catch {
    memoryBest = value;
  }
}

function loadPersistedBest(): number {
  try {
    const storage = (globalThis as { localStorage?: { getItem(k: string): string | null } }).localStorage;
    if (!storage) return memoryBest;
    const raw = storage.getItem(BEST_KEY);
    if (raw == null) return memoryBest;
    const parsed = Number(raw);
    return Number.isInteger(parsed) && parsed >= 0 ? parsed : memoryBest;
  } catch {
    return memoryBest;
  }
}

/** Daily seed from the calendar date: YYYYMMDD (e.g. 2026-01-05 -> 20260105). */
export function dailySeed(date?: Date): number {
  const d = date ?? new Date();
  const year = d.getFullYear();
  const month = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  // YYYYMMDD as a stable zero-padded number.
  return Number(`${year}${month}${day}`);
}

/** Device-persisted best score. */
export function getBest(): number {
  return loadPersistedBest();
}

/** Persist a new best; rejects negative/NaN/non-integer input. */
export function setBest(n: number): void {
  if (!Number.isInteger(n) || Number.isNaN(n) || n < 0) {
    throw new Error(`setBest: expected a non-negative integer, got ${String(n)}`);
  }
  const best = Math.max(loadPersistedBest(), Math.floor(n));
  memoryBest = best;
  persistBest(best);
}

let currentScore = 0;
let currentSeed = 0;

/** Synchronous one-tap restart; completes < 1s (no sleep on the path). */
export function restart(): void {
  currentScore = 0;
  currentSeed = dailySeed();
}

/** Local share-sheet payload — score + seed, never uploaded. */
export function sharePayload(): { score: number; seed: number } {
  return { score: currentScore, seed: currentSeed === 0 ? dailySeed() : currentSeed };
}
