'use strict';
// Batch 1 — gallery-real gates (T1 verify preconditions surface, T2 proof flags).
// T1 Interface (verbatim from plan):
//   `node packages/cli/bin/muse-native.js gallery-real --assert-surface-contract [--device <serial>] -> exit 0 | exit 1 FAIL <code> | exit 2 usage`
// T2 Interface (verbatim from plan):
//   `node packages/cli/bin/muse-native.js gallery-real --assert-no-diagram --assert-counts [--device <serial>] -> exit 0 | exit 1 FAIL <code> | exit 2 usage; node packages/cli/bin/muse-native.js gallery-real --assert-interactive [--device <serial>] -> exit 0 | exit 1 FAIL <code> | exit 2 usage; node packages/cli/bin/muse-native.js gallery-real --assert-real-pixels [--device <serial>] -> exit 0 | exit 1 FAIL <code> | exit 2 usage`
// Hermetic: node:test + node:assert/strict + stdlib only. CLI cases assert only
// usage/exit paths (exit 2) or fail-closed device-less paths (exit 1
// NO-DEVICE-RUNNER / INTERACTIVE NO-DEVICE-RUNNER) — never adb, never a
// device, never network. Lib cases inject fakes or use pure parsers.
// ASSUMPTION A1: explicit `--device` on the FR-M3-011 legs is NOT accepted by
// lib/gallery-real.js parseGalleryRealArgs (known set has no --device); the
// serial travels via ANDROID_SERIAL. A `--device` token on those legs is a
// usage error (exit 2), not a device selection. Reconcile names, not verdicts.
// ASSUMPTION A2: `--help` on the surface-contract leg exits 0 with usage;
// `--help` on the gallery-real.js leg exits 2 with usage (mirrors that
// module's existing `opts.help -> exit 2` behavior). Tested as-is.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

const contract = require('../lib/gallery-surface-contract.js');
const real = require('../lib/gallery-real.js');

function runCli(args) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    env: { ...process.env, ANDROID_SERIAL: '' },
  });
}

const CLEAN_INTERFACE_TEXT = [
  '// seam interface: zero gallery-concept tokens',
  'struct MuseSurface {',
  '  int handle;',
  '};',
  'int attachSurface(int handle);',
  'int renderFrame(int handle);',
  '',
].join('\n');

function okSurfaceDeps(overrides = {}) {
  const base = {
    steps: ['attach', 'render', 'touch', 'theme', 'ime', 'detach', 'destroy'],
    fallbackToStaticRoot: false,
    stats: { presented: 3 },
    logcat: 'I/MuseSurface: attached handle=1\nI/MuseSurface: frame presented\n',
  };
  const seq = { ...base, ...overrides };
  return {
    driveSequence: async () => ({ steps: seq.steps, fallbackToStaticRoot: seq.fallbackToStaticRoot }),
    readFrameStats: async () => seq.stats,
    readLogcat: async () => seq.logcat,
    readInterfaceText: async () => CLEAN_INTERFACE_TEXT,
  };
}

