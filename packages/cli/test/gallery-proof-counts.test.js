'use strict';
// T1 — `parseDeviceCounts` per-line pair-parsing regression (BUG-M3-FIDELITY-012).
// Spec refs: AC-M3-020 (FR-M3-009, SPEC-M3-NATIVE), TC-M3-021.
// Interface (plan.md verbatim):
//   `parseDeviceCounts(text: string): { rows: number, pages: number } | null` — pure helper
//   under test (exported from `packages/cli/lib/gallery-proof.js`); throws nothing, returns
//   `null` when no complete per-line pair exists (caller maps `null` to `counts-mismatch`);
//   error shape: assertion failures only.
// Hermetic: node:test + node:assert/strict only. No adb, no device, no network, no config.
// Reference only (not imported for behavior): the `readCounts` double pattern in
// `packages/cli/test/gallery-parity.test.js`.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: `text` is the raw `dumpsys + logcat` buffer; parsing is per-line, so a `/.../`
//   regex without the `s` flag (or an explicit line split) is the observable boundary.
// - A2: non-string/empty input yields `null` (throws nothing) — the caller maps it to
//   `counts-mismatch`, never a pass.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const galleryProof = require('../lib/gallery-proof.js');
const cp = require('node:child_process');

function parseDeviceCounts(text) {
  assert.equal(typeof galleryProof.parseDeviceCounts, 'function',
    'lib/gallery-proof.js must export parseDeviceCounts(text)');
  return galleryProof.parseDeviceCounts(text);
}

describe('T1 — stale load-only line + fresh pair yields the fresh pair (AC-M3-020)', () => {
  it('stale gallery-rows=110 load-only line + fresh rows=121 pages=12 pair yields {rows:121, pages:12}', () => {
    const text = [
      '01-01 00:00:01.000  1234  1234 I GalleryPage: gallery-rows=110',
      '01-01 00:00:02.000  1234  1234 I GalleryPage: gallery-rows=121 gallery-pages=12',
    ].join('\n');
    assert.deepEqual(parseDeviceCounts(text), { rows: 121, pages: 12 });
  });

  it('last complete per-line pair wins when two complete pairs exist', () => {
    const text = [
      'GalleryPage: gallery-rows=110 gallery-pages=11',
      'GalleryPage: gallery-rows=121 gallery-pages=12',
    ].join('\n');
    assert.deepEqual(parseDeviceCounts(text), { rows: 121, pages: 12 });
  });
});

describe('T1 — fail-closed when no complete per-line pair exists (AC-M3-020)', () => {
  it('stale-only buffer (lone gallery-rows lines) yields null, never a spliced pair', () => {
    const text = [
      'GalleryPage: gallery-rows=110',
      'GalleryPage: gallery-rows=110',
      'GalleryPage: gallery-pages=12',
    ].join('\n');
    assert.equal(parseDeviceCounts(text), null);
  });

  it('empty buffer yields null and throws nothing', () => {
    assert.equal(parseDeviceCounts(''), null);
  });

  it('unrelated log text yields null and throws nothing', () => {
    assert.equal(parseDeviceCounts('some unrelated logcat line without markers\nanother line'), null);
  });
});

// T5 — `readDeviceCounts` pre-render-window retry regression (BUG-M3-FIDELITY-012 (b)).
// Spec refs: AC-M3-020 (FR-M3-009, SPEC-M3-NATIVE), TC-M3-021.
// Interface (plan.md verbatim):
//   `readDeviceCounts({ serial?: string, attempts?: number, sleepMs?: number, sleep?: (ms: number) => Promise<void> }): Promise<{ rows: number, pages: number }>` — rejects `device-unreachable` when no device answers, rejects `counts-mismatch` when no complete pair parses after all attempts (never resolves `null` as pass); error shape: `{ code: 'device-unreachable' | 'counts-mismatch', message: string }` via existing `failWith`.
// Hermetic: node:test + node:assert/strict only; adb shot layer stubbed in-process
// via `node:child_process` `execFileSync` (no adb binary, no serial, no device,
// no network). Zero-delay injected `sleep`; attempts/sleepMs injected.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: per-attempt shots are served in order (first logcat shot = pre-render
//   lone-line window, later shots = fresh pair); the retry loop re-reads
//   dumpsys+logcat each attempt (mirrors `defaultReadViewport` bounded-retry shape).
// - A2: `sleep` is awaited between attempts only (not after the final attempt);
//   tests assert at least one delay elapsed on the retry path.
// - A3: dumpsys output is empty in these shots so the logcat leg decides; the
//   dumpsys-first direct scan (attempt 0) is preserved by T6 but not asserted here.

