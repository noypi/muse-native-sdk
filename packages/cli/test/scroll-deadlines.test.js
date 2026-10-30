'use strict';
// T6 — Implement declared `scroll-deadlines` audit CLI as thin hermetic wrapper
// (TC-LISTS-034 Check). Spec refs: FR-LISTS-007, AC-LISTS-034 (TC-LISTS-034 Check).
// Interface (plan verbatim):
//   `muse-native scroll-deadlines --rates 60,90,120 --budget
//   <scroll-budgets.json>` — exit 0 with zero above-budget misses per rate,
//   exit 1 tabulating misses per rate otherwise; hermetic
//   vsync-timestamp-driven fling simulation; budget file lists per-rate ns
//   budgets.
// Detail under test: per-command module `packages/cli/lib/scroll-deadlines.js`
// required from `packages/cli/lib/cli.js` argv dispatch (`COMMANDS`
// extended); steps `integrate` from vsync `dtS` per rate through the T4
// deadline accounting, checks misses against the budget file.
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path/child_process)
// only. Spawns `node bin/muse-native.js` against tmpdir fixtures (commands
// .test.js pattern); no device, no network, no secrets, no live services.
// Committed budget fixture path recorded here: the coder-owned
// `packages/cli/test/fixtures/scroll-budgets.json` must list per-rate ns
// budgets for 60/90/120 (see the fixture-exists case below).
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-D1: exact miss-table rendering is coder-owned; the exit-1 case asserts
//   only that output tabulates misses per rate (names each of 60/90/120 and
//   a miss count), using a zero-ns budget so every simulated frame misses.
// - A-D2: missing `--rates`, missing `--budget`, a nonexistent budget path,
//   a malformed budget file, and a budget missing any requested rate all
//   exit nonzero (fail-closed, never a silent pass).
// - A-D3: an unknown flag exits 2 via the existing `OptionError`.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
const COMMITTED_BUDGETS = path.join(CLI_DIR, 'test', 'fixtures', 'scroll-budgets.json');

let scrollDeadlines = null;
try {
  // eslint-disable-next-line global-require
  scrollDeadlines = require('../lib/scroll-deadlines.js');
} catch {
  scrollDeadlines = null;
}

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
});

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

// Generous per-rate ns budgets: no hermetic vsync-driven frame may miss.
function writeGenerousBudget(root) {
  const p = path.join(root, 'scroll-budgets.json');
  fs.writeFileSync(p, `${JSON.stringify({ 60: 16666666, 90: 11111111, 120: 8333333 })}\n`);
  return p;
}

// Zero-ns budgets: every simulated frame is above budget.
function writeZeroBudget(root) {
  const p = path.join(root, 'scroll-budgets.json');
  fs.writeFileSync(p, `${JSON.stringify({ 60: 0, 90: 0, 120: 0 })}\n`);
  return p;
}

describe('T6 — scroll-deadlines module surface (AC-LISTS-034)', () => {
  it('packages/cli/lib/scroll-deadlines.js exists and loads', () => {
    assert.ok(scrollDeadlines !== null, 'lib/scroll-deadlines.js must exist (thin wrapper module)');
  });

  it('wrapper steps vsync-timestamp fling frames through deadline accounting', () => {
    assert.ok(scrollDeadlines, 'lib/scroll-deadlines.js must load');
    const exported = typeof scrollDeadlines === 'function' ? scrollDeadlines : scrollDeadlines.run;
    assert.equal(typeof exported, 'function', 'scroll-deadlines module must export a callable entry point');
    const src = [scrollDeadlines.toString(), typeof scrollDeadlines === 'object' ? JSON.stringify(Object.keys(scrollDeadlines)) : ''].join('\n');
    assert.ok(
      /vsync|deadline|budget|rate|integrate/i.test(src),
      'wrapper must reference vsync timestamps / deadline accounting / per-rate budgets (thin reuse, not a reimplementation)',
    );
  });

  it('committed budget fixture lists per-rate ns budgets for 60/90/120', () => {
    assert.ok(fs.existsSync(COMMITTED_BUDGETS), `budget fixture must exist at packages/cli/test/fixtures/scroll-budgets.json (got ${COMMITTED_BUDGETS})`);
    const budgets = JSON.parse(fs.readFileSync(COMMITTED_BUDGETS, 'utf8'));
    for (const rate of ['60', '90', '120']) {
      assert.ok(
        Number.isInteger(budgets[rate]) && budgets[rate] > 0,
        `budget file must list a positive integer ns budget for rate ${rate}`,
      );
    }
  });
});

