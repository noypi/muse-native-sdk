'use strict';
// T3 — `muse-native gallery-real --assert-surface-contract` gate (TC-RUNTIME-034, AC-RUNTIME-034).
// Spec refs: SPEC-ENGINE-RUNTIME, FR-RUNTIME-011, AC-RUNTIME-034. Depends on: T2.
// Plan Interface line (verbatim, see unit-tests/T3.md):
//   `muse-native gallery-real --assert-surface-contract [--device <serial>] -> exit 0
//    (contract holds: attach/render/touch/theme/ime/detach sequence ok, frame stats
//    non-zero presents, zero FATAL in logcat, zero gallery-concept tokens in interface)
//    | exit 1 {code: string, detail: string} fail-closed on any seam violation or
//    fallback-to-static-root.`
// Hermetic: node:test + node:assert/strict + stdlib only. Spawns `node bin/muse-native.js`
// for the CLI exit contract. The device/logcat/gallery runner is stubbed via an
// injectable `deps` override (ASSUMPTION A1) — hermetic cases never touch adb, a
// device, the network, secrets, or live services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: `runSurfaceContract` accepts an optional hermetic `deps` override
//   `{ driveSequence, readFrameStats, readLogcat }` plus an optional `emit`
//   line sink, mirroring the gallery-present injectable-runner pattern. The plan
//   Interface line names only the CLI shape; the override is a test-only seam,
//   reconcile names (not verdicts) with the coder.
// - A2: unknown flags and missing `--device` values exit 2 with usage naming the
//   flag (gallery-proof/gallery-present exit-code convention 0/1/2).
// - A3: the failure value is `{ code: string(non-empty), detail: string(non-empty) }`
//   and the CLI prints `SURFACE-CONTRACT-FAIL: <code>: <detail>` on stderr.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

let surfaceContract = null;
try {
  // eslint-disable-next-line global-require
  surfaceContract = require('../lib/gallery-surface-contract.js');
} catch {
  surfaceContract = null;
}

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function okSequence() {
  return {
    steps: ['attach', 'render', 'touch', 'theme', 'ime', 'detach', 'destroy'],
    stats: { presented: 3, dropped: 0, deadlineMissed: 0 },
    logcat: 'I/MuseSurface: attached handle=1\nI/MuseSurface: frame presented\n',
    fallbackToStaticRoot: false,
  };
}

function okDeps(overrides = {}) {
  const seq = { ...okSequence(), ...overrides };
  return {
    driveSequence: async () => ({ steps: seq.steps, fallbackToStaticRoot: seq.fallbackToStaticRoot }),
    readFrameStats: async () => seq.stats,
    readLogcat: async () => seq.logcat,
  };
}

describe('T3 — gallery-surface-contract module surface (AC-RUNTIME-034)', () => {
  it('packages/cli/lib/gallery-surface-contract.js exists and loads', () => {
    assert.ok(surfaceContract !== null, 'lib/gallery-surface-contract.js must exist');
  });

  it('module exports parseSurfaceContractArgs', () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    assert.equal(typeof surfaceContract.parseSurfaceContractArgs, 'function',
      'must export parseSurfaceContractArgs(argv)');
  });

  it('module exports runSurfaceContract', () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    assert.equal(typeof surfaceContract.runSurfaceContract, 'function',
      'must export runSurfaceContract(opts)');
  });
});

describe('T3 — parseSurfaceContractArgs hermetic (no device)', () => {
  it('parses gallery-real --assert-surface-contract with no device', () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    assert.deepEqual(
      surfaceContract.parseSurfaceContractArgs(['gallery-real', '--assert-surface-contract']),
      { assertSurfaceContract: true, device: null },
    );
  });

  it('parses --device <serial>', () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    assert.deepEqual(
      surfaceContract.parseSurfaceContractArgs(
        ['gallery-real', '--assert-surface-contract', '--device', 'emulator-5554'],
      ),
      { assertSurfaceContract: true, device: 'emulator-5554' },
    );
  });

  it('parses without the leading gallery-real token', () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    assert.deepEqual(
      surfaceContract.parseSurfaceContractArgs(['--assert-surface-contract']),
      { assertSurfaceContract: true, device: null },
    );
  });

  it('rejects a missing --assert-surface-contract flag (usage failure, never silent)', () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    assert.throws(
      () => surfaceContract.parseSurfaceContractArgs(['gallery-real']),
      /assert-surface-contract|Usage/i,
    );
  });

  it('rejects unknown flags (exit-2 usage shape)', () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    assert.throws(
      () => surfaceContract.parseSurfaceContractArgs(['gallery-real', '--assert-surface-contract', '--bogus-flag']),
      /bogus-flag|unknown/i,
    );
  });

  it('rejects a missing --device value (usage failure, never silent)', () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    assert.throws(
      () => surfaceContract.parseSurfaceContractArgs(['gallery-real', '--assert-surface-contract', '--device']),
      /device|value|Usage/i,
    );
  });
});