function readDeviceCounts(opts) {
  assert.equal(typeof galleryProof.readDeviceCounts, 'function',
    'lib/gallery-proof.js must export readDeviceCounts(opts) (T6 adds the bounded-retry shape)');
  return galleryProof.readDeviceCounts(opts);
}

// Stub the adb shot layer: dumpsys reads return `dumpsys`, logcat reads serve
// `logcatShots` in order (repeating the last shot); `fail` makes every adb call
// throw so the device-unreachable mapping is exercised. Returns a restore fn.
function stubAdbShots({ dumpsys = '', logcatShots = [''], fail = null } = {}) {
  const orig = cp.execFileSync;
  let logcatCalls = 0;
  cp.execFileSync = (bin, args) => {
    assert.equal(bin, 'adb', 'device reads must go through the adb shot layer');
    if (fail) throw fail;
    const flat = (args || []).join(' ');
    if (flat.includes('dumpsys')) return Buffer.from(dumpsys);
    if (flat.includes('logcat')) {
      const shot = logcatShots[Math.min(logcatCalls, logcatShots.length - 1)];
      logcatCalls += 1;
      return Buffer.from(shot);
    }
    return Buffer.from('');
  };
  return () => { cp.execFileSync = orig; };
}

function zeroDelaySleepRecorder() {
  const calls = [];
  const sleep = (ms) => { calls.push(ms); return Promise.resolve(); };
  return { calls, sleep };
}

async function assertRejectsWithCode(promise, code) {
  await assert.rejects(promise, (err) => {
    assert.equal(err && err.code, code, `expected rejection code ${code}, got ${err && err.code}: ${err && err.message}`);
    assert.match(String((err && err.message) || ''), new RegExp(code),
      'error message must carry the code (failWith shape)');
    return true;
  });
}

describe('T5 — pre-render lone-line first shot retries to the later complete pair (AC-M3-020)', () => {
  it('lone gallery-rows=110 first shot then rows=121 pages=12 pair resolves {rows:121, pages:12}', async () => {
    const restore = stubAdbShots({
      dumpsys: '',
      logcatShots: [
        '01-01 00:00:01.000  1234  1234 I GalleryPage: gallery-rows=110',
        '01-01 00:00:02.000  1234  1234 I GalleryPage: gallery-rows=121 gallery-pages=12',
      ],
    });
    const { calls, sleep } = zeroDelaySleepRecorder();
    try {
      const result = await readDeviceCounts({ attempts: 3, sleepMs: 0, sleep });
      assert.deepEqual(result, { rows: 121, pages: 12 });
      assert.ok(calls.length >= 1, `retry path must delay between attempts (sleep calls: ${calls.length})`);
    } finally {
      restore();
    }
  });

  it('stale lone-line prefix + fresh pair in one shot still yields the pair (never-stale splice guard)', async () => {
    const restore = stubAdbShots({
      dumpsys: '',
      logcatShots: [
        [
          '01-01 00:00:01.000  1234  1234 I GalleryPage: gallery-rows=110',
          '01-01 00:00:02.000  1234  1234 I GalleryPage: gallery-rows=121 gallery-pages=12',
        ].join('\n'),
      ],
    });
    const { sleep } = zeroDelaySleepRecorder();
    try {
      const result = await readDeviceCounts({ attempts: 2, sleepMs: 0, sleep });
      assert.deepEqual(result, { rows: 121, pages: 12 });
    } finally {
      restore();
    }
  });
});

describe('T5 — fail-closed when no complete pair parses after all attempts (AC-M3-020)', () => {
  it('stale lone-lines-only across all attempts rejects counts-mismatch (never null pass)', async () => {
    const restore = stubAdbShots({
      dumpsys: '',
      logcatShots: [
        'GalleryPage: gallery-rows=110',
        'GalleryPage: gallery-rows=110',
        'GalleryPage: gallery-rows=110',
      ],
    });
    const { sleep } = zeroDelaySleepRecorder();
    try {
      await assertRejectsWithCode(
        readDeviceCounts({ attempts: 3, sleepMs: 0, sleep }),
        'counts-mismatch',
      );
    } finally {
      restore();
    }
  });

  it('adb layer throwing with no device rejects device-unreachable', async () => {
    const restore = stubAdbShots({ fail: new Error('spawn adb ENOENT') });
    const { sleep } = zeroDelaySleepRecorder();
    try {
      await assertRejectsWithCode(
        readDeviceCounts({ attempts: 2, sleepMs: 0, sleep }),
        'device-unreachable',
      );
    } finally {
      restore();
    }
  });
});
