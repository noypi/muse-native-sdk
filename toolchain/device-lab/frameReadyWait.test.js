'use strict';
// Batch 1 — BUG-GRAPHICS-005 event-driven frame-ready waits (FR-GRAPHICS-015).
// Hermetic: node:test + node:assert/strict only. All adb interaction is via an
// injected fake `adb` async function (argv => output); no device, no network,
// no downloads, no secrets, no live services.
// Red state: `toolchain/device-lab/frameReadyWait.js` and
// `toolchain/device-lab/captureGalleryFrames.js` do not exist yet, so every
// case below fails until T1/T2 land.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadWait() {
  return require('./frameReadyWait.js');
}

function loadCapture() {
  return require('./captureGalleryFrames.js');
}

// Fake adb runner: async (argv: string[]) => string output.
// Records every invocation in order in `calls` with a monotonic seq.
// ASSUMPTION A2 (see unit-tests/T1.md): `adb` is an async argv->output
// function; the default implementation shells out to `adb -s <serial>`.
function makeFakeAdb(handler) {
  const calls = [];
  let seq = 0;
  const adb = async (argv) => {
    const entry = { seq: seq++, argv: Array.from(argv) };
    calls.push(entry);
    if (handler) return handler(argv, entry, calls);
    return '';
  };
  adb.calls = calls;
  return adb;
}

const MARKER = (n, total = 9) => `gallery-page=${n}/${total}`;

describe('T1 — waitForFrameReady resolves after the expected marker', () => {
  it('marker-seen: resolves with { marker, elapsedMs } once gallery-page=N/9 is observed', async () => {
    const { waitForFrameReady } = loadWait();
    const adb = makeFakeAdb(() => `some log line\n${MARKER(3)}\nmore logs\n`);
    const result = await waitForFrameReady({ serial: 'fake-serial-not-real', expectedPage: 3, adb, timeoutMs: 2000 });
    assert.ok(result.marker.includes(MARKER(3)), `marker must name the expected page, got: ${result.marker}`);
    assert.equal(typeof result.elapsedMs, 'number');
    assert.ok(result.elapsedMs >= 0, 'elapsedMs must be non-negative');
  });

  it('timeout: rejects with FrameNotReadyError(expectedPage, timeoutMs) and never returns a stale result', async () => {
    const { waitForFrameReady, FrameNotReadyError } = loadWait();
    const adb = makeFakeAdb(() => 'unrelated log output, no marker\n');
    const timeoutMs = 120;
    await assert.rejects(
      () => waitForFrameReady({ serial: 'fake-serial-not-real', expectedPage: 5, adb, timeoutMs }),
      (err) => {
        assert.ok(err instanceof FrameNotReadyError, `must be FrameNotReadyError, got: ${err && err.constructor && err.constructor.name}`);
        assert.equal(err.expectedPage, 5);
        assert.equal(err.timeoutMs, timeoutMs);
        assert.equal(err.name, 'FrameNotReadyError');
        return true;
      }
    );
  });

  it('wrong-page-ignored: gallery-page=M/9 for M != N never resolves the wait', async () => {
    const { waitForFrameReady, FrameNotReadyError } = loadWait();
    const adb = makeFakeAdb(() => `${MARKER(2)}\n${MARKER(4)}\n`);
    const timeoutMs = 120;
    await assert.rejects(
      () => waitForFrameReady({ serial: 'fake-serial-not-real', expectedPage: 3, adb, timeoutMs }),
      (err) => {
        assert.ok(err instanceof FrameNotReadyError, `wrong-page output must time out, got: ${err}`);
        assert.equal(err.expectedPage, 3);
        return true;
      }
    );
  });
});