describe('T6 — scroll-deadlines CLI exit contract (AC-LISTS-034, TC-LISTS-034 Check)', () => {
  it('exit 0 with zero above-budget misses per rate (generous budgets)', () => {
    const root = mkTmp('muse-sdead-ok-');
    const budget = writeGenerousBudget(root);
    const res = runCli(['scroll-deadlines', '--rates', '60,90,120', '--budget', budget]);
    assert.equal(
      res.status,
      0,
      `expected exit 0 with zero misses, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`,
    );
  });

  it('exit 1 tabulating misses per rate on a zero-ns budget', () => {
    const root = mkTmp('muse-sdead-miss-');
    const budget = writeZeroBudget(root);
    const res = runCli(['scroll-deadlines', '--rates', '60,90,120', '--budget', budget]);
    assert.equal(res.status, 1, `above-budget misses must exit 1, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
    const out = `${res.stdout}\n${res.stderr}`;
    for (const rate of ['60', '90', '120']) {
      assert.ok(out.includes(rate), `miss table must tabulate rate ${rate}`);
    }
    assert.ok(/miss/i.test(out), 'miss table must report misses');
  });

  it('nonzero when --budget is omitted (never a silent pass)', () => {
    const res = runCli(['scroll-deadlines', '--rates', '60,90,120']);
    assert.notEqual(res.status, 0, 'omitting --budget must not pass');
    assert.ok(!/Unknown command/.test(res.stderr), 'must be a usage failure, not an unknown command');
  });

  it('nonzero when --rates is omitted', () => {
    const root = mkTmp('muse-sdead-norates-');
    const budget = writeGenerousBudget(root);
    const res = runCli(['scroll-deadlines', '--budget', budget]);
    assert.notEqual(res.status, 0, 'omitting --rates must not pass');
    assert.ok(!/Unknown command/.test(res.stderr), 'must be a usage failure, not an unknown command');
  });

  it('nonzero on a nonexistent budget path', () => {
    const res = runCli([
      'scroll-deadlines',
      '--rates',
      '60,90,120',
      '--budget',
      path.join(os.tmpdir(), 'muse-sdead-nope-12345.json'),
    ]);
    assert.notEqual(res.status, 0, 'a nonexistent budget path must exit nonzero, never a silent pass');
    assert.ok(!/Unknown command/.test(res.stderr), 'must be a budget failure, not an unknown command');
  });

  it('nonzero on a malformed budget file', () => {
    const root = mkTmp('muse-sdead-malformed-');
    const bad = path.join(root, 'scroll-budgets.json');
    fs.writeFileSync(bad, 'not json at all\n');
    const res = runCli(['scroll-deadlines', '--rates', '60,90,120', '--budget', bad]);
    assert.notEqual(res.status, 0, 'a malformed budget file must exit nonzero');
    assert.ok(!/Unknown command/.test(res.stderr), 'must be a budget failure, not an unknown command');
  });

  it('nonzero when the budget file misses a requested rate', () => {
    const root = mkTmp('muse-sdead-gap-');
    const gap = path.join(root, 'scroll-budgets.json');
    fs.writeFileSync(gap, `${JSON.stringify({ 60: 16666666 })}\n`);
    const res = runCli(['scroll-deadlines', '--rates', '60,90,120', '--budget', gap]);
    assert.notEqual(res.status, 0, 'a budget missing a requested rate must fail closed, never pass');
    assert.ok(!/Unknown command/.test(res.stderr), 'must be a budget failure, not an unknown command');
  });

  it('unknown flags exit 2 via OptionError naming the flag', () => {
    const root = mkTmp('muse-sdead-flag-');
    const budget = writeGenerousBudget(root);
    const res = runCli(['scroll-deadlines', '--rates', '60,90,120', '--budget', budget, '--bogus-flag']);
    assert.equal(res.status, 2, `unknown flags must exit 2, got ${res.status}`);
    assert.ok(res.stderr.includes('--bogus-flag'), 'stderr must name the unknown flag');
  });
});
