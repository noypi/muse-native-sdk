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

  // Security-requested T6 addendum pins (CLEARED WITH NOTES, review round 4:
  // no fix cycle — these document CURRENT accepted behavior, not a change).
  it('env-device note (accepted risk): parseTapPageArgs does NOT consume ANDROID_SERIAL — explicit --device still required', () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    const saved = process.env.ANDROID_SERIAL;
    process.env.ANDROID_SERIAL = 'env-serial-should-be-ignored-by-gate';
    try {
      // The in-tree gate returns device:null regardless of env; the env
      // fallback (if any) lives outside the gate. Serial reaches only
      // injected deps + stderr text, never the gate itself.
      assert.deepEqual(
        galleryTap.parseTapPageArgs(['gallery-tap', '--tap-page']),
        { tapPage: true, assertZeroJniFaults: false, device: null },
      );
      // Missing --device value still usage-rejects even when env is set.
      assert.throws(
        () => galleryTap.parseTapPageArgs(['gallery-tap', '--tap-page', '--device']),
        /device|value|Usage/i,
      );
    } finally {
      if (saved === undefined) delete process.env.ANDROID_SERIAL;
      else process.env.ANDROID_SERIAL = saved;
    }
  });

  it('env-device note (accepted risk): CLI missing --device value exits 2 regardless of ANDROID_SERIAL', () => {
    const res = runCli(['gallery-tap', '--tap-page', '--device'], {
      env: { ...process.env, ANDROID_SERIAL: 'env-serial-should-not-satisfy-gate' },
    });
    assert.equal(res.status, 2, `missing value must exit 2 even with ANDROID_SERIAL set, got ${res.status}`);
    assert.ok(/Usage|device/i.test(`${res.stdout}\n${res.stderr}`),
      'missing-value failure must print usage even with ANDROID_SERIAL set');
  });

  // Security-requested T6 addendum pin (CLEARED WITH NOTES, review round 4:
  // accepted risk, do NOT change production to fail-closed).
  it('inconclusive-shape note (accepted risk): non-string logcat skips the JNI-fault leg to PASS', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    // Current default: `typeof logcat === 'string'` guard silently skips the
    // scan for non-string provider output. Pinned as accepted risk per
    // review round 4 — do NOT change production to fail-closed.
    const result = await galleryTap.runTapPage({
      device: 'stubbed-no-device',
      assertZeroJniFaults: true,
      deps: {
        driveSequence: async () => ({ pageBefore: 2, pageAfter: 3 }),
        readLogcat: async () => 42,
        readScreencap: async () => ({ blank: false }),
      },
    });
    assert.equal(result.ok, true);
  });

  // Hardening re-check pin A1 (round-5 `## Security Tests Requested`, row 2):
  // call-time `{device}` overrides the bound serial. Hermetic: injected
  // `deps` observe the received `{device}` — no adb, no device, no network.
  // Pins CURRENT accepted behavior; MUST stay green (no production change).
  it('call-time device parity: runTapPage forwards call-time {device} to injected deps (overrides bound serial)', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    const seen = {};
    const capture = (name) => async (callArgs) => {
      seen[name] = callArgs && callArgs.device;
      if (name === 'drive') return { pageBefore: -1, pageAfter: 3 };
      if (name === 'screencap') return { blank: false };
      return 'I/MuseHost: tap dispatched page=3\nI/MuseHost: frame presented\n';
    };
    const result = await galleryTap.runTapPage({
      device: 'call-time-serial-override',
      assertZeroJniFaults: true,
      deps: {
        driveSequence: capture('drive'),
        readLogcat: capture('logcat'),
        readScreencap: capture('screencap'),
      },
      emit: () => {},
    });
    assert.equal(result.ok, true);
    assert.equal(seen.drive, 'call-time-serial-override',
      `driveSequence must receive the call-time device, got ${String(seen.drive)}`);
    assert.equal(seen.screencap, 'call-time-serial-override',
      `readScreencap must receive the call-time device, got ${String(seen.screencap)}`);
    assert.equal(seen.logcat, 'call-time-serial-override',
      `readLogcat must receive the call-time device, got ${String(seen.logcat)}`);
  });

  // Security-requested T6 addendum pin (CLEARED WITH NOTES, review round 4:
  // accepted risk, do NOT change production to fail-closed).
  it('inconclusive-shape note (accepted risk): unknown-shape screencap defaults that leg to PASS', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    // Current default: `isBlankViewport` returns false for unrecognized
    // shapes (null/undefined/{}, ...). Pinned as accepted risk per review
    // round 4 — do NOT change production to fail-closed.
    for (const shot of [{}, null, undefined]) {
      const result = await galleryTap.runTapPage({
        device: 'stubbed-no-device',
        assertZeroJniFaults: false,
        deps: {
          driveSequence: async () => ({ pageBefore: 2, pageAfter: 3 }),
          readLogcat: async () => '',
          readScreencap: async () => shot,
        },
      });
      assert.equal(result.ok, true, `unknown-shape screencap must currently PASS, got shot=${String(shot)}`);
    }
  });
});