describe('T3 — runSurfaceContract hermetic, device/logcat/gallery stubbed (no device)', () => {
  it('ok sequence resolves with per-step PASS lines and non-zero presents', async () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    const lines = [];
    const result = await surfaceContract.runSurfaceContract({
      device: 'stubbed-no-device',
      deps: okDeps(),
      emit: (line) => lines.push(String(line)),
    });
    assert.equal(result.ok, true);
    for (const step of ['attach', 'render', 'touch', 'theme', 'ime', 'detach', 'destroy']) {
      assert.ok(lines.some((l) => l.includes(step) && /PASS/i.test(l)),
        `must emit a PASS line for step ${step}, got: ${lines.join('; ').slice(0, 400)}`);
    }
    assert.ok((result.stats.presented || 0) > 0, 'frame stats must show non-zero presents');
  });

  it('zero presents fails closed with a named code (never exit 0)', async () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    await assert.rejects(
      surfaceContract.runSurfaceContract({
        device: 'stubbed-no-device',
        deps: okDeps({ stats: { presented: 0, dropped: 0, deadlineMissed: 0 } }),
      }),
      (err) => {
        assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry a named code');
        assert.ok(err && typeof err.detail === 'string' && err.detail.length > 0, 'failure must carry a detail');
        return true;
      },
    );
  });

  it('FATAL in logcat fails closed with a named code', async () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    await assert.rejects(
      surfaceContract.runSurfaceContract({
        device: 'stubbed-no-device',
        deps: okDeps({ logcat: 'I/MuseSurface: attached\nF/Native: FATAL: swapchain lost\n' }),
      }),
      (err) => {
        assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry a named code');
        assert.ok(/fatal/i.test(`${err.code} ${err.detail}`), 'failure must name the FATAL cause');
        return true;
      },
    );
  });

  it('fallback-to-static-root is reported as gate failure (AC-032 carry-through)', async () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    await assert.rejects(
      surfaceContract.runSurfaceContract({
        device: 'stubbed-no-device',
        deps: okDeps({ fallbackToStaticRoot: true }),
      }),
      (err) => {
        assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry a named code');
        assert.ok(/static-root|fallback/i.test(`${err.code} ${err.detail}`),
          'failure must name fallback-to-static-root');
        return true;
      },
    );
  });

  it('failed seam step fails closed with {code, detail} (unknown-kind fixture)', async () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    await assert.rejects(
      surfaceContract.runSurfaceContract({
        device: 'stubbed-no-device',
        deps: {
          driveSequence: async () => {
            const err = new Error('unknown component kind: no render path');
            err.code = 'kCreateFailed';
            throw err;
          },
          readFrameStats: async () => ({ presented: 0, dropped: 0, deadlineMissed: 0 }),
          readLogcat: async () => '',
        },
      }),
      (err) => {
        assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry err.code');
        assert.ok(err && typeof err.detail === 'string' && err.detail.length > 0, 'failure must carry err.detail');
        return true;
      },
    );
  });

  it('touch-step failure fails closed (tap never silently dropped)', async () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    await assert.rejects(
      surfaceContract.runSurfaceContract({
        device: 'stubbed-no-device',
        deps: {
          driveSequence: async () => {
            const err = new Error('dispatchTouch failed: no surface');
            err.code = 'kNoSurface';
            err.step = 'touch';
            throw err;
          },
          readFrameStats: async () => ({ presented: 1, dropped: 0, deadlineMissed: 0 }),
          readLogcat: async () => '',
        },
      }),
      /touch|kNoSurface/i,
    );
  });

  it('missing seam step in the driven sequence fails closed (incomplete attach/render/touch/theme/ime/detach)', async () => {
    assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
    await assert.rejects(
      surfaceContract.runSurfaceContract({
        device: 'stubbed-no-device',
        deps: okDeps({ steps: ['attach', 'render'] }),
      }),
      /step|sequence|missing/i,
    );
  });
});

describe('T3 — gallery-real CLI exit contract (TC-RUNTIME-034 Check)', () => {
  it('gallery-real without --assert-surface-contract exits 2 with usage (never a silent pass)', () => {
    const res = runCli(['gallery-real']);
    assert.equal(res.status, 2, `missing flag must exit 2, got ${res.status}`);
    assert.ok(/Usage|assert-surface-contract/i.test(`${res.stdout}\n${res.stderr}`),
      'missing-flag failure must print usage');
  });

  it('unknown flags exit 2 with usage naming the flag', () => {
    const res = runCli(['gallery-real', '--assert-surface-contract', '--bogus-flag']);
    assert.equal(res.status, 2, `unknown flags must exit 2, got ${res.status}`);
    assert.ok(/bogus-flag|Usage/i.test(`${res.stdout}\n${res.stderr}`),
      'stderr must name the unknown flag with usage');
  });

  it('missing --device value exits 2 with usage (never a silent pass)', () => {
    const res = runCli(['gallery-real', '--assert-surface-contract', '--device']);
    assert.equal(res.status, 2, `missing value must exit 2, got ${res.status}`);
    assert.ok(/Usage|device/i.test(`${res.stdout}\n${res.stderr}`),
      'missing-value failure must print usage');
  });

  it('--help lists --assert-surface-contract', () => {
    const res = runCli(['gallery-real', '--help']);
    assert.equal(res.status, 0, `help must exit 0, got ${res.status}`);
    assert.ok(/assert-surface-contract/.test(`${res.stdout}\n${res.stderr}`),
      'help text must list the --assert-surface-contract flag');
  });
});
