'use strict';
// TC-SAMPLE-013 (AC-SAMPLE-013, FR-SAMPLE-006) — Batch 1 (T1).
// `muse-native init <dir>` scaffolds a complete TS/TSX project that builds
// and runs with the documented commands and no manual edit first.
// Hermetic: node:test + node:assert/strict + stdlib only. Drives the real
// CLI via spawnSync into fresh os.tmpdir() trees; no network, no secrets,
// no live services, no repo writes. Style mirrors test/help.test.js.
// T1 Interface (plan verbatim):
//   `runInit(dir: string, opts?: { force?: boolean }): void` — copies template
//   tree (muse-native.yaml, package.json with four fixed `@muse/*` deps,
//   tsconfig.json with `jsxImportSource: "@muse/jsx"`, src/App.tsx, README
//   with next commands) into `dir`; exits `2` usage on missing operand, `1`
//   on guard refusal, non-zero with named path on copy failure. Supported
//   `muse` spelling aliases to same path; NO `capo` spelling exists
//   (HIST-0290 / RULE-SAMPLE-004 v2.17: canonical `muse-native` + `muse`
//   alias only).
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T1a: the public boundary is the CLI (`muse-native init <dir>`); the
//   `runInit` export shape is asserted, exit codes via the CLI.
// - A-T1b: the `muse` alias is `bin/muse.js`; per HIST-0290 there is NO
//   `capo` alias — `bin/capo.js` must be absent and the `capo` spelling
//   must not resolve (fails closed / unresolvable). Red until the coder
//   removes the capo surface.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
const BIN_MUSE = path.join(CLI_DIR, 'bin', 'muse.js');
const BIN_CAPO = path.join(CLI_DIR, 'bin', 'capo.js');

// Full template tree T1 must copy (relative to the scaffold root).
const EXPECTED_TREE = [
  'muse-native.yaml',
  'package.json',
  'tsconfig.json',
  path.join('src', 'App.tsx'),
  'README.md',
];

let scratch = null;

beforeEach(() => {
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-init-013-'));
});

afterEach(() => {
  if (scratch) {
    fs.rmSync(scratch, { recursive: true, force: true });
    scratch = null;
  }
});

function runCli(bin, args) {
  return spawnSync(process.execPath, [bin, ...args], { encoding: 'utf8' });
}

describe('TC-SAMPLE-013 — init scaffolds a complete project (AC-SAMPLE-013)', () => {
  it('lib/init.js exports runInit as its module function', () => {
    const runInit = require('../lib/init.js');
    assert.equal(typeof runInit, 'function', 'lib/init.js must export runInit(dir, opts?) as a function');
  });

  it('init <dir> exits 0 and copies the full template tree', () => {
    const dir = path.join(scratch, 'my-app');
    const r = runCli(BIN_NATIVE, ['init', dir]);
    assert.equal(r.status, 0, `init must exit 0, got ${r.status}: ${r.stderr}`);
    for (const rel of EXPECTED_TREE) {
      assert.ok(
        fs.existsSync(path.join(dir, rel)),
        `scaffolded project must contain ${rel}`,
      );
    }
  });

  it('scaffolded tree matches the templates/ source tree file-for-file', () => {
    const dir = path.join(scratch, 'my-app');
    const r = runCli(BIN_NATIVE, ['init', dir]);
    assert.equal(r.status, 0, `init must exit 0, got ${r.status}: ${r.stderr}`);
    const templatesDir = path.join(CLI_DIR, 'templates');
    for (const rel of EXPECTED_TREE) {
      const want = fs.readFileSync(path.join(templatesDir, rel), 'utf8');
      const got = fs.readFileSync(path.join(dir, rel), 'utf8');
      assert.equal(got, want, `scaffolded ${rel} must equal templates/${rel}`);
    }
  });

  it('init with no <dir> operand is usage error with exit 2', () => {
    const r = runCli(BIN_NATIVE, ['init']);
    assert.equal(r.status, 2, `missing operand must exit 2, got ${r.status}: ${r.stdout}`);
    assert.ok(
      r.stderr.includes('Usage: muse-native init <dir>'),
      `missing operand must print usage to stderr, got: ${r.stderr}`,
    );
  });

  it('copy failure exits non-zero and names the path', () => {
    // A regular file where the scaffold root must go: mkdir fails, and the
    // failure must name the path. (dir exists as non-dir -> guard refusal
    // also names the path with exit 1; either way the path is named.)
    const blocker = path.join(scratch, 'blocker');
    fs.writeFileSync(blocker, 'x');
    const r = runCli(BIN_NATIVE, ['init', blocker]);
    assert.notEqual(r.status, 0, 'copy/guard failure must exit non-zero');
    assert.ok(
      r.stderr.includes(blocker),
      `failure must name the path '${blocker}', got: ${r.stderr}`,
    );
  });

  it('supported `muse` spelling aliases to the same scaffold path', () => {
    const dir = path.join(scratch, 'alias-app');
    const r = runCli(BIN_MUSE, ['init', dir]);
    assert.equal(r.status, 0, `muse init must exit 0, got ${r.status}: ${r.stderr}`);
    for (const rel of EXPECTED_TREE) {
      assert.ok(
        fs.existsSync(path.join(dir, rel)),
        `muse-alias scaffold must contain ${rel}`,
      );
    }
  });

  it('`capo` bin is absent (HIST-0290: no capo spelling)', () => {
    assert.ok(
      !fs.existsSync(BIN_CAPO),
      'bin/capo.js must NOT exist (canonical muse-native + muse alias only)',
    );
  });

  it('`capo` spelling is not resolved (fails closed)', () => {
    // The capo bin must be unresolvable: invoking it via node fails closed
    // (non-zero, ENOENT-style error) and scaffolds nothing.
    const dir = path.join(scratch, 'capo-app');
    const r = runCli(BIN_CAPO, ['init', dir]);
    assert.notEqual(r.status, 0, 'capo init must fail closed (non-zero exit)');
    assert.ok(
      !fs.existsSync(path.join(dir, 'muse-native.yaml')),
      'capo invocation must not scaffold a project',
    );
  });
});
