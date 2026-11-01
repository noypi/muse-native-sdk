'use strict';
// T6 — `muse-native gallery-tap --tap-page [--assert-zero-jni-faults] [--device <serial>]`
// tap-page gate (TC-RUNTIME-036/037, AC-RUNTIME-036/037).
// Spec refs: SPEC-ENGINE-RUNTIME v1.19, FR-RUNTIME-012, AC-RUNTIME-036, AC-RUNTIME-037.
// Plan Interface line (verbatim, see unit-tests/T6.md):
//   `muse-native gallery-tap --tap-page [--assert-zero-jni-faults] [--device <serial>]`
//   ` -> exit 0 (tap paged: page index advanced, new page rendered with no blank`
//   ` viewport; with --assert-zero-jni-faults additionally zero UnsatisfiedLinkError`
//   ` and zero SIGSEGV in logcat) | exit 1 {code, detail} fail-closed | exit 2 usage.`
// Hermetic: node:test + node:assert/strict + stdlib only. Spawns `node
// bin/muse-native.js` for the CLI exit contract. The tap/logcat/screencap
// device lane is stubbed via an injectable `deps` override (ASSUMPTION A1) —
// hermetic cases never touch adb, a device, the network, secrets, live
// services, or the filesystem: page state, viewport verdicts, and logcat
// text are supplied via `deps.driveSequence` / `deps.readLogcat` /
// `deps.readScreencap`, never read from disk or a device.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: `lib/gallery-tap.js` exposes `parseTapPageArgs(argv)` and
//   `runTapPage({ device, assertZeroJniFaults, deps, emit })` beside the
//   existing button lane (`parseGalleryTapArgs` / `runGalleryTap`), mirroring
//   the T3 `parseSurfaceContractArgs` / `runSurfaceContract` injectable-runner
//   pattern. The plan Interface line names only the CLI shape; the override
//   is a test-only seam, reconcile names (not verdicts) with the coder.
// - A2: unknown flags and missing `--device` values exit 2 with usage naming
//   the flag (gallery-proof/gallery-present/gallery-surface-contract exit-code
//   convention 0/1/2).
// - A3: the failure value is `{ code: string(non-empty), detail: string(non-empty) }`
//   and the CLI prints `TAP-PAGE-FAIL: <code>: <detail>` on stderr.
// - A4: blank-viewport verdict comes from `deps.readScreencap` returning
//   `{ blank: boolean }` (the most defensible hermetic stand-in for the
//   screenshot pixel-variance check); the coder owns the real screencap
//   plumbing, reconcile the shape, not the verdict.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

let galleryTap = null;
try {
  // eslint-disable-next-line global-require
  galleryTap = require('../lib/gallery-tap.js');
} catch {
  galleryTap = null;
}

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function okTap() {
  return {
    pageBefore: 2,
    pageAfter: 3,
    blank: false,
    logcat: 'I/MuseHost: tap dispatched page=3\nI/MuseHost: frame presented\n',
  };
}

function okDeps(overrides = {}) {
  const tap = { ...okTap(), ...overrides };
  return {
    driveSequence: async () => ({ pageBefore: tap.pageBefore, pageAfter: tap.pageAfter }),
    readLogcat: async () => tap.logcat,
    readScreencap: async () => ({ blank: tap.blank }),
  };
}

describe('T6 — gallery-tap tap-page module surface (FR-RUNTIME-012)', () => {
  it('packages/cli/lib/gallery-tap.js exists and loads', () => {
    assert.ok(galleryTap !== null, 'lib/gallery-tap.js must exist');
  });

  it('module exports parseTapPageArgs', () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    assert.equal(typeof galleryTap.parseTapPageArgs, 'function',
      'must export parseTapPageArgs(argv)');
  });

  it('module exports runTapPage', () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    assert.equal(typeof galleryTap.runTapPage, 'function',
      'must export runTapPage(opts)');
  });
});