describe('T1 gallery-real --assert-surface-contract (AC-RUNTIME-035 / FR-RUNTIME-011)', () => {
  it('help lists --assert-surface-contract and exits 0', () => {
    const r = runCli(['gallery-real', '--assert-surface-contract', '--help']);
    assert.equal(r.status, 0);
    assert.match(r.stdout, /--assert-surface-contract/);
  });

  it('unknown flag exits 2 with usage', () => {
    const r = runCli(['gallery-real', '--assert-surface-contract', '--bogus-flag']);
    assert.equal(r.status, 2);
    assert.match(r.stderr, /Usage: muse-native gallery-real --assert-surface-contract/);
  });

  it('missing --device value exits 2', () => {
    const r = runCli(['gallery-real', '--assert-surface-contract', '--device']);
    assert.equal(r.status, 2);
    assert.match(r.stderr, /--device/);
  });

  it('bare gallery-real with no assert flag exits 2 (missing required flag)', () => {
    const r = runCli(['gallery-real']);
    assert.equal(r.status, 2);
    assert.match(r.stderr, /--assert-surface-contract/);
  });

  it('device-less run fails closed exit 1 (CLI, no serial)', () => {
    const r = runCli(['gallery-real', '--assert-surface-contract']);
    assert.equal(r.status, 1);
    assert.match(r.stderr, /NO-DEVICE-RUNNER/);
  });

  it('lib parseSurfaceContractArgs parses --device serial', () => {
    const opts = contract.parseSurfaceContractArgs(['gallery-real', '--assert-surface-contract', '--device', 'emulator-5554']);
    assert.equal(opts.assertSurfaceContract, true);
    assert.equal(opts.device, 'emulator-5554');
  });

  it('lib parseSurfaceContractArgs throws on missing assert flag', () => {
    assert.throws(() => contract.parseSurfaceContractArgs(['gallery-real', '--device', 'X']), /--assert-surface-contract/);
  });

  it('lib runSurfaceContract resolves ok with injected deps (hermetic)', async () => {
    const lines = [];
    const result = await contract.runSurfaceContract({ deps: okSurfaceDeps(), emit: (l) => lines.push(l) });
    assert.equal(result.ok, true);
    assert.equal(lines.length, 7);
  });

  it('lib runSurfaceContract rejects device-less with NO-DEVICE-RUNNER (fail-closed)', async () => {
    await assert.rejects(
      contract.runSurfaceContract({ device: null, deps: {} }),
      (err) => err && err.code === 'NO-DEVICE-RUNNER',
    );
  });

  it('lib runSurfaceContract fails closed on zero presents', async () => {
    await assert.rejects(
      contract.runSurfaceContract({ deps: okSurfaceDeps({ stats: { presented: 0 } }), emit: () => {} }),
      (err) => err && err.code === 'ZERO-PRESENTS',
    );
  });

  it('lib runSurfaceContract fails closed on FATAL logcat', async () => {
    await assert.rejects(
      contract.runSurfaceContract({ deps: okSurfaceDeps({ logcat: 'F/emuse-host: FATAL error' }), emit: () => {} }),
      (err) => err && err.code === 'FATAL-LOGCAT',
    );
  });
});

