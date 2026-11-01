'use strict';
// T2 — `muse-native gallery-real --assert-interactive` (AC-M3-026, AC-GRAPHICS-043).
// Spec refs: SPEC-M3-NATIVE v (FR-M3-011/AC-M3-026, TC-M3-027), SPEC-GRAPHICS-RENDERING
// (FR-GRAPHICS-015/AC-GRAPHICS-043, TC-GRAPHICS-045). Depends on: T1.
// Plan Interface line (verbatim, see unit-tests/T2.md):
//   `runGalleryRealInteractive(argv: string[]) -> { exitCode: 0|1|2; rows:
//    Array<{check: 'tap'|'focus'|'caret-selection'|'scroll'|'switch-scrub'|
//    'dialog-dismiss'|'sheet-dismiss'|'menu-dismiss'|'state-binding'|
//    'no-diagram'|'shell-paging', pass: boolean}> }; exit 2 + usage on unknown
//    flag (never a silent pass); exit 1 names the failing check; no-diagram
//    failure names the reachable mock-painter symbol`
// Hermetic: node:test + node:assert/strict + stdlib only. Spawns `node
// bin/muse-native.js` for the CLI exit contract. The device/interactive runner
// is stubbed via an injectable `deps` override (ASSUMPTION A1) — hermetic cases
// never touch adb, a device, the network, secrets, live services, or the
// filesystem.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: `lib/gallery-surface-contract.js` exports `runGalleryRealInteractive(argv,
//   opts)` where `opts = { deps, emit }` mirrors the T3 `runSurfaceContract`
//   injectable-runner pattern. The plan names only `argv`; the `opts` second
//   arg is a test-only seam, reconcile names (not verdicts) with the coder.
// - A2: `deps` is `{ driveInteractive, readMockSymbols }`; `driveInteractive()`
//   resolves the 11 check rows, `readMockSymbols()` lists reachable
//   mock-painter symbols for the no-diagram leg. Reconcile names, not verdicts.
// - A3: the failure value is `{ code: string(non-empty), detail: string(non-empty) }`
//   and the CLI prints `INTERACTIVE-FAIL: <code>: <detail>` on stderr naming the
//   failing check (exit 1); usage failures print usage and exit 2.
// - A4: device-less run (no injected deps, no serial) fails closed exit 1 with
//   `NO-DEVICE-RUNNER` (A3 plan assumption: hermetic double carries the gate).

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

const ALL_CHECKS = [
  'tap',
  'focus',
  'caret-selection',
  'scroll',
  'switch-scrub',
  'dialog-dismiss',
  'sheet-dismiss',
  'menu-dismiss',
  'state-binding',
  'no-diagram',
  'shell-paging',
];

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function okDeps(overrides = {}) {
  const failing = new Set(overrides.fail || []);
  const symbols = overrides.mockSymbols || [];
  return {
    driveInteractive: async () => ALL_CHECKS.map((check) => ({ check, pass: !failing.has(check) })),
    readMockSymbols: async () => symbols.slice(),
  };
}

function requireInteractive() {
  assert.ok(surfaceContract, 'lib/gallery-surface-contract.js must load');
  assert.equal(typeof surfaceContract.runGalleryRealInteractive, 'function',
    'must export runGalleryRealInteractive(argv)');
  return surfaceContract.runGalleryRealInteractive;
}

describe('T2 — gallery-real interactive module surface (AC-M3-026/AC-GRAPHICS-043)', () => {
  it('packages/cli/lib/gallery-surface-contract.js exists and loads', () => {
    assert.ok(surfaceContract !== null, 'lib/gallery-surface-contract.js must exist');
  });

  it('module exports runGalleryRealInteractive', () => {
    requireInteractive();
  });
});

describe('T2 — runGalleryRealInteractive happy path (hermetic, no device)', () => {
  it('all 11 checks pass -> exitCode 0 with one row per check', async () => {
    const run = requireInteractive();
    const lines = [];
    const result = await run(['gallery-real', '--assert-interactive'], {
      deps: okDeps(),
      emit: (line) => lines.push(String(line)),
    });
    assert.equal(result.exitCode, 0);
    assert.ok(Array.isArray(result.rows), 'result must carry rows');
    assert.equal(result.rows.length, 11, `must report all 11 checks, got ${result.rows.length}`);
    for (const check of ALL_CHECKS) {
      const row = result.rows.find((r) => r.check === check);
      assert.ok(row, `must report a row for check '${check}'`);
      assert.equal(row.pass, true, `check '${check}' must pass`);
    }
    assert.ok(lines.some((l) => /PASS/i.test(l)), 'must emit PASS lines');
  });

  it('parses without the leading gallery-real token', async () => {
    const run = requireInteractive();
    const result = await run(['--assert-interactive'], {
      deps: okDeps(),
      emit: () => {},
    });
    assert.equal(result.exitCode, 0);
    assert.equal(result.rows.length, 11);
  });
});