describe('T6 — hardening re-check pin A2 (accepted risk, test-device-only cosmetic)', () => {
  // Hardening re-check pin A2 (round-5 `## Security Tests Requested`, row 1
  // LOW: `uimode night yes` → `no` has no `finally` restore).
  // ACCEPTED RISK — do NOT change production to restore the prior value:
  // the surface-contract drive unconditionally flips night-mode to `yes`
  // mid-run and flips it back to `no` at the end, so a device that entered
  // in night-`yes` leaves in night-`no`. Test-device-only cosmetic (no code
  // execution / privilege / data consequence); the security-relevant theme
  // assertions ride the NATIVE_SET_THEME broadcasts, not the uimode lines.
  // Static source-order pin (hermetic, no adb): the drive issues
  // `uimode night yes` before `uimode night no`, with no pre-run value
  // capture/restore. MUST stay green under current code.
  it('uimode restore (accepted risk, test-device-only cosmetic): drive ends with night-mode no, not the prior value', () => {
    const fs = require('node:fs');
    const src = fs.readFileSync(
      path.join(CLI_DIR, 'lib', 'gallery-surface-contract.js'), 'utf8');
    const yesIdx = src.indexOf("'uimode', 'night', 'yes'");
    const altYesIdx = src.indexOf('"uimode", "night", "yes"');
    const noIdx = src.indexOf("'uimode', 'night', 'no'");
    const altNoIdx = src.indexOf('"uimode", "night", "no"');
    const firstYes = [yesIdx, altYesIdx].filter((i) => i >= 0).sort((a, b) => a - b)[0];
    const firstNo = [noIdx, altNoIdx].filter((i) => i >= 0).sort((a, b) => a - b)[0];
    assert.ok(firstYes !== undefined && firstYes >= 0, 'drive must toggle uimode night yes');
    assert.ok(firstNo !== undefined && firstNo >= 0, 'drive must restore uimode night no');
    assert.ok(firstYes < firstNo,
      'drive must end with night-mode no (restores the toggled value, not the prior value)');
    // No prior-value restore: no pre-run capture of the night mode.
    assert.ok(!/settings get secure ui_night_mode|cmd",?\s*"?uimode"?\s*,?\s*"?night"?\s*\]/.test(src)
      || !/restore.*prior|pre-run.*uimode|saveUimode/i.test(src),
      'accepted risk: no prior-value capture/restore exists (ends in no)');
  });
});