describe('T6 — parseTapPageArgs hermetic (no device)', () => {
  it('parses gallery-tap --tap-page with no device', () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    assert.deepEqual(
      galleryTap.parseTapPageArgs(['gallery-tap', '--tap-page']),
      { tapPage: true, assertZeroJniFaults: false, device: null },
    );
  });

  it('parses --tap-page --assert-zero-jni-faults', () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    assert.deepEqual(
      galleryTap.parseTapPageArgs(['gallery-tap', '--tap-page', '--assert-zero-jni-faults']),
      { tapPage: true, assertZeroJniFaults: true, device: null },
    );
  });

  it('parses --device <serial>', () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    assert.deepEqual(
      galleryTap.parseTapPageArgs(['gallery-tap', '--tap-page', '--device', 'emulator-5554']),
      { tapPage: true, assertZeroJniFaults: false, device: 'emulator-5554' },
    );
  });

  it('parses without the leading gallery-tap token', () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    assert.deepEqual(
      galleryTap.parseTapPageArgs(['--tap-page']),
      { tapPage: true, assertZeroJniFaults: false, device: null },
    );
  });

  it('rejects a missing --tap-page flag (usage failure, never silent)', () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    assert.throws(
      () => galleryTap.parseTapPageArgs(['gallery-tap']),
      /tap-page|Usage/i,
    );
  });

  it('rejects unknown flags (exit-2 usage shape)', () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    assert.throws(
      () => galleryTap.parseTapPageArgs(['gallery-tap', '--tap-page', '--bogus-flag']),
      /bogus-flag|unknown/i,
    );
  });

  it('rejects a missing --device value (usage failure, never silent)', () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    assert.throws(
      () => galleryTap.parseTapPageArgs(['gallery-tap', '--tap-page', '--device']),
      /device|value|Usage/i,
    );
  });
});

describe('T6 — runTapPage hermetic, device/logcat/screencap stubbed (no device)', () => {
  it('tap pages: page index advances and the new page renders with no blank viewport', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    const lines = [];
    const result = await galleryTap.runTapPage({
      device: 'stubbed-no-device',
      assertZeroJniFaults: false,
      deps: okDeps(),
      emit: (line) => lines.push(String(line)),
    });
    assert.equal(result.ok, true);
    assert.ok(result.pageAfter > result.pageBefore,
      `page index must advance, got before=${result.pageBefore} after=${result.pageAfter}`);
    assert.equal(result.blank, false, 'new page must render with no blank viewport');
    assert.ok(lines.some((l) => /page|tap/i.test(l) && /PASS/i.test(l)),
      `must emit a tap-page PASS line, got: ${lines.join('; ').slice(0, 400)}`);
  });

  it('non-advancing page fails closed with a named code (never exit 0)', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    await assert.rejects(
      galleryTap.runTapPage({
        device: 'stubbed-no-device',
        assertZeroJniFaults: false,
        deps: okDeps({ pageBefore: 2, pageAfter: 2 }),
      }),
      (err) => {
        assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry a named code');
        assert.ok(err && typeof err.detail === 'string' && err.detail.length > 0, 'failure must carry a detail');
        assert.ok(/page|advanc/i.test(`${err.code} ${err.detail}`), 'failure must name the non-advancing page');
        return true;
      },
    );
  });

  it('blank viewport on the new page fails closed with a named code', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    await assert.rejects(
      galleryTap.runTapPage({
        device: 'stubbed-no-device',
        assertZeroJniFaults: false,
        deps: okDeps({ blank: true }),
      }),
      (err) => {
        assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry a named code');
        assert.ok(err && typeof err.detail === 'string' && err.detail.length > 0, 'failure must carry a detail');
        assert.ok(/blank|viewport/i.test(`${err.code} ${err.detail}`), 'failure must name the blank viewport');
        return true;
      },
    );
  });

  it('unreachable device fails closed with a named code', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    await assert.rejects(
      galleryTap.runTapPage({
        device: 'stubbed-no-device',
        assertZeroJniFaults: false,
        deps: {
          driveSequence: async () => {
            const err = new Error("device unreachable via adb against serial 'stubbed-no-device'");
            err.code = 'DEVICE-UNREACHABLE';
            throw err;
          },
          readLogcat: async () => '',
          readScreencap: async () => ({ blank: false }),
        },
      }),
      (err) => {
        assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry a named code');
        assert.ok(err && typeof err.detail === 'string' && err.detail.length > 0, 'failure must carry a detail');
        assert.ok(/device|unreachable|adb/i.test(`${err.code} ${err.detail}`), 'failure must name the device fault');
        return true;
      },
    );
  });

  it('--assert-zero-jni-faults passes on clean logcat', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    const result = await galleryTap.runTapPage({
      device: 'stubbed-no-device',
      assertZeroJniFaults: true,
      deps: okDeps(),
    });
    assert.equal(result.ok, true);
  });

  it('--assert-zero-jni-faults fails closed on UnsatisfiedLinkError in logcat', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    await assert.rejects(
      galleryTap.runTapPage({
        device: 'stubbed-no-device',
        assertZeroJniFaults: true,
        deps: okDeps({ logcat: 'I/MuseHost: tap dispatched\nE/MuseHost: java.lang.UnsatisfiedLinkError: nativeTap\n' }),
      }),
      (err) => {
        assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry a named code');
        assert.ok(err && typeof err.detail === 'string' && err.detail.length > 0, 'failure must carry a detail');
        assert.ok(/unsatisfiedlinkerror|jni/i.test(`${err.code} ${err.detail}`), 'failure must name the JNI fault');
        return true;
      },
    );
  });

  it('--assert-zero-jni-faults fails closed on SIGSEGV in logcat', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    await assert.rejects(
      galleryTap.runTapPage({
        device: 'stubbed-no-device',
        assertZeroJniFaults: true,
        deps: okDeps({ logcat: 'I/MuseHost: tap dispatched\nF/DEBUG: signal 11 (SIGSEGV) fault addr 0x0\n' }),
      }),
      (err) => {
        assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry a named code');
        assert.ok(err && typeof err.detail === 'string' && err.detail.length > 0, 'failure must carry a detail');
        assert.ok(/sigsegv|signal 11/i.test(`${err.code} ${err.detail}`), 'failure must name the SIGSEGV fault');
        return true;
      },
    );
  });

  it('fault-bearing logcat passes without --assert-zero-jni-faults (scan is opt-in)', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    const result = await galleryTap.runTapPage({
      device: 'stubbed-no-device',
      assertZeroJniFaults: false,
      deps: okDeps({ logcat: 'I/MuseHost: tap dispatched\nE/MuseHost: java.lang.UnsatisfiedLinkError: nativeTap\n' }),
    });
    assert.equal(result.ok, true, 'without the flag the JNI scan must not gate the tap-page verdict');
  });
});