describe('T2 — runGalleryRealInteractive failure paths (hermetic)', () => {
  it('unknown flag -> exit 2 with usage naming the flag (never a silent pass)', async () => {
    const run = requireInteractive();
    const result = await run(['gallery-real', '--assert-interactive', '--bogus-flag'], {
      deps: okDeps(),
      emit: () => {},
    });
    assert.equal(result.exitCode, 2, 'unknown flags must exit 2');
    assert.ok(/bogus-flag|usage/i.test(`${result.stderr || ''} ${result.usage || ''}`),
      'usage failure must name the unknown flag');
  });

  for (const check of ['tap', 'focus', 'caret-selection', 'scroll', 'switch-scrub',
    'dialog-dismiss', 'sheet-dismiss', 'menu-dismiss', 'state-binding', 'shell-paging']) {
    it(`failing '${check}' -> exit 1 naming the check`, async () => {
      const run = requireInteractive();
      const result = await run(['gallery-real', '--assert-interactive'], {
        deps: okDeps({ fail: [check] }),
        emit: () => {},
      });
      assert.equal(result.exitCode, 1, `failing '${check}' must exit 1`);
      const row = result.rows.find((r) => r.check === check);
      assert.ok(row, `must report a row for '${check}'`);
      assert.equal(row.pass, false, `'${check}' row must fail`);
      const names = `${result.code || ''} ${result.detail || ''} ${result.stderr || ''}`;
      assert.ok(new RegExp(check.replace(/[-[\]{}()*+?.\\^$|]/g, '\\$&'), 'i').test(names),
        `exit-1 failure must name the failing check '${check}'`);
    });
  }

  it("no-diagram failure names the reachable mock-painter symbol", async () => {
    const run = requireInteractive();
    const result = await run(['gallery-real', '--assert-interactive'], {
      deps: okDeps({ fail: ['no-diagram'], mockSymbols: ['MockPainter'] }),
      emit: () => {},
    });
    assert.equal(result.exitCode, 1, 'no-diagram failure must exit 1');
    const row = result.rows.find((r) => r.check === 'no-diagram');
    assert.ok(row, 'must report a no-diagram row');
    assert.equal(row.pass, false, 'no-diagram row must fail');
    const names = `${result.code || ''} ${result.detail || ''} ${result.stderr || ''} ${(result.symbols || []).join(' ')}`;
    assert.ok(/MockPainter|PaintCaption|mock/i.test(names),
      'no-diagram failure must name the reachable mock-painter symbol');
  });

  it('device-less run fails closed (exit 1, never a silent pass)', async () => {
    const run = requireInteractive();
    await assert.rejects(
      run(['gallery-real', '--assert-interactive'], { emit: () => {} }),
      (err) => {
        assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry a named code');
        assert.ok(/NO-DEVICE-RUNNER|device/i.test(`${err.code} ${err.detail || err.message || ''}`),
          'device-less failure must name the missing device runner');
        return true;
      },
    );
  });
});

describe('T2 — gallery-real CLI exit contract (TC-M3-027/TC-GRAPHICS-045 Check)', () => {
  it('gallery-real --assert-interactive --help lists --assert-interactive', () => {
    const res = runCli(['gallery-real', '--assert-interactive', '--help']);
    assert.equal(res.status, 0, `help must exit 0, got ${res.status}`);
    assert.ok(/assert-interactive/.test(`${res.stdout}\n${res.stderr}`),
      'help text must list the --assert-interactive flag');
  });

  it('unknown flags exit 2 with usage naming the flag', () => {
    const res = runCli(['gallery-real', '--assert-interactive', '--bogus-flag']);
    assert.equal(res.status, 2, `unknown flags must exit 2, got ${res.status}`);
    assert.ok(/bogus-flag|Usage/i.test(`${res.stdout}\n${res.stderr}`),
      'stderr must name the unknown flag with usage');
  });

  it('device-less run exits 1 fail-closed naming the cause (never a silent pass)', () => {
    const res = runCli(['gallery-real', '--assert-interactive'], {
      env: { ...process.env, ANDROID_SERIAL: '' },
    });
    assert.equal(res.status, 1, `device-less run must exit 1, got ${res.status}`);
    assert.ok(/NO-DEVICE-RUNNER|device|FAIL/i.test(`${res.stdout}\n${res.stderr}`),
      'device-less failure must name the cause');
  });
});
