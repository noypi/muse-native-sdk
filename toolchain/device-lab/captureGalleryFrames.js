'use strict';
// T2 — Wait-gated gallery frame capture (FR-GRAPHICS-015).
// Per page: broadcast `muse.native.PROOF_ADVANCE_PAGE`, gate on the T1
// `waitForFrameReady` marker observation, then double-capture
// (`screencap` → pull → rm, twice) and return the md5 pair with a stability
// flag (`stable` is true only when md5a === md5b).
// With `useWait=true` the legacy fixed sleeps (`sleep 2` per page, `sleep 4`
// cold-start backstop) are retained as an upper-bound backstop during proof —
// they never gate the capture; the T1 marker observation does. Any
// `FrameNotReadyError` aborts the run fail-closed: it propagates before any
// screencap for that page, so no stale frame is recorded.

const { execFileSync } = require('node:child_process');
const crypto = require('node:crypto');

const { waitForFrameReady } = require('./frameReadyWait.js');

// Legacy fixed-sleep budget, retained as a backstop only (never the gate).
const BACKSTOP_SLEEP_MS = 2000;
const COLD_START_SLEEP_MS = 4000;

// Denominator of the `gallery-page=N/9` marker emitted by the gallery.
const GALLERY_TOTAL_PAGES = 9;

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// Mirror of the T1 adb resolution: the contract passes a plain async
// `(argv) => string` function; tolerate an object-shaped AdbLike and fall
// back to shelling out to `adb -s <serial>` when none is injected.
function resolveAdbRunner(adb, serial) {
  if (adb == null) {
    return async (argv) => {
      const out = execFileSync('adb', ['-s', serial, ...argv], {
        encoding: 'utf8',
        stdio: ['ignore', 'pipe', 'pipe'],
      });
      return typeof out === 'string' ? out : String(out);
    };
  }
  if (typeof adb === 'function') return adb;
  if (typeof adb.run === 'function') return (argv) => adb.run(argv);
  if (typeof adb.exec === 'function') return (argv) => adb.exec(argv);
  if (typeof adb.spawn === 'function') return (argv) => adb.spawn(argv);
  throw new TypeError('adb must be an async (argv: string[]) => string function');
}

function md5Hex(bytes) {
  return crypto.createHash('md5').update(bytes).digest('hex');
}

// Single capture: `screencap` → pull → rm. Returns the md5 of the captured
// bytes. `outDir` names where pulls land on a live device; under the
// hermetic fake adb no real files are written.
async function captureOnce(run, serial, page, outDir, tag) {
  const remotePath = `/sdcard/muse-proof-page-${page}-${tag}.png`;
  const localPath = `${outDir}/page-${page}-${tag}.png`;
  const bytes = await run(['shell', 'screencap', '-p', remotePath]);
  await run(['pull', remotePath, localPath]);
  await run(['shell', 'rm', '-f', remotePath]);
  void serial;
  return md5Hex(typeof bytes === 'string' ? bytes : String(bytes));
}

async function captureGalleryFrames({
  serial,
  pages = GALLERY_TOTAL_PAGES,
  outDir,
  useWait = false,
  adb,
  timeoutMs,
  totalPages = GALLERY_TOTAL_PAGES,
} = {}) {
  if (!Number.isInteger(pages) || pages < 1) {
    throw new TypeError(`pages must be a positive integer, got: ${pages}`);
  }
  if (typeof outDir !== 'string' || outDir.length === 0) {
    throw new TypeError(`outDir must be a non-empty string, got: ${outDir}`);
  }
  const run = resolveAdbRunner(adb, serial);

  if (useWait) {
    // Cold start (legacy proof sequence): restart the gallery, keep the
    // fixed sleep as a backstop, then let the per-page loop below wait on
    // the `gallery-page=1/9` marker via T1.
    await run(['shell', 'am', 'force-stop', 'muse.native']);
    await run(['shell', 'am', 'start', 'muse.native/.GalleryActivity']);
    await sleep(COLD_START_SLEEP_MS);
  }

  const results = [];
  for (let page = 1; page <= pages; page += 1) {
    // Advance the gallery to this page.
    await run([
      'shell',
      'am',
      'broadcast',
      '-a',
      'muse.native.PROOF_ADVANCE_PAGE',
      '--ei',
      'page',
      String(page),
    ]);
    // Event-driven gate (T1): throws FrameNotReadyError on timeout, before
    // any screencap for this page — the run aborts fail-closed.
    await waitForFrameReady({ serial, expectedPage: page, totalPages, timeoutMs, adb: run });
    if (useWait) {
      // Upper-bound backstop only, not the gate.
      await sleep(BACKSTOP_SLEEP_MS);
    }
    // Double-capture md5 pairing (belt-and-braces during transition).
    const md5a = await captureOnce(run, serial, page, outDir, 'a');
    const md5b = await captureOnce(run, serial, page, outDir, 'b');
    results.push({ page, md5a, md5b, stable: md5a === md5b });
  }
  return results;
}

module.exports = { captureGalleryFrames, BACKSTOP_SLEEP_MS, COLD_START_SLEEP_MS };
