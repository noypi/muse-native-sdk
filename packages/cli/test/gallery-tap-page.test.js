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
// T1 (Batch 1) — `gallery-tap --tap-page --assert-zero-jni-faults` arg parsing
// only. Spec refs: FR-RUNTIME-012, AC-RUNTIME-036/037 (parent context);
// specs-engine-runtime.md FR-RUNTIME-012 Notes (FR-INPUT-011 untouched,
// FR-RUNTIME-008 untouched). Neighbor: packages/cli/test/gallery-tap.test.js
// must stay green (not edited here).
// Interface (plan T1 verbatim):
//   `parseGalleryTapArgs(argv: string[]): { tapPage: boolean, assertZeroJniFaults: boolean, button: string|null, serial: string|null, help: boolean }`
//   — unknown flag or `--assert-zero-jni-faults` without `--tap-page` throws
//   `OptionError` (code `GALLERY_TAP_USAGE`, CLI exit 2); `--tap-page`
//   without `--button` is legal (paging needs no button name).
// Scope: arg-parsing cases only — both flags parse true, fault-without-tap-page
// exits 2 naming requirement, tap-page without --button legal, unknown flag
// exits 2, existing parsing untouched. No device sequence / fault-scan behavior
// (those are T2/T3). Hermetic: node:test + node:assert/strict + stdlib only;
// adb is never invoked (parse-level assertions + usage-error CLI runs that
// exit before any lane).
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T1: `parseGalleryTapArgs` keeps its current export shape
//   (`module.exports.parseGalleryTapArgs`, also reachable via the module
//   function object) and its current input convention (leading
//   `'gallery-tap'` element stripped when present).
// - A-T2: boolean flags default false; `button`/`serial` default null;
//   `help` defaults false; `-h`/`--help` still set help without throwing.
// - A-T3: `--tap-page` / `--assert-zero-jni-faults` are space-free boolean
//   flags (no `--flag=value` form); the usage error message names the
//   offending requirement (`--tap-page` when the fault flag stands alone).
// - A-T4: CLI exit code for usage errors stays 2 via `getExitCode('config')`,
//   matching the existing gallery-tap lane.

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