describe('T3 — runTapPage plan-interface result contract (AC-RUNTIME-033/036/037)', () => {
  // Batch-2 plan Interface (verbatim, see unit-tests/T3.md):
  //   `parseTapPageArgs(argv: string[]) -> { tapPage: true; assertZeroJniFaults:
  //    boolean; device: string|null }; runTapPage(opts) -> { exitCode: 0|1|2;
  //    pageAdvanced: boolean; blankViewport: boolean; jniFaults:
  //    { unsatisfiedLinkError: number; sigsegv: number } }; exit 0 iff
  //    pageAdvanced && !blankViewport && (assertZeroJniFaults ? zero faults :
  //    true); exit 2 on usage errors`
  // ASSUMPTION A5 (contract genuinely silent): the in-tree `runTapPage` signals
  // via resolve/reject (ok/throw, T6 shape) and the CLI maps resolve -> exit 0,
  // rejection -> exit 1, usage throws -> exit 2. These tests assert the
  // plan-specified result fields on resolve and the exit mapping at the CLI;
  // reconcile field plumbing (not verdicts) with the coder.
  it('ok run reports pageAdvanced true, blankViewport false, zero jniFaults, exitCode 0', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    const result = await galleryTap.runTapPage({
      device: 'stubbed-no-device',
      assertZeroJniFaults: true,
      deps: okDeps(),
      emit: () => {},
    });
    assert.equal(result.pageAdvanced, true, 'tap must have advanced the page');
    assert.equal(result.blankViewport, false, 'new page must not be a blank viewport');
    assert.deepEqual(result.jniFaults, { unsatisfiedLinkError: 0, sigsegv: 0 });
    assert.equal(result.exitCode, 0, 'exit 0 iff advanced && !blank && zero faults');
  });

  it('without --assert-zero-jni-faults the fault scan is skipped (exitCode 0 despite faults)', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    const result = await galleryTap.runTapPage({
      device: 'stubbed-no-device',
      assertZeroJniFaults: false,
      deps: okDeps({ logcat: 'E/MuseHost: java.lang.UnsatisfiedLinkError: nativeTap\nF/DEBUG: signal 11 (SIGSEGV)\n' }),
      emit: () => {},
    });
    assert.equal(result.exitCode, 0);
    assert.equal(result.pageAdvanced, true);
  });

  it('fault counts are reported per pattern (2x UnsatisfiedLinkError + 1x SIGSEGV)', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    const logcat = [
      'I/MuseHost: tap dispatched',
      'E/MuseHost: java.lang.UnsatisfiedLinkError: nativeTap',
      'E/MuseHost: java.lang.UnsatisfiedLinkError: nativeRender',
      'F/DEBUG: signal 11 (SIGSEGV) fault addr 0x0',
      '',
    ].join('\n');
    await assert.rejects(
      galleryTap.runTapPage({
        device: 'stubbed-no-device',
        assertZeroJniFaults: true,
        deps: okDeps({ logcat }),
        emit: () => {},
      }),
      (err) => {
        const text = `${err.code || ''} ${err.detail || err.message || ''}`;
        assert.ok(/JNI-FAULT/i.test(text), 'fault run must fail with JNI-FAULT');
        assert.ok(/unsatisfiedlinkerror/i.test(text) && /sigsegv|signal 11/i.test(text),
          `failure must name both fault kinds, got: ${text.slice(0, 300)}`);
        const counts = err.jniFaults || {};
        assert.equal(counts.unsatisfiedLinkError, 2, 'must count both UnsatisfiedLinkError lines');
        assert.equal(counts.sigsegv, 1, 'must count the SIGSEGV line');
        return true;
      },
    );
  });

  it('non-advancing page reports pageAdvanced false (exit 1, never exit 0)', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    await assert.rejects(
      galleryTap.runTapPage({
        device: 'stubbed-no-device',
        assertZeroJniFaults: false,
        deps: okDeps({ pageBefore: 2, pageAfter: 2 }),
        emit: () => {},
      }),
      (err) => {
        assert.ok(/PAGE-NOT-ADVANCED/.test(`${err.code || ''}`), 'must fail with PAGE-NOT-ADVANCED');
        assert.equal(err.pageAdvanced, false, 'failure must report pageAdvanced false');
        return true;
      },
    );
  });

  it('blank viewport reports blankViewport true (exit 1, never exit 0)', async () => {
    assert.ok(galleryTap, 'lib/gallery-tap.js must load');
    await assert.rejects(
      galleryTap.runTapPage({
        device: 'stubbed-no-device',
        assertZeroJniFaults: true,
        deps: okDeps({ blank: true }),
        emit: () => {},
      }),
      (err) => {
        assert.ok(/BLANK-VIEWPORT/.test(`${err.code || ''}`), 'must fail with BLANK-VIEWPORT');
        assert.equal(err.blankViewport, true, 'failure must report blankViewport true');
        return true;
      },
    );
  });
});

describe('T3 — gallery-tap CLI exit-code mapping (TC-RUNTIME-033/036/037 Check)', () => {
  it('device-less gallery-tap --tap-page exits 1 fail-closed naming the cause', () => {
    const res = runCli(['gallery-tap', '--tap-page'], {
      env: { ...process.env, ANDROID_SERIAL: '' },
    });
    assert.equal(res.status, 1, `device-less run must exit 1, got ${res.status}`);
    assert.ok(/TAP-PAGE-FAIL|NO-DEVICE-RUNNER|device/i.test(`${res.stdout}\n${res.stderr}`),
      'device-less failure must name the cause');
  });

  it('device-less gallery-tap --tap-page --assert-zero-jni-faults exits 1 fail-closed', () => {
    const res = runCli(['gallery-tap', '--tap-page', '--assert-zero-jni-faults'], {
      env: { ...process.env, ANDROID_SERIAL: '' },
    });
    assert.equal(res.status, 1, `device-less run must exit 1, got ${res.status}`);
    assert.ok(/TAP-PAGE-FAIL|NO-DEVICE-RUNNER|device/i.test(`${res.stdout}\n${res.stderr}`),
      'device-less failure must name the cause');
  });
});