describe('T2 gallery-real proof flags (AC-M3-027 / AC-RUNTIME-035)', () => {
  it('--assert-no-diagram --help exits 2 with usage', () => {
    const r = runCli(['gallery-real', '--assert-no-diagram', '--help']);
    assert.equal(r.status, 2);
    assert.match(`${r.stdout}${r.stderr}`, /--assert-no-diagram/);
  });

  it('--assert-no-diagram with unknown flag exits 2', () => {
    const r = runCli(['gallery-real', '--assert-no-diagram', '--bogus-flag']);
    assert.equal(r.status, 2);
    assert.match(r.stderr, /Usage: muse-native gallery-real/);
  });

  it('asserting nothing (bare --page or no flag) exits 2, never silent pass', () => {
    const r = runCli(['gallery-real', '--page', 'controls']);
    assert.equal(r.status, 2);
    assert.match(r.stderr, /nothing to assert/);
  });

  it('--assert-no-diagram --page <clean> exits 0 hermetic (no fs scan)', () => {
    const r = runCli(['gallery-real', '--assert-no-diagram', '--page', 'controls']);
    assert.equal(r.status, 0);
    assert.match(r.stdout, /gallery-real ok/);
  });

  it('--assert-no-diagram --page reaching mock painter exits 1', () => {
    const r = runCli(['gallery-real', '--assert-no-diagram', '--page', 'MockPainter-page']);
    assert.equal(r.status, 1);
    assert.match(r.stderr, /mock-painter reachable/i);
  });

  it('--assert-interactive device-less fails closed exit 1 NO-DEVICE-RUNNER', () => {
    const r = runCli(['gallery-real', '--assert-interactive']);
    assert.equal(r.status, 1);
    assert.match(r.stderr, /NO-DEVICE-RUNNER/);
  });

  it('--assert-interactive --help exits 0 and lists the flag', () => {
    const r = runCli(['gallery-real', '--assert-interactive', '--help']);
    assert.equal(r.status, 0);
    assert.match(r.stdout, /--assert-interactive/);
  });

  it('lib parseGalleryRealArgs accepts all four T2 flags', () => {
    const opts = real.parseGalleryRealArgs(['gallery-real', '--assert-no-diagram', '--assert-counts', '--assert-interactive', '--assert-real-pixels']);
    assert.equal(opts.assertNoDiagram, true);
    assert.equal(opts.assertCounts, true);
    assert.equal(opts.assertInteractive, true);
    assert.equal(opts.assertRealPixels, true);
  });

  it('lib parseGalleryRealArgs throws usage on unknown flag', () => {
    assert.throws(() => real.parseGalleryRealArgs(['gallery-real', '--assert-no-diagram', '--bogus']), (err) => err && err.code === 'GALLERY_REAL_USAGE');
  });

  it('lib parseGalleryRealArgs throws usage on missing --page value', () => {
    assert.throws(() => real.parseGalleryRealArgs(['gallery-real', '--assert-no-diagram', '--page']), (err) => err && err.code === 'GALLERY_REAL_USAGE');
  });

  it('lib assertNoMockPainter passes clean target, names mock symbol', () => {
    assert.equal(real.assertNoMockPainter('controls'), undefined);
    assert.throws(() => real.assertNoMockPainter('MockPainter-x'), /MockPainter/);
    assert.throws(() => real.assertNoMockPainter(''), /invalid-pageTarget/);
    assert.throws(() => real.assertNoMockPainter('__proto__'), /MockReachableError|unsafe/);
  });

  it('lib runInteractiveAssertions returns per-kind report; unknown page throws unknown-page', () => {
    assert.deepEqual(real.runInteractiveAssertions('controls'), { tap: true, focus: false, caret: false, scroll: false, dismiss: false });
    assert.deepEqual(real.runInteractiveAssertions('text'), { tap: false, focus: true, caret: true, scroll: false, dismiss: false });
    assert.throws(() => real.runInteractiveAssertions('no-such-page'), (err) => err && err.code === 'unknown-page');
    assert.throws(() => real.runInteractiveAssertions(''), (err) => err && err.code === 'unknown-page');
  });

  it('lib parseInteractiveArgs requires --assert-interactive, parses --device', () => {
    const opts = contract.parseInteractiveArgs(['gallery-real', '--assert-interactive', '--device', 'emulator-5554']);
    assert.equal(opts.assertInteractive, true);
    assert.equal(opts.device, 'emulator-5554');
    assert.throws(() => contract.parseInteractiveArgs(['gallery-real', '--device', 'X']), /--assert-interactive/);
  });

  it('lib runGalleryRealInteractive all-pass resolves exitCode 0 hermetic', async () => {
    const rows = contract.INTERACTIVE_CHECKS.map((check) => ({ check, pass: true }));
    const res = await contract.runGalleryRealInteractive(['gallery-real', '--assert-interactive'], {
      deps: { driveInteractive: async () => rows, readMockSymbols: async () => [] },
      emit: () => {},
    });
    assert.equal(res.exitCode, 0);
  });

  it('lib runGalleryRealInteractive failing check resolves exitCode 1 naming the check', async () => {
    const rows = contract.INTERACTIVE_CHECKS.map((check) => ({ check, pass: check !== 'tap' }));
    const res = await contract.runGalleryRealInteractive(['gallery-real', '--assert-interactive'], {
      deps: { driveInteractive: async () => rows, readMockSymbols: async () => [] },
      emit: () => {},
    });
    assert.equal(res.exitCode, 1);
    assert.equal(res.code, 'tap');
  });
});