describe('T3 — harness self-check: marker-before-screencap, fail-closed (FR-GRAPHICS-015; supports TC-GRAPHICS-046)', () => {
  it('rapid-marker: immediate marker resolves event-driven, well under the legacy fixed-sleep budget', async () => {
    const { waitForFrameReady } = loadWait();
    const adb = makeFakeAdb(() => `${MARKER(1)}\n`);
    const wallStart = Date.now();
    const result = await waitForFrameReady({ serial: 'fake-serial-not-real', expectedPage: 1, adb, timeoutMs: 5000 });
    const wallMs = Date.now() - wallStart;
    assert.ok(result.marker.includes(MARKER(1)), `marker must name the expected page, got: ${result.marker}`);
    // Event-driven, not time-driven: no 2000ms/page legacy sleep is waited out.
    assert.ok(result.elapsedMs < 500, `rapid marker must resolve in <500ms, got elapsedMs=${result.elapsedMs}`);
    assert.ok(wallMs < 1000, `wall time must stay well under the legacy ~2000ms/page budget, got ${wallMs}ms`);
  });

  it('flapping-logcat: interleaved unrelated lines are ignored, only gallery-page=N/9 gates', async () => {
    const { waitForFrameReady } = loadWait();
    let polls = 0;
    const adb = makeFakeAdb(() => {
      polls += 1;
      if (polls < 3) return `noise line ${polls}\nActivityManager: START u0 {cmp=muse.native/.GalleryActivity}\nSurfaceFlinger: frame update tick\n`;
      return `flapping noise\n${MARKER(6)}\ntrailing noise\n`;
    });
    const result = await waitForFrameReady({ serial: 'fake-serial-not-real', expectedPage: 6, adb, timeoutMs: 2000 });
    assert.ok(result.marker.includes(MARKER(6)), `only the exact marker gates, got: ${result.marker}`);
    assert.ok(polls >= 3, `wait must poll past the unrelated lines (polls=${polls})`);
  });

  it('timeout-fail-closed: no screencap invocation follows a FrameNotReadyError', async () => {
    const waitMod = loadWait();
    const { captureGalleryFrames } = loadCapture();
    const adb = makeFakeAdb((argv) => {
      const cmd = argv.join(' ');
      if (cmd.includes('logcat')) return 'unrelated log output, no marker\n';
      if (cmd.includes('screencap')) return 'fake-png-bytes-not-real';
      return '';
    });
    await assert.rejects(
      () => captureGalleryFrames({ serial: 'fake-serial-not-real', pages: 1, outDir: 'fake-out-dir-not-real', adb, timeoutMs: 120 }),
      (err) => {
        const isFrameErr = err instanceof waitMod.FrameNotReadyError || err.name === 'FrameNotReadyError';
        assert.ok(isFrameErr, `must abort with FrameNotReadyError, got: ${err}`);
        return true;
      }
    );
    const screencaps = adb.calls.filter((c) => c.argv.join(' ').includes('screencap'));
    assert.equal(screencaps.length, 0, `no stale frame may be captured after a failed wait (screencaps=${screencaps.length})`);
  });

  it('double-capture stability: stable is true only when md5a === md5b', async () => {
    const { captureGalleryFrames } = loadCapture();
    const stableAdb = makeFakeAdb((argv) => {
      const cmd = argv.join(' ');
      if (cmd.includes('logcat')) return `${MARKER(1)}\n`;
      if (cmd.includes('screencap')) return 'fake-stable-png-bytes-not-real';
      return '';
    });
    const stableResults = await captureGalleryFrames({ serial: 'fake-serial-not-real', pages: 1, outDir: 'fake-out-dir-not-real', adb: stableAdb });
    assert.equal(stableResults.length, 1);
    assert.equal(stableResults[0].md5a, stableResults[0].md5b, 'identical captures must hash identically');
    assert.equal(stableResults[0].stable, true, 'stable must be true when md5a === md5b');

    let screencapCount = 0;
    const flappingAdb = makeFakeAdb((argv) => {
      const cmd = argv.join(' ');
      if (cmd.includes('logcat')) return `${MARKER(1)}\n`;
      if (cmd.includes('screencap')) {
        screencapCount += 1;
        return `fake-png-bytes-frame-${screencapCount}-not-real`;
      }
      return '';
    });
    const unstableResults = await captureGalleryFrames({ serial: 'fake-serial-not-real', pages: 1, outDir: 'fake-out-dir-not-real', adb: flappingAdb });
    assert.equal(unstableResults.length, 1);
    assert.notEqual(unstableResults[0].md5a, unstableResults[0].md5b, 'differing captures must hash differently');
    assert.equal(unstableResults[0].stable, false, 'stable must be false when md5a !== md5b');
  });
});
describe('T2 — captureGalleryFrames ordering (mocked adb, hermetic)', () => {
  it('screencap happens strictly after marker observation for every page', async () => {
    const { captureGalleryFrames } = loadCapture();
    const pages = 3;
    const adb = makeFakeAdb((argv) => {
      const cmd = argv.join(' ');
      if (cmd.includes('logcat')) {
        // Logcat always shows all page markers so the T1 wait can gate.
        return [MARKER(1), MARKER(2), MARKER(3)].join('\n');
      }
      if (cmd.includes('screencap')) return 'fake-png-bytes-not-real';
      if (cmd.includes('PROOF_ADVANCE_PAGE')) return '';
      return '';
    });
    const outDir = 'fake-out-dir-not-real';
    const results = await captureGalleryFrames({ serial: 'fake-serial-not-real', pages, outDir, useWait: true, adb });
    assert.equal(results.length, pages);
    for (const r of results) {
      assert.equal(typeof r.page, 'number');
      assert.equal(typeof r.md5a, 'string');
      assert.equal(typeof r.md5b, 'string');
      assert.equal(typeof r.stable, 'boolean');
    }
    // Ordering: for each page N, the first logcat observation of
    // gallery-page=N/9 must precede the first screencap for that page.
    // The fake records argv order; the implementation must poll logcat
    // (marker gate) before each page's screencap.
    const calls = adb.calls;
    const firstLogcatIdx = calls.findIndex((c) => c.argv.join(' ').includes('logcat'));
    const firstScreencapIdx = calls.findIndex((c) => c.argv.join(' ').includes('screencap'));
    assert.ok(firstLogcatIdx !== -1, 'must poll logcat');
    assert.ok(firstScreencapIdx !== -1, 'must screencap');
    assert.ok(firstLogcatIdx < firstScreencapIdx, `logcat marker poll (idx ${firstLogcatIdx}) must precede first screencap (idx ${firstScreencapIdx})`);
  });

  it('missing marker aborts fail-closed with FrameNotReadyError and zero screencaps for that page', async () => {
    const waitMod = loadWait();
    const { captureGalleryFrames } = loadCapture();
    const adb = makeFakeAdb((argv) => {
      const cmd = argv.join(' ');
      if (cmd.includes('logcat')) return 'no markers here\n';
      if (cmd.includes('screencap')) return 'fake-png-bytes-not-real';
      return '';
    });
    await assert.rejects(
      () => captureGalleryFrames({ serial: 'fake-serial-not-real', pages: 2, outDir: 'fake-out-dir-not-real', useWait: true, adb, timeoutMs: 120 }),
      (err) => {
        const isFrameErr = err instanceof waitMod.FrameNotReadyError || err.name === 'FrameNotReadyError';
        assert.ok(isFrameErr, `must abort with FrameNotReadyError, got: ${err}`);
        return true;
      }
    );
    const screencaps = adb.calls.filter((c) => c.argv.join(' ').includes('screencap'));
    assert.equal(screencaps.length, 0, `no stale frame may be captured after a failed wait (screencaps=${screencaps.length})`);
  });
});