function parse(argv) {
  assert.ok(galleryTap, 'lib/gallery-tap.js must load');
  assert.equal(
    typeof galleryTap.parseGalleryTapArgs,
    'function',
    'gallery-tap module must export parseGalleryTapArgs',
  );
  return galleryTap.parseGalleryTapArgs(argv);
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

describe('T1 — gallery-tap page arg parsing (FR-RUNTIME-012)', () => {
  it('both flags parse true alongside button/serial', () => {
    const opts = parse([
      'gallery-tap',
      '--button', 'Button-filled',
      '--tap-page',
      '--assert-zero-jni-faults',
      '--serial', 'emulator-5554',
    ]);
    assert.equal(opts.tapPage, true);
    assert.equal(opts.assertZeroJniFaults, true);
    assert.equal(opts.button, 'Button-filled');
    assert.equal(opts.serial, 'emulator-5554');
  });

  it('--tap-page alone parses true with fault flag false', () => {
    const opts = parse(['gallery-tap', '--tap-page']);
    assert.equal(opts.tapPage, true);
    assert.equal(opts.assertZeroJniFaults, false);
  });

  it('--tap-page without --button is legal (button stays null)', () => {
    const opts = parse(['gallery-tap', '--tap-page']);
    assert.equal(opts.button, null);
    assert.equal(opts.tapPage, true);
  });

  it('--assert-zero-jni-faults without --tap-page throws OptionError GALLERY_TAP_USAGE', () => {
    assert.throws(
      () => parse(['gallery-tap', '--assert-zero-jni-faults']),
      (err) => {
        assert.equal(err.name, 'OptionError');
        assert.equal(err.code, 'GALLERY_TAP_USAGE');
        assert.ok(/--tap-page/.test(err.message), 'error must name the --tap-page requirement');
        return true;
      },
    );
  });

  it('unknown flag throws OptionError GALLERY_TAP_USAGE', () => {
    assert.throws(
      () => parse(['gallery-tap', '--bogus-flag']),
      (err) => {
        assert.equal(err.name, 'OptionError');
        assert.equal(err.code, 'GALLERY_TAP_USAGE');
        assert.ok(/--bogus-flag/.test(err.message), 'error must name the unknown flag');
        return true;
      },
    );
  });

  it('existing parsing untouched: button/serial/assert flags and help still parse', () => {
    const opts = parse([
      '--button', 'Button-filled',
      '--assert-once',
      '--assert-outside-silent',
      '--drag-no-fire',
      '--serial', 'emulator-5554',
    ]);
    assert.equal(opts.button, 'Button-filled');
    assert.equal(opts.serial, 'emulator-5554');
    assert.equal(opts.help, false);
    const helpOpts = parse(['--help']);
    assert.equal(helpOpts.help, true);
  });

  it('defaults stay hermetic: flags false, button/serial null, help false', () => {
    const opts = parse([]);
    assert.equal(opts.tapPage, false);
    assert.equal(opts.assertZeroJniFaults, false);
    assert.equal(opts.button, null);
    assert.equal(opts.serial, null);
    assert.equal(opts.help, false);
  });
});

describe('T1 — gallery-tap page CLI exit codes (usage errors exit 2)', () => {
  it('fault flag without --tap-page exits 2 naming the requirement', () => {
    const res = runCli(['gallery-tap', '--assert-zero-jni-faults']);
    assert.equal(res.status, 2, `expected exit 2, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
    assert.ok(
      /--tap-page/.test(`${res.stdout}\n${res.stderr}`),
      'exit-2 output must name the --tap-page requirement',
    );
  });

  it('unknown page-ish flag exits 2 naming the flag', () => {
    const res = runCli(['gallery-tap', '--tap-page', '--bogus-flag']);
    assert.equal(res.status, 2, `expected exit 2, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
    assert.ok(
      /--bogus-flag/.test(`${res.stdout}\n${res.stderr}`),
      'exit-2 output must name the unknown flag',
    );
  });
});

// ── T2 (Batch 2) — tap-page device sequence with page-advance + ──
// no-blank-viewport assertions. Spec refs: FR-RUNTIME-012, AC-RUNTIME-036.
// Interface (plan T2 verbatim):
//   `runGalleryTap(['gallery-tap','--tap-page','--serial',s]) => exit 0 iff
//   page index advances AND post-tap screencap is non-blank; exit 1 with
//   gallery-tap results (tap-page, lane device serial <s>): tabulation naming
//   page-advanced / no-blank-viewport rows otherwise` — any failing adb step
//   exits 1 naming serial + failed adb args.
// Scope: stubbed-adb CLI runs only (recorded argv). No T1 rewrite; T3
// (fault scan) out of scope.
// Hermetic: node:test + node:assert/strict + stdlib only; adb is a tmp-bin
// `sh` stub prepended to PATH (real adb never runs). Each test owns its
// tmp roots (no shared mutable state).
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: the page tap reuses the existing device-lane tap coordinates
//   (150,250) — the argv `input tap 150 250` must appear in the recorded
//   sequence; a right-edge fallback tap is NOT asserted here.
// - A2: page-index detection reads the `muse-host` logcat tag
//   (`adb -s <s> logcat -d -s muse-host`) before/after the tap, backed by a
//   screencap non-blank assertion (`adb -s <s> exec-out screencap -p`).
//   Stub logcat emits `muse-host: page-index <N>` lines; the parser is
//   expected to match /page[-_ ]?index\s*[:= ]\s*(\d+)/i (with a
//   /page\s+(\d+)/ fallback). Non-blank screencap = non-empty stdout;
//   blank = empty stdout (screencap fallback for advance is NOT required
//   here — an unchanged index fails even with a non-blank frame).

const fs = require('node:fs');
const os = require('node:os');

const T2_TMP_ROOTS = [];
function t2mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  T2_TMP_ROOTS.push(dir);
  return dir;
}

// Stateful stubbed `adb`: records every argv line to <bin>/adb.log and
// serves canned outputs. `logcat -d` calls are counted: the 1st dump
// prints d1, the 2nd and later print d2 (pre/post page-index reads).
// `exec-out screencap` prints `png` verbatim (empty string = blank).
// When `failOn` (substring of "$*") matches, exits `failExit` (default 1)
// printing `failOut` — models an unreachable serial / failing step.
function writeTapPageAdbStub({ d1, d2, png, failOn, failExit, failOut }) {
  const bin = t2mkTmp('muse-tap-page-bin-');
  const log = path.join(bin, 'adb.log');
  const countFile = path.join(bin, 'adb.count');
  fs.writeFileSync(log, '');
  fs.writeFileSync(countFile, '0');
  const script = [
    '#!/bin/sh',
    `LOG=${JSON.stringify(log)}`,
    `COUNT_FILE=${JSON.stringify(countFile)}`,
    `D1=${JSON.stringify(d1 === undefined ? '' : d1)}`,
    `D2=${JSON.stringify(d2 === undefined ? '' : d2)}`,
    `PNG=${JSON.stringify(png === undefined ? '' : png)}`,
    `FAIL_ON=${JSON.stringify(failOn === undefined ? '' : failOn)}`,
    `FAIL_EXIT=${failExit === undefined ? 1 : failExit}`,
    `FAIL_OUT=${JSON.stringify(failOut === undefined ? '' : failOut)}`,
    'printf \'%s\\n\' "$*" >> "$LOG"',
    'if [ -n "$FAIL_ON" ]; then case "$*" in *"$FAIL_ON"*) [ -n "$FAIL_OUT" ] && printf \'%s\\n\' "$FAIL_OUT"; exit "$FAIL_EXIT";; esac; fi',
    'case "$*" in',
    '(*"logcat"*" -d"*)',
    '  N=$(cat "$COUNT_FILE" 2>/dev/null || echo 0); N=$((N + 1)); printf \'%s\' "$N" > "$COUNT_FILE";',
    '  if [ "$N" = "1" ]; then printf \'%s\\n\' "$D1"; else printf \'%s\\n\' "$D2"; fi; exit 0;;',
    '(*"exec-out"*"screencap"*) printf \'%s\' "$PNG"; exit 0;;',
    '(*) exit 0;;',
    'esac',
  ].join('\n');
  fs.writeFileSync(path.join(bin, 'adb'), `${script}\n`, { mode: 0o755 });
  return { bin, log };
}

function t2WithPath(bin, options) {
  const env = { ...process.env, PATH: `${bin}${path.delimiter}${process.env.PATH}` };
  delete env.ANDROID_SERIAL;
  return { ...options, env };
}

function t2ReadLog(log) {
  try {
    return fs.readFileSync(log, 'utf8').split('\n').filter((l) => l.length > 0);
  } catch {
    return [];
  }
}

// Asserts the recorded argv lines contain the tap-page adb steps in order:
// install → force-stop → logcat -c → am start → input tap (150,250) →
// logcat -d → screencap. Extra invocations (e.g. a second logcat -d
// re-read) are allowed; order is what matters.
function t2AssertStepOrder(lines) {
  const need = [
    /install/,
    /force-stop/,
    /logcat.*-c/,
    /am.*start/,
    /input.*tap.*150.*250/,
    /logcat.*-d/,
    /screencap/,
  ];
  let idx = -1;
  for (const re of need) {
    const next = lines.findIndex((l, i) => i > idx && re.test(l));
    assert.ok(next > idx, `adb sequence must contain ${re} in order; got:\n${lines.join('\n')}`);
    idx = next;
  }
}

const T2_SERIAL = 'emulator-5554';
const T2_PAGE_BEFORE = 'muse-host: page-index 2\n';
const T2_PAGE_AFTER = 'muse-host: page-index 3\n';
const T2_PNG = `PNGFAKEDATA-${'A'.repeat(200)}`;

describe('T2 — tap-page device sequence (FR-RUNTIME-012, AC-RUNTIME-036)', () => {
  it('page index 2→3 with non-blank screencap exits 0 with page-advanced + no-blank-viewport pass rows', () => {
    const { bin } = writeTapPageAdbStub({ d1: T2_PAGE_BEFORE, d2: T2_PAGE_AFTER, png: T2_PNG });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--serial', T2_SERIAL],
      t2WithPath(bin, { cwd: t2mkTmp('muse-tap-page-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 0, `expected exit 0, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/tap-page/.test(out), 'output must name the tap-page lane');
    assert.ok(new RegExp(T2_SERIAL.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).test(out), 'output must name the lane device serial');
    assert.ok(/page-advanced/.test(out), 'output must tabulate the page-advanced row');
    assert.ok(/no-blank-viewport/.test(out), 'output must tabulate the no-blank-viewport row');
  });

  it('tap-page run shells install/force-stop/logcat-c/start/tap/logcat-d/screencap in order', () => {
    const { bin, log } = writeTapPageAdbStub({ d1: T2_PAGE_BEFORE, d2: T2_PAGE_AFTER, png: T2_PNG });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--serial', T2_SERIAL],
      t2WithPath(bin, { cwd: t2mkTmp('muse-tap-page-cwd-') }),
    );
    assert.equal(res.status, 0, `expected exit 0, got ${res.status}; out: ${`${res.stdout}\n${res.stderr}`.slice(0, 800)}`);
    t2AssertStepOrder(t2ReadLog(log));
  });

  it('unchanged page index exits 1 naming the page-advanced row', () => {
    const { bin } = writeTapPageAdbStub({ d1: T2_PAGE_BEFORE, d2: T2_PAGE_BEFORE, png: T2_PNG });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--serial', T2_SERIAL],
      t2WithPath(bin, { cwd: t2mkTmp('muse-tap-page-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 1, `unchanged index must exit 1, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/page-advanced/.test(out), 'exit-1 output must name the failing page-advanced row');
    assert.ok(/fail/.test(out), 'exit-1 output must mark the row fail');
  });

  it('blank screencap exits 1 naming the no-blank-viewport row', () => {
    const { bin } = writeTapPageAdbStub({ d1: T2_PAGE_BEFORE, d2: T2_PAGE_AFTER, png: '' });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--serial', T2_SERIAL],
      t2WithPath(bin, { cwd: t2mkTmp('muse-tap-page-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 1, `blank screencap must exit 1, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/no-blank-viewport/.test(out), 'exit-1 output must name the failing no-blank-viewport row');
    assert.ok(/fail/.test(out), 'exit-1 output must mark the row fail');
  });

  it('unreachable serial exits 1 naming the serial', () => {
    const { bin } = writeTapPageAdbStub({
      d1: T2_PAGE_BEFORE, d2: T2_PAGE_AFTER, png: T2_PNG,
      failOn: 'install', failExit: 1, failOut: 'device not found',
    });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--serial', '127.0.0.1:5999'],
      t2WithPath(bin, { cwd: t2mkTmp('muse-tap-page-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 1, `unreachable serial must exit 1, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/127\.0\.0\.1:5999/.test(out), 'exit-1 output must name the serial');
  });

  it('any failing adb step exits 1 naming serial + failed adb args', () => {
    const { bin } = writeTapPageAdbStub({
      d1: T2_PAGE_BEFORE, d2: T2_PAGE_AFTER, png: T2_PNG,
      failOn: 'input tap', failExit: 1, failOut: 'closed',
    });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--serial', T2_SERIAL],
      t2WithPath(bin, { cwd: t2mkTmp('muse-tap-page-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 1, `failing adb step must exit 1, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(new RegExp(T2_SERIAL.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).test(out), 'exit-1 output must name the serial');
    assert.ok(/input tap|tap/.test(out), 'exit-1 output must name the failed adb args');
  });

  it('--tap-page without --serial exits 1 naming no-device without invoking adb (fail closed)', () => {
    const { bin } = writeTapPageAdbStub({ d1: T2_PAGE_BEFORE, d2: T2_PAGE_AFTER, png: T2_PNG });
    const res = runCli(
      ['gallery-tap', '--tap-page'],
      t2WithPath(bin, { cwd: t2mkTmp('muse-tap-page-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 1, `--tap-page without --serial must exit 1, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/no-device|no device/i.test(out), 'exit-1 output must name no-device');
    assert.ok(!fs.existsSync(path.join(bin, 'adb.log')) || t2ReadLog(path.join(bin, 'adb.log')).length === 0,
      'fail-closed run must not invoke adb');
  });
});
// ── T3 (Batch 3) — tap-page zero-jni-faults gate across full logcat. ──
// Spec refs: FR-RUNTIME-012, AC-RUNTIME-037.
// Interface (plan T3 verbatim):
//   `runGalleryTap(['gallery-tap','--tap-page','--assert-zero-jni-faults','--serial',s])
//   => exit 0 iff T2 passes AND full `adb -s s logcat -d` contains zero
//   `UnsatisfiedLinkError` and zero `SIGSEGV|Fatal signal.*SIGSEGV` matches;
//   any match prints `zero-jni-faults fail` + first matching line (truncated
//   300 chars) and exits 1` — `--tap-page` without `--serial` exits 1 naming
//   no-device (fail closed, never touches adb); default lane never shells adb.
// Scope: stubbed-adb CLI runs only. No T1/T2 rewrite (16 cases stay green).
// Hermetic: node:test + node:assert/strict + stdlib only; adb is a tmp-bin
// `sh` stub prepended to PATH (real adb never runs). Full-buffer scan reads
// the unfiltered `logcat -d` dump (no `-s muse-host` filter); tagged
// `logcat -d -s muse-host` reads still serve the page index.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A3: the fault scan covers the full buffer from this run's `logcat -c`
//   (pre-run history cannot leak in); the stub models this by serving FULL
//   verbatim for the unfiltered dump.
// - A4: tap-page needs a serial — `--tap-page` without `--serial` fails
//   closed (exit 1 naming no-device) without invoking adb, with or without
//   the fault flag (nfr-audit Checks must not hang).

const T3_TMP_ROOTS = [];
function t3mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  T3_TMP_ROOTS.push(dir);
  return dir;
}

// Stateful stubbed `adb` for T3: records every argv line to <bin>/adb.log.
// - `logcat -d -s muse-host` (tagged): 1st call prints d1, 2nd+ print d2
//   (pre/post page-index reads, `muse-host: page-index <N>`).
// - plain `logcat -d` (no muse-host): prints FULL verbatim (fault buffer).
// - `exec-out screencap`: prints `png` verbatim (empty = blank).
// - `failOn` substring match: exits `failExit` printing `failOut`.
function writeFaultGateAdbStub({ d1, d2, png, full, failOn, failExit, failOut }) {
  const bin = t3mkTmp('muse-fault-bin-');
  const log = path.join(bin, 'adb.log');
  const countFile = path.join(bin, 'adb.count');
  fs.writeFileSync(log, '');
  fs.writeFileSync(countFile, '0');
  const script = [
    '#!/bin/sh',
    `LOG=${JSON.stringify(log)}`,
    `COUNT_FILE=${JSON.stringify(countFile)}`,
    `D1=${JSON.stringify(d1 === undefined ? '' : d1)}`,
    `D2=${JSON.stringify(d2 === undefined ? '' : d2)}`,
    `PNG=${JSON.stringify(png === undefined ? '' : png)}`,
    `FULL=${JSON.stringify(full === undefined ? '' : full)}`,
    `FAIL_ON=${JSON.stringify(failOn === undefined ? '' : failOn)}`,
    `FAIL_EXIT=${failExit === undefined ? 1 : failExit}`,
    `FAIL_OUT=${JSON.stringify(failOut === undefined ? '' : failOut)}`,
    'printf \'%s\\n\' "$*" >> "$LOG"',
    'if [ -n "$FAIL_ON" ]; then case "$*" in *"$FAIL_ON"*) [ -n "$FAIL_OUT" ] && printf \'%s\\n\' "$FAIL_OUT"; exit "$FAIL_EXIT";; esac; fi',
    'case "$*" in',
    '(*"muse-host"*"logcat"*|*"logcat"*"muse-host"*)',
    '  N=$(cat "$COUNT_FILE" 2>/dev/null || echo 0); N=$((N + 1)); printf \'%s\' "$N" > "$COUNT_FILE";',
    '  if [ "$N" = "1" ]; then printf \'%s\\n\' "$D1"; else printf \'%s\\n\' "$D2"; fi; exit 0;;',
    '(*"logcat"*" -d"*) printf \'%s\\n\' "$FULL"; exit 0;;',
    '(*"exec-out"*"screencap"*) printf \'%s\' "$PNG"; exit 0;;',
    '(*) exit 0;;',
    'esac',
  ].join('\n');
  fs.writeFileSync(path.join(bin, 'adb'), `${script}\n`, { mode: 0o755 });
  return { bin, log };
}

function t3WithPath(bin, options) {
  const env = { ...process.env, PATH: `${bin}${path.delimiter}${process.env.PATH}` };
  delete env.ANDROID_SERIAL;
  return { ...options, env };
}

function t3ReadLog(log) {
  try {
    return fs.readFileSync(log, 'utf8').split('\n').filter((l) => l.length > 0);
  } catch {
    return [];
  }
}

const T3_SERIAL = 'emulator-5554';
const T3_PAGE_BEFORE = 'muse-host: page-index 2\n';
const T3_PAGE_AFTER = 'muse-host: page-index 3\n';
const T3_PNG = `PNGFAKEDATA-${'A'.repeat(200)}`;
const T3_CLEAN = 'muse-host: page-index 3\nI/ActivityManager: clean run\n';
const T3_JNI = `I/muse-host: page-index 3\nE/AndroidRuntime: java.lang.UnsatisfiedLinkError: No implementation found for void com.muse.host.Native.tick()${'B'.repeat(400)}\n`;
const T3_SEGV = 'I/muse-host: page-index 3\nF/libc: Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR) in tid 1234\n';
const T3_BARE_SEGV = 'W/native: SIGSEGV received in render thread\n';

describe('T3 — tap-page zero-jni-faults gate (FR-RUNTIME-012, AC-RUNTIME-037)', () => {
  it('stubbed adb with clean logcat exits 0 tabulating zero-jni-faults pass', () => {
    const { bin } = writeFaultGateAdbStub({
      d1: T3_PAGE_BEFORE, d2: T3_PAGE_AFTER, png: T3_PNG, full: T3_CLEAN,
    });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--assert-zero-jni-faults', '--serial', T3_SERIAL],
      t3WithPath(bin, { cwd: t3mkTmp('muse-fault-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 0, `clean logcat must exit 0, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/zero-jni-faults/.test(out), 'output must tabulate the zero-jni-faults row');
    assert.ok(/pass/.test(out), 'clean run must mark rows pass');
  });

  it('logcat with java.lang.UnsatisfiedLinkError exits 1 naming the fault', () => {
    const { bin } = writeFaultGateAdbStub({
      d1: T3_PAGE_BEFORE, d2: T3_PAGE_AFTER, png: T3_PNG, full: T3_JNI,
    });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--assert-zero-jni-faults', '--serial', T3_SERIAL],
      t3WithPath(bin, { cwd: t3mkTmp('muse-fault-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 1, `UnsatisfiedLinkError must exit 1, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/zero-jni-faults/.test(out), 'exit-1 output must name the zero-jni-faults row');
    assert.ok(/fail/.test(out), 'exit-1 output must mark the row fail');
    assert.ok(/UnsatisfiedLinkError/.test(out), 'exit-1 output must name the fault');
  });

  it('logcat with Fatal signal 11 (SIGSEGV) exits 1 naming the fault', () => {
    const { bin } = writeFaultGateAdbStub({
      d1: T3_PAGE_BEFORE, d2: T3_PAGE_AFTER, png: T3_PNG, full: T3_SEGV,
    });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--assert-zero-jni-faults', '--serial', T3_SERIAL],
      t3WithPath(bin, { cwd: t3mkTmp('muse-fault-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 1, `Fatal signal SIGSEGV must exit 1, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/zero-jni-faults/.test(out), 'exit-1 output must name the zero-jni-faults row');
    assert.ok(/fail/.test(out), 'exit-1 output must mark the row fail');
    assert.ok(/SIGSEGV|Fatal signal/.test(out), 'exit-1 output must name the fault');
  });

  it('logcat with a bare SIGSEGV line exits 1', () => {
    const { bin } = writeFaultGateAdbStub({
      d1: T3_PAGE_BEFORE, d2: T3_PAGE_AFTER, png: T3_PNG, full: T3_BARE_SEGV,
    });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--assert-zero-jni-faults', '--serial', T3_SERIAL],
      t3WithPath(bin, { cwd: t3mkTmp('muse-fault-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 1, `bare SIGSEGV must exit 1, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/zero-jni-faults/.test(out), 'exit-1 output must name the zero-jni-faults row');
  });

  it('--tap-page with the fault flag but no --serial exits 1 naming no-device without invoking adb', () => {
    const { bin } = writeFaultGateAdbStub({
      d1: T3_PAGE_BEFORE, d2: T3_PAGE_AFTER, png: T3_PNG, full: T3_CLEAN,
    });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--assert-zero-jni-faults'],
      t3WithPath(bin, { cwd: t3mkTmp('muse-fault-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 1, `fault-flag run without --serial must exit 1, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/no-device|no device/i.test(out), 'exit-1 output must name no-device');
    assert.ok(t3ReadLog(path.join(bin, 'adb.log')).length === 0, 'fail-closed run must not invoke adb');
  });

  it('--tap-page without --serial exits 1 naming no-device without invoking adb (nfr-audit Check)', () => {
    const { bin } = writeFaultGateAdbStub({
      d1: T3_PAGE_BEFORE, d2: T3_PAGE_AFTER, png: T3_PNG, full: T3_CLEAN,
    });
    const res = runCli(
      ['gallery-tap', '--tap-page'],
      t3WithPath(bin, { cwd: t3mkTmp('muse-fault-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 1, `--tap-page without --serial must exit 1, got ${res.status}; out: ${out.slice(0, 800)}`);
    assert.ok(/no-device|no device/i.test(out), 'exit-1 output must name no-device');
    assert.ok(t3ReadLog(path.join(bin, 'adb.log')).length === 0, 'fail-closed run must not invoke adb');
  });

  it('--tap-page without the fault flag skips the scan (faulty logcat still exits 0 when T2 passes)', () => {
    const { bin } = writeFaultGateAdbStub({
      d1: T3_PAGE_BEFORE, d2: T3_PAGE_AFTER, png: T3_PNG, full: T3_JNI,
    });
    const res = runCli(
      ['gallery-tap', '--tap-page', '--serial', T3_SERIAL],
      t3WithPath(bin, { cwd: t3mkTmp('muse-fault-cwd-') }),
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.equal(res.status, 0, `run without the fault flag must skip the scan (exit 0), got ${res.status}; out: ${out.slice(0, 800)}`);
  });
});