describe('T6 — gallery-tap CLI exit contract (TC-RUNTIME-036/037 Check)', () => {
  it('gallery-tap --tap-page --help lists --tap-page', () => {
    const res = runCli(['gallery-tap', '--tap-page', '--help']);
    assert.equal(res.status, 0, `help must exit 0, got ${res.status}`);
    assert.ok(/tap-page/.test(`${res.stdout}\n${res.stderr}`),
      'help text must list the --tap-page flag');
  });

  it('gallery-tap --tap-page --help lists --assert-zero-jni-faults', () => {
    const res = runCli(['gallery-tap', '--tap-page', '--help']);
    assert.equal(res.status, 0, `help must exit 0, got ${res.status}`);
    assert.ok(/assert-zero-jni-faults/.test(`${res.stdout}\n${res.stderr}`),
      'help text must list the --assert-zero-jni-faults flag');
  });

  it('unknown flags exit 2 with usage naming the flag', () => {
    const res = runCli(['gallery-tap', '--tap-page', '--bogus-flag']);
    assert.equal(res.status, 2, `unknown flags must exit 2, got ${res.status}`);
    assert.ok(/bogus-flag|Usage/i.test(`${res.stdout}\n${res.stderr}`),
      'stderr must name the unknown flag with usage');
  });

  it('missing --device value exits 2 with usage (never a silent pass)', () => {
    const res = runCli(['gallery-tap', '--tap-page', '--device']);
    assert.equal(res.status, 2, `missing value must exit 2, got ${res.status}`);
    assert.ok(/Usage|device/i.test(`${res.stdout}\n${res.stderr}`),
      'missing-value failure must print usage');
  });
});
