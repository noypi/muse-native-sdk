'use strict';
// T1 — Event-driven frame-ready wait (FR-GRAPHICS-015).
// Polls `adb logcat` for the `gallery-page=N/9` marker and resolves only
// after the marker for the expected page is observed. Bounded and
// fail-closed: timeout rejects with FrameNotReadyError, never falls through
// to a stale/unwaited result.

const { execFileSync } = require('node:child_process');

const DEFAULT_TIMEOUT_MS = 30000;
const POLL_INTERVAL_MS = 50;

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
  for (;;) {
    const output = await run(['logcat', '-d']);
    const marker = findMarker(output, expectedPage, totalPages);
    if (marker !== null) {
      return { marker, elapsedMs: Date.now() - start };
    }
    if (Date.now() - start >= timeoutMs) {
      throw new FrameNotReadyError(expectedPage, timeoutMs);
    }
    const remaining = timeoutMs - (Date.now() - start);
    await sleep(Math.min(POLL_INTERVAL_MS, Math.max(remaining, 0)));
  }
}

module.exports = { waitForFrameReady, FrameNotReadyError, DEFAULT_TIMEOUT_MS };
