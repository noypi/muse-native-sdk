'use strict';
// TC-WORKFLOW-011 (T6: FR-WORKFLOW-004, AC-WORKFLOW-011, AC-WORKFLOW-012) — Batch 3.
// Unknown options, invalid values and incompatible combinations are rejected
// at `configure` (before any compilation) with distinct exit codes:
// 0 ok, 2 config/usage error, 1 compile/stage error, 3 packaging error.
// The packaging path is stubbed — tests assert the `getExitCode` mapping
// function, not an end-to-end packaging failure.
// Hermetic: node:test + node:assert/strict + stdlib only. Unit-level calls to
// `validateOptions` / `getExitCode` run in-process; CLI rejection cases spawn
// `node bin/muse-native.js` against fs.mkdtemp dirs; env passed per-spawn
// (never mutates process.env); no network, no secrets, no live services.
// Scope: the OPTION layer only (`validateOptions`/`getExitCode` + CLI option
// rejection). Unknown CONFIG keys already fail via ConfigError (T1/T2) and
// `--stage` passthrough already exists (T3) — those are not re-tested here.
// Out-of-scope flags rejected as unknown options per plan A3: `--with-sample`,
// `--strict`, `--abi`, `--release`, `--modules`, `--output`, `--archs`,
// `--packaging`.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T6a: `validateOptions(argv: string[])` is exported from
//   `packages/cli/lib/cli.js`; it throws `OptionError { option, accepted }`
//   naming the option and the accepted values.
// - A-T6b: `getExitCode(kind)` is exported from
//   `packages/cli/lib/exit-codes.js` with kinds ok/config/compile/package.
// - A-T6c: the incompatible-combination case is `--release` with a debug-only
//   stage (plan example); the accepted-values message names the conflict.
// - A-T6d: CLI option errors exit 2 and write
//   `Error: <option> ... Expected ...` to stderr before any compilation
//   (no `dist/` artefacts).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    const dir = tmpRoots.pop();
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function writeApp(appDir, yamlText) {
  fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), yamlText, 'utf8');
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(
    path.join(appDir, 'src', 'App.tsx'),
    "import { View } from '@muse/native';\nexport default function App() { return (<View />); }\n",
    'utf8',
  );
}

describe('TC-WORKFLOW-011 — option validation and exit codes', () => {
  it('getExitCode maps ok/config/compile/package to 0/2/1/3', () => {
    const { getExitCode } = require('../lib/exit-codes.js');
    assert.equal(typeof getExitCode, 'function', 'exit-codes.js must export getExitCode()');
    assert.equal(getExitCode('ok'), 0, "kind 'ok' must map to 0");
    assert.equal(getExitCode('config'), 2, "kind 'config' must map to 2");
    assert.equal(getExitCode('compile'), 1, "kind 'compile' must map to 1");
    assert.equal(getExitCode('package'), 3, "kind 'package' must map to 3");
  });

  it('validateOptions accepts a known option set without throwing', () => {
    const cli = require('../lib/cli.js');
    assert.equal(typeof cli.validateOptions, 'function', 'cli.js must export validateOptions()');
    assert.doesNotThrow(() => cli.validateOptions(['build', '--stage', 'typecheck']));
  });

  it('validateOptions throws OptionError naming option and accepted values for an unknown option', () => {
    const cli = require('../lib/cli.js');
    assert.throws(
      () => cli.validateOptions(['build', '--with-sample']),
      (err) => {
        assert.equal(err.name, 'OptionError', 'must throw OptionError');
        assert.ok(String(err.option).includes('--with-sample'), 'OptionError must name the option');
        assert.ok(typeof err.accepted === 'string' && err.accepted.length > 0, 'OptionError must carry accepted values');
        return true;
      },
      'unknown option must throw OptionError',
    );
  });

  it('validateOptions throws OptionError for an out-of-scope severity flag', () => {
    const cli = require('../lib/cli.js');
    assert.throws(
      () => cli.validateOptions(['build', '--strict']),
      (err) => err && err.name === 'OptionError' && String(err.option).includes('--strict'),
      '--strict must be rejected as an unknown option',
    );
  });

  it('validateOptions throws OptionError for an invalid enum value', () => {
    const cli = require('../lib/cli.js');
    assert.throws(
      () => cli.validateOptions(['build', '--stage', 'bogus-stage']),
      (err) => err && err.name === 'OptionError' && String(err.option).includes('--stage'),
      'invalid --stage value must throw OptionError naming the option',
    );
  });

  it('validateOptions throws OptionError for an incompatible combination (--release with a debug-only stage)', () => {
    const cli = require('../lib/cli.js');
    assert.throws(
      () => cli.validateOptions(['build', '--release', '--stage', 'typecheck']),
      (err) => err && err.name === 'OptionError' && typeof err.accepted === 'string' && err.accepted.length > 0,
      'incompatible combination must throw OptionError with accepted values',
    );
  });

  it('CLI: unknown option fails at configure with exit 2 naming option and accepted values, no dist/ writes', () => {
    const appDir = mkTmp('muse-invalidopt-');
    writeApp(appDir, 'name: good-app\nversion: 0.0.1\n');
    const r = runCli(['build', appDir, '--with-sample']);
    assert.equal(r.status, 2, `unknown option must exit 2, got ${r.status}: ${r.stderr}`);
    assert.match(r.stderr, /--with-sample/, 'stderr must name the rejected option');
    assert.match(r.stderr, /[Ee]xpected/, 'stderr must state the accepted values');
    assert.ok(!fs.existsSync(path.join(appDir, 'dist', 'ir.json')), 'must not compile: no ir.json on option error');
  });

  it('CLI: invalid --stage value fails with exit 2 naming the option', () => {
    const appDir = mkTmp('muse-invalidopt-');
    writeApp(appDir, 'name: good-app\nversion: 0.0.1\n');
    const r = runCli(['build', appDir, '--stage', 'bogus-stage']);
    assert.equal(r.status, 2, `invalid value must exit 2, got ${r.status}: ${r.stderr}`);
    assert.match(r.stderr, /--stage/, 'stderr must name the option');
    assert.ok(!fs.existsSync(path.join(appDir, 'dist', 'ir.json')), 'must not compile on invalid value');
  });

  it('CLI: incompatible combination fails at configure with exit 2 before compilation', () => {
    const appDir = mkTmp('muse-invalidopt-');
    writeApp(appDir, 'name: good-app\nversion: 0.0.1\n');
    const r = runCli(['build', appDir, '--release', '--stage', 'typecheck']);
    assert.equal(r.status, 2, `incompatible combination must exit 2, got ${r.status}: ${r.stderr}`);
    assert.ok(r.stderr.length > 0, 'must print an error to stderr');
    assert.ok(!fs.existsSync(path.join(appDir, 'dist', 'ir.json')), 'must not compile on incompatible combination');
  });

  it('CLI: valid options still build with exit 0 (control, not a re-test of T3)', () => {
    const appDir = mkTmp('muse-invalidopt-');
    writeApp(appDir, 'name: good-app\nversion: 0.0.1\n');
    const r = runCli(['build', appDir, '--stage', 'typecheck']);
    assert.equal(r.status, 0, `valid options must exit 0, got ${r.status}: ${r.stderr}`);
  });
});
