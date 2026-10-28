'use strict';
// TC-SAMPLE-015 (AC-SAMPLE-015, FR-SAMPLE-006) — Batch 1 (T2).
// `muse-native init` refuses to write into a non-empty directory unless
// `--force` is passed, and prints the exact commands to run next.
// Hermetic: node:test + node:assert/strict + stdlib only. Drives the real
// CLI via spawnSync into fresh os.tmpdir() trees; no network, no secrets,
// no live services, no repo writes. Style mirrors test/help.test.js.
// T2 Interface (plan verbatim):
//   `runInit(dir: string, opts?: { force?: boolean }): void` — non-empty
//   `dir` without `force:true` writes `Error: directory '<dir>' already
//   exists and is not empty.` to stderr, writes nothing, exits `1`; with
//   `--force` proceeds; on success stdout prints exact next commands
//   (`cd <dir>`, build, run).
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T2a: the public boundary is the CLI: `--force` is passed as
//   `muse-native init <dir> --force`; the `opts?: { force?: boolean }`
//   shape is the in-process spelling of the same flag.
// - A-T2b: "writes nothing" means the pre-existing directory entries are
//   byte-identical after the refusal (snapshot before/after) and no
//   scaffold file appears.
// - A-T2c: "exact next commands" are asserted as stdout lines naming
//   `cd <dir>`, a `muse-native build` command and a `muse-native run`
//   command; exact line layout beyond those three commands is not pinned.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

let scratch = null;

beforeEach(() => {
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-init-guard-'));
});

afterEach(() => {
  if (scratch) {
    fs.rmSync(scratch, { recursive: true, force: true });
    scratch = null;
  }
});

function runInit(args) {
  return spawnSync(process.execPath, [BIN_NATIVE, 'init', ...args], { encoding: 'utf8' });
}

function snapshot(dir) {
  return walk(dir).map((f) => [path.relative(dir, f), fs.readFileSync(f, 'utf8')]);
}

function walk(dir) {
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...walk(full));
    else out.push(full);
  }
  return out;
}

describe('TC-SAMPLE-015 — init guard refuses non-empty dir without --force (AC-SAMPLE-015)', () => {
  it("refuses a non-empty dir: exit 1 with the exact stderr line", () => {
    const dir = path.join(scratch, 'taken');
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'keep.txt'), 'precious\n');
    const r = runInit([dir]);
    assert.equal(r.status, 1, `guard refusal must exit 1, got ${r.status}: ${r.stdout}`);
    assert.equal(
      r.stderr,
      `Error: directory '${dir}' already exists and is not empty.\n`,
      `refusal stderr must be the exact guard line, got: ${JSON.stringify(r.stderr)}`,
    );
  });

  it('refusal writes nothing: pre-existing entries are untouched', () => {
    const dir = path.join(scratch, 'taken');
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'keep.txt'), 'precious\n');
    const before = snapshot(dir);
    const r = runInit([dir]);
    assert.equal(r.status, 1, `guard refusal must exit 1, got ${r.status}`);
    assert.deepEqual(snapshot(dir), before, 'refusal must not write anything into the directory');
    for (const scaffold of ['muse-native.yaml', 'package.json', 'tsconfig.json', 'README.md']) {
      assert.ok(!fs.existsSync(path.join(dir, scaffold)), `refusal must not create ${scaffold}`);
    }
    assert.ok(!fs.existsSync(path.join(dir, 'src')), 'refusal must not create src/');
  });

  it('--force proceeds into a non-empty dir and scaffolds', () => {
    const dir = path.join(scratch, 'taken');
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'keep.txt'), 'precious\n');
    const r = runInit([dir, '--force']);
    assert.equal(r.status, 0, `--force must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(fs.existsSync(path.join(dir, 'muse-native.yaml')), '--force must scaffold muse-native.yaml');
    assert.ok(fs.existsSync(path.join(dir, 'package.json')), '--force must scaffold package.json');
    assert.ok(fs.existsSync(path.join(dir, 'tsconfig.json')), '--force must scaffold tsconfig.json');
    assert.ok(fs.existsSync(path.join(dir, 'src', 'App.tsx')), '--force must scaffold src/App.tsx');
    assert.ok(fs.existsSync(path.join(dir, 'README.md')), '--force must scaffold README.md');
    assert.equal(
      fs.readFileSync(path.join(dir, 'keep.txt'), 'utf8'),
      'precious\n',
      '--force must preserve the pre-existing file',
    );
  });

  it('success stdout prints the exact next commands (cd, build, run)', () => {
    const dir = path.join(scratch, 'my-app');
    const r = runInit([dir]);
    assert.equal(r.status, 0, `init must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(
      r.stdout.includes(`cd ${dir}`),
      `success output must name \`cd ${dir}\`, got:\n${r.stdout}`,
    );
    assert.ok(
      r.stdout.includes('muse-native build'),
      `success output must name the build next command, got:\n${r.stdout}`,
    );
    assert.ok(
      r.stdout.includes('muse-native run'),
      `success output must name the run next command, got:\n${r.stdout}`,
    );
  });

  it('empty existing dir still scaffolds without --force (control)', () => {
    const dir = path.join(scratch, 'empty');
    fs.mkdirSync(dir, { recursive: true });
    const r = runInit([dir]);
    assert.equal(r.status, 0, `empty dir must scaffold with exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(fs.existsSync(path.join(dir, 'muse-native.yaml')), 'empty dir must gain muse-native.yaml');
  });
});
