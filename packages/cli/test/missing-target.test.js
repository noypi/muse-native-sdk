'use strict';
// TC-WORKFLOW-007/008/009 (T2: FR-WORKFLOW-003 AC-WORKFLOW-007/008/009).
// T2 scope: accept new flags + positional sample target; missing target fails
// at configure. Binds ONLY to the pinned T2 `Interface:` lines, never to
// implementation internals.
// Hermetic: node:test + node:assert/strict + stdlib only. `validateOptions`
// runs in-process; `runBuild` missing-target cases run in-process against
// fs.mkdtemp dirs (guaranteed-absent target paths) plus one local spawn of
// `bin/muse-native.js` with per-spawn env (process.env never mutated); no
// network, no secrets, no live services, no dist/ writes asserted absent.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T2a: `validateOptions(argv: string[])` is exported from
//   `packages/cli/lib/cli.js`; it throws `OptionError { option, accepted }`
//   naming the option and the accepted values for unknown/invalid/`--flag=value`.
// - A-T2b: the accepted set gains `--with-sample, --abi, --release,
//   --modules, --output, --strict` (space form `--<name> <value>` for value
//   flags, bare flag for booleans) plus one positional target operand
//   `examples/m3-gallery` (positionals are skipped by option validation).
// - A-T2c: `runBuild(appDir: string, rest: string[])` is exported from
//   `packages/cli/lib/build.js`; a missing/unknown target writes
//   `Error: <target> ... <expected path>` to stderr and sets the config-error
//   exit code (2, via `getExitCode('config')`) before any compilation or
//   `dist/` write.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const { validateOptions, OptionError } = require('../lib/cli.js');
const runBuild = require('../lib/build.js');
const { getExitCode } = require('../lib/exit-codes.js');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}

// Capture stderr writes + process.exitCode around a synchronous runBuild call.
let stderrChunks;
let origWrite;
let origExitCode;
function captureStart() {
  stderrChunks = [];
  origWrite = process.stderr.write;
  origExitCode = process.exitCode;
  process.exitCode = undefined;
  process.stderr.write = (chunk, enc, cb) => {
    stderrChunks.push(String(chunk));
    if (typeof enc === 'function') enc();
    else if (typeof cb === 'function') cb();
    return true;
  };
}
function captureStop() {
  process.stderr.write = origWrite;
  const code = process.exitCode;
  process.exitCode = origExitCode;
  return { stderr: stderrChunks.join(''), exitCode: code };
}
afterEach(() => {
  if (origWrite && process.stderr.write !== origWrite) process.stderr.write = origWrite;
  process.exitCode = origExitCode;
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

describe('T2 validateOptions — new flags + positional sample target (AC-007/008)', () => {
  it('accepts --with-sample without throwing', () => {
    assert.doesNotThrow(() => validateOptions(['build', '--with-sample']));
  });

  it('accepts --strict without throwing', () => {
    assert.doesNotThrow(() => validateOptions(['build', '--strict']));
  });

  it('accepts --release without throwing', () => {
    assert.doesNotThrow(() => validateOptions(['build', '--release']));
  });

  it('accepts value flags --abi/--modules/--output in space form', () => {
    assert.doesNotThrow(() => validateOptions(['build', '--abi', 'arm64-v8a']));
    assert.doesNotThrow(() => validateOptions(['build', '--modules', 'engine']));
    assert.doesNotThrow(() => validateOptions(['build', '--output', 'out/dir']));
  });

  it('accepts the positional target operand examples/m3-gallery', () => {
    assert.doesNotThrow(() => validateOptions(['build', 'examples/m3-gallery']));
  });

  it('still rejects an unknown option with OptionError naming option + accepted values', () => {
    assert.throws(
      () => validateOptions(['build', '--no-such-flag']),
      (err) => {
        assert.equal(err.name, 'OptionError');
        assert.ok(String(err.option).includes('--no-such-flag'));
        assert.ok(typeof err.accepted === 'string' && err.accepted.length > 0);
        return true;
      },
    );
  });

  it('still rejects --flag=value form with OptionError', () => {
    assert.throws(
      () => validateOptions(['build', '--abi=arm64-v8a']),
      (err) => err && err.name === 'OptionError' && String(err.option).includes('--abi=arm64-v8a'),
    );
  });

  it('still rejects an invalid --stage value with OptionError', () => {
    assert.throws(
      () => validateOptions(['build', '--stage', 'bogus-stage']),
      (err) => err && err.name === 'OptionError' && String(err.option).includes('--stage'),
    );
  });
});

describe('T2 runBuild — missing/unknown target fails closed at configure (AC-009)', () => {
  it('missing target writes Error: <target> ... <expected path> with config-error exit, no dist/ writes', async () => {
    const parent = mkTmp('muse-t2-missing-');
    const target = path.join(parent, 'no-such-target');
    captureStart();
    try {
      runBuild(target, []);
    } finally {
      await new Promise((resolve) => setImmediate(resolve));
    }
    const { stderr, exitCode } = captureStop();
    assert.equal(exitCode, getExitCode('config'), `missing target must set config-error exit, got ${exitCode}: ${stderr}`);
    assert.match(stderr, /Error:.*no-such-target/, 'stderr must name the missing target');
    assert.match(stderr, /[Ee]xpected/, 'stderr must state the expected path');
    assert.ok(!fs.existsSync(path.join(target, 'dist')), 'must not write dist/ for a missing target');
    assert.ok(!fs.existsSync(path.join(target, 'dist', 'ir.json')), 'must not compile for a missing target');
  });

  it('CLI: muse-native build <absent-target> exits 2 naming target + expected path, no dist/ writes', () => {
    const parent = mkTmp('muse-t2-missing-cli-');
    const target = path.join(parent, 'no-such-target');
    const r = spawnSync(process.execPath, [BIN_NATIVE, 'build', target], { encoding: 'utf8' });
    assert.equal(r.status, getExitCode('config'), `absent target must exit 2, got ${r.status}: ${r.stderr}`);
    assert.match(r.stderr, /no-such-target/, 'stderr must name the missing target');
    assert.match(r.stderr, /[Ee]xpected/, 'stderr must state the expected path');
    assert.ok(!fs.existsSync(path.join(target, 'dist')), 'must not write dist/ for a missing target');
  });
});
