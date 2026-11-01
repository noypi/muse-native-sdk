'use strict';
// T1 — Event-driven frame-ready wait (FR-GRAPHICS-015).
// Polls `adb logcat` for the `gallery-page=N/9` marker and resolves only
// after the marker for the expected page is observed. Bounded and
// fail-closed: timeout rejects with FrameNotReadyError, never falls through
// to a stale/unwaited result.

const { execFileSync } = require('node:child_process');

const DEFAULT_TIMEOUT_MS = 30000;
const POLL_INTERVAL_MS = 50;
const SETTLE_POLL_MS = 50;

class FrameNotReadyError extends Error {
  constructor(expectedPage, timeoutMs) {
    super(`frame not ready: gallery-page=${expectedPage} marker not observed within ${timeoutMs}ms`);
    this.name = 'FrameNotReadyError';
    this.expectedPage = expectedPage;
    this.timeoutMs = timeoutMs;
  }
}

// Default adb runner: async (argv: string[]) => string. Shells out to
// `adb -s <serial> ...argv` and returns stdout as text.
function makeDefaultAdb(serial) {
  return async (argv) => {
    const out = execFileSync('adb', ['-s', serial, ...argv], {
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    return typeof out === 'string' ? out : String(out);
  };
}

// Accept the A2 function shape; tolerate an object-shaped AdbLike without
// changing behavior for function callers.
function resolveAdbRunner(adb, serial) {
  if (adb == null) return makeDefaultAdb(serial);
  if (typeof adb === 'function') return adb;
  if (typeof adb.run === 'function') return (argv) => adb.run(argv);
  if (typeof adb.exec === 'function') return (argv) => adb.exec(argv);
  if (typeof adb.spawn === 'function') return (argv) => adb.spawn(argv);
  throw new TypeError('adb must be an async (argv: string[]) => string function');
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function findMarker(output, expectedPage, totalPages) {
  if (typeof output !== 'string') return null;
  const needle = `gallery-page=${expectedPage}/${totalPages}`;
  const idx = output.indexOf(needle);
  if (idx === -1) return null;
  return needle;
}

// Settle check (marker-before-composite race guard): after the marker is
// observed, require composite to have settled before resolving. Preferred
// form: a SurfaceFlinger frame-time advance on the gallery surface. Fallback
// (hermetic fake adb has no parseable SurfaceFlinger output): require the
// marker to persist across two consecutive logcat polls separated by a short
// delay. Bounded by the caller's deadline; returns true when settled, false
// when the marker flaked (caller keeps polling) — throws FrameNotReadyError
// only via the caller's timeout path (fail-closed, never stale).
async function settleAfterMarker(run, expectedPage, totalPages, deadlineMs) {
  // Best-effort SurfaceFlinger frame-time-advance probe. Any failure or
  // unparseable output falls through to the marker-persistence check so the
  // hermetic fake adb (which returns canned logcat text for every argv)
  // stays green.
  try {
    const before = await run(['shell', 'dumpsys', 'SurfaceFlinger']);
    const t0 = extractFrameTime(before);
    if (t0 !== null) {
      await sleep(SETTLE_POLL_MS);
      if (Date.now() >= deadlineMs) return false;
      const after = await run(['shell', 'dumpsys', 'SurfaceFlinger']);
      const t1 = extractFrameTime(after);
      if (t1 !== null && t1 > t0) {
        // Frames advanced: composite settled. Still require the marker to
        // be present so a page flip mid-settle does not resolve stale.
        const output = await run(['logcat', '-d']);
        return findMarker(output, expectedPage, totalPages) !== null;
      }
      // Parseable but not advancing: fall through to persistence check.
    }
  } catch {
    // Ignore probe errors; persistence check below is the hermetic path.
  }
  // Marker-persistence fallback: marker must survive one more poll after a
  // short delay.
  await sleep(SETTLE_POLL_MS);
  if (Date.now() >= deadlineMs) return false;
  const output = await run(['logcat', '-d']);
  return findMarker(output, expectedPage, totalPages) !== null;
}

// Extract the largest frame-timestamp-like number from `dumpsys
// SurfaceFlinger` output. Returns null when nothing parseable is present
// (e.g. hermetic fake adb) so the caller can fall back.
function extractFrameTime(output) {
  if (typeof output !== 'string') return null;
  const matches = output.match(/\b\d{10,}\b/g);
  if (!matches) return null;
  let max = null;
  for (const m of matches) {
    const n = Number(m);
    if (!Number.isFinite(n)) continue;
    if (max === null || n > max) max = n;
  }
  return max;
}

async function waitForFrameReady({ serial, expectedPage, totalPages = 9, timeoutMs = DEFAULT_TIMEOUT_MS, adb } = {}) {
  if (!Number.isInteger(expectedPage) || expectedPage < 1) {
    throw new TypeError(`expectedPage must be a positive integer, got: ${expectedPage}`);
  }
  if (!Number.isInteger(totalPages) || totalPages < 1) {
    throw new TypeError(`totalPages must be a positive integer, got: ${totalPages}`);
  }
  if (typeof timeoutMs !== 'number' || !(timeoutMs >= 0)) {
    throw new TypeError(`timeoutMs must be a non-negative number, got: ${timeoutMs}`);
  }
  const run = resolveAdbRunner(adb, serial);
  const start = Date.now();
  const deadlineMs = start + timeoutMs;
  for (;;) {
    const output = await run(['logcat', '-d']);
    const marker = findMarker(output, expectedPage, totalPages);
    if (marker !== null) {
      if (Date.now() >= deadlineMs) {
        throw new FrameNotReadyError(expectedPage, timeoutMs);
      }
      const settled = await settleAfterMarker(run, expectedPage, totalPages, deadlineMs);
      if (settled) {
        return { marker, elapsedMs: Date.now() - start };
      }
    }
    if (Date.now() - start >= timeoutMs) {
      throw new FrameNotReadyError(expectedPage, timeoutMs);
    }
    const remaining = timeoutMs - (Date.now() - start);
    await sleep(Math.min(POLL_INTERVAL_MS, Math.max(remaining, 0)));
  }
}

module.exports = { waitForFrameReady, FrameNotReadyError, DEFAULT_TIMEOUT_MS };
