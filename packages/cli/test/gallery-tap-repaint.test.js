'use strict';
// T8 — `gallery-tap-repaint` CLI, hermetic-first, optional `--serial`
// device lane (TC-INPUT-021 Check). Spec refs: TC-INPUT-021 (Check
// executability only; no AC/requirement change, no weakening).
// Interface (plan T8 verbatim):
//   `muse-native gallery-tap-repaint --assert-one-frame --assert-no-reinstantiation [--serial <serial>]`
//   — exit 0 iff every asserted check holds, 1 with per-check tabulation
//   otherwise, 2 on usage error.
// Detail under test: per-command module
// `packages/cli/lib/gallery-tap-repaint.js` required from
// `packages/cli/lib/cli.js` argv dispatch (`COMMANDS` extended); hermetic
// lane (default, no `--serial`) shells the registered engine suites from the
// existing `build/host` tree via
// `ctest --test-dir build/host -R 'tap_handler_repaint|touch_dispatch'`;
// missing/unbuilt `build/host` fails closed (exit 1 naming the missing
// build). Device lane (only with `--serial`) shells the exact T7 adb
// sequence against that serial and tabulates TC-INPUT-021 from device
// observables; unreachable device fails closed with exit 1. Never
// re-implements T3–T6 engine behavior in JS.
// Hermetic: node:test + node:assert/strict + stdlib only. `ctest`/`adb` are
// stubbed via a tmp bin dir prepended to PATH — the real suite and real adb
// are never run from these unit tests. Each flag maps 1:1 to a T5/T6
// hermetic case in the default lane.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-R1: a fixture code root stands in for the checkout: tests run the CLI
//   with `cwd` pointed at a tmp root, and `build/host` is resolved under
//   that root. A root without `build/host/CTestTestfile.cmake` counts as
//   missing/unbuilt. Coder owns the exact probe; reconcile paths, not
//   verdicts, at implementation time.
// - A-R2: dropping any `--assert-*` flag flips its row to fail (exit 1),
//   while an unknown flag or a missing `--serial` value is a usage error
//   (exit 2).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

let galleryTapRepaint = null;
try {
  // eslint-disable-next-line global-require
  galleryTapRepaint = require('../lib/gallery-tap-repaint.js');
} catch {
  galleryTapRepaint = null;
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

// Fixture code root with a built `build/host` tree marker (stands in for the
// post-T5/T6 cmake build; never the real checkout tree).
function writeBuiltHostFixture(root) {
  const hostDir = path.join(root, 'build', 'host');
  fs.mkdirSync(hostDir, { recursive: true });
  fs.writeFileSync(path.join(hostDir, 'CTestTestfile.cmake'), '# built-host fixture\n');
}

// Stub executables via PATH: `stubs` maps name -> { exit, out, record }.
// Records argv to <bin>/<name>.args when `record` is true. Real ctest/adb
// are never executed: the tmp bin dir shadows them on PATH.
function writeStubBin(stubs) {
  const bin = mkTmp('muse-grepaint-bin-');
  for (const [name, spec] of Object.entries(stubs)) {
    const target = path.join(bin, name);
    const lines = ['#!/bin/sh'];
    if (spec.record) lines.push(`printf '%s\\n' "$@" > "${target}.args"`);
    if (spec.touch) lines.push(`touch "${spec.touch}"`);
    if (spec.out) lines.push(`printf '%s\\n' ${JSON.stringify(spec.out)}`);
    lines.push(`exit ${spec.exit === undefined ? 0 : spec.exit}`);
    fs.writeFileSync(target, `${lines.join('\n')}\n`, { mode: 0o755 });
  }
  return bin;
}

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function withPath(bin, options) {
  const env = { ...process.env, PATH: `${bin}${path.delimiter}${process.env.PATH}` };
  // Force hermetic lane: no ambient serial may leak a device lane.
  delete env.ANDROID_SERIAL;
  return { ...options, env };
}

const REPAINT_ARGS = [
  'gallery-tap-repaint',
  '--assert-one-frame',
  '--assert-no-reinstantiation',
];

describe('T8 — gallery-tap-repaint module surface (TC-INPUT-021 Check)', () => {
  it('packages/cli/lib/gallery-tap-repaint.js exists and loads', () => {
    assert.ok(galleryTapRepaint !== null, 'lib/gallery-tap-repaint.js must exist (thin wrapper module)');
  });

  it('wrapper shells ctest/adb (no re-implemented engine behavior)', () => {
    assert.ok(galleryTapRepaint, 'lib/gallery-tap-repaint.js must load');
    const exported = typeof galleryTapRepaint === 'function' ? galleryTapRepaint : galleryTapRepaint.run;
    assert.equal(typeof exported, 'function', 'gallery-tap-repaint module must export a callable entry point');
    const src = [
      typeof galleryTapRepaint === 'function' ? galleryTapRepaint.toString() : '',
      typeof galleryTapRepaint === 'object' && galleryTapRepaint !== null
        ? JSON.stringify(Object.keys(galleryTapRepaint))
        : '',
    ].join('\n');
    assert.ok(
      /ctest|tap_handler_repaint|touch_dispatch|adb|serial|repaint|vsync/i.test(src),
      'wrapper must shell the engine suites / adb sequence (thin reuse, not a reimplementation)',
    );
  });
});

describe('T8 — gallery-tap-repaint hermetic lane (default, no --serial)', () => {
  it('exit 0 with per-check tabulation against the stubbed built build/host', () => {
    const root = mkTmp('muse-grepaint-root-');
    writeBuiltHostFixture(root);
    const bin = writeStubBin({
      ctest: { exit: 0, out: '100% tests passed', record: true },
      adb: { exit: 99, touch: path.join(root, 'adb-touched') },
    });
    const res = runCli(REPAINT_ARGS, withPath(bin, { cwd: root }));
    assert.equal(
      res.status,
      0,
      `expected exit 0 on stubbed green suites, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`,
    );
    const out = `${res.stdout}\n${res.stderr}`;
    assert.ok(/one-frame|one_frame/i.test(out), 'output must tabulate the one-frame check');
    assert.ok(/no-reinstantiation|reinstantiation/i.test(out), 'output must tabulate the no-reinstantiation check');
  });

  it('shelled ctest targets the tap_handler_repaint|touch_dispatch suites', () => {
    const root = mkTmp('muse-grepaint-root-');
    writeBuiltHostFixture(root);
    const bin = writeStubBin({ ctest: { exit: 0, record: true } });
    const res = runCli(REPAINT_ARGS, withPath(bin, { cwd: root }));
    assert.equal(res.status, 0, `expected exit 0, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
    const argsFile = path.join(bin, 'ctest.args');
    assert.ok(fs.existsSync(argsFile), 'hermetic lane must shell ctest');
    const argv = fs.readFileSync(argsFile, 'utf8');
    assert.ok(/tap_handler_repaint/.test(argv) && /touch_dispatch/.test(argv),
      `ctest must target tap_handler_repaint|touch_dispatch, got: ${argv.trim()}`);
  });

  it('default lane never touches adb', () => {
    const root = mkTmp('muse-grepaint-root-');
    writeBuiltHostFixture(root);
    const marker = path.join(root, 'adb-touched');
    const bin = writeStubBin({
      ctest: { exit: 0, record: true },
      adb: { exit: 0, touch: marker },
    });
    const res = runCli(REPAINT_ARGS, withPath(bin, { cwd: root }));
    assert.equal(res.status, 0, `expected exit 0, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
    assert.ok(!fs.existsSync(marker), 'default lane must never invoke adb');
    assert.ok(!fs.existsSync(path.join(bin, 'adb.args')), 'default lane must never invoke adb');
  });

  it('missing/unbuilt build/host fails closed exit 1 naming the missing build', () => {
    const root = mkTmp('muse-grepaint-root-');
    const bin = writeStubBin({ ctest: { exit: 0, record: true } });
    const res = runCli(REPAINT_ARGS, withPath(bin, { cwd: root }));
    assert.equal(res.status, 1, `missing build/host must exit 1, got ${res.status}`);
    assert.ok(
      /build\/host|missing.*build|unbuilt|not built/i.test(`${res.stdout}\n${res.stderr}`),
      'exit-1 output must name the missing build/host',
    );
    assert.ok(!fs.existsSync(path.join(bin, 'ctest.args')), 'missing build must fail before shelling ctest');
  });

  it('failing stubbed suites exit 1 with per-check tabulation', () => {
    const root = mkTmp('muse-grepaint-root-');
    writeBuiltHostFixture(root);
    const bin = writeStubBin({ ctest: { exit: 1, out: 'Failed', record: true } });
    const res = runCli(REPAINT_ARGS, withPath(bin, { cwd: root }));
    assert.equal(res.status, 1, `failing suites must exit 1, got ${res.status}`);
    assert.ok(/fail/i.test(`${res.stdout}\n${res.stderr}`), 'exit-1 output must tabulate the failing check');
  });

  it('dropping --assert-one-frame flips its row to fail with exit 1', () => {
    const root = mkTmp('muse-grepaint-root-');
    writeBuiltHostFixture(root);
    const bin = writeStubBin({ ctest: { exit: 0, record: true } });
    const res = runCli(
      ['gallery-tap-repaint', '--assert-no-reinstantiation'],
      withPath(bin, { cwd: root }),
    );
    assert.equal(res.status, 1, `dropped --assert-one-frame must exit 1, got ${res.status}`);
  });

  it('dropping --assert-no-reinstantiation flips its row to fail with exit 1', () => {
    const root = mkTmp('muse-grepaint-root-');
    writeBuiltHostFixture(root);
    const bin = writeStubBin({ ctest: { exit: 0, record: true } });
    const res = runCli(
      ['gallery-tap-repaint', '--assert-one-frame'],
      withPath(bin, { cwd: root }),
    );
    assert.equal(res.status, 1, `dropped --assert-no-reinstantiation must exit 1, got ${res.status}`);
  });
});

describe('T8 — gallery-tap-repaint usage errors (exit 2)', () => {
  it('unknown flags exit 2 naming the flag', () => {
    const root = mkTmp('muse-grepaint-root-');
    writeBuiltHostFixture(root);
    const bin = writeStubBin({ ctest: { exit: 0 }, adb: { exit: 0 } });
    const res = runCli([...REPAINT_ARGS, '--bogus-flag'], withPath(bin, { cwd: root }));
    assert.equal(res.status, 2, `unknown flags must exit 2, got ${res.status}`);
    assert.ok(res.stderr.includes('--bogus-flag'), 'stderr must name the unknown flag');
  });
});

describe('T8 — gallery-tap-repaint device lane (--serial)', () => {
  it('unreachable serial fails closed exit 1 naming the device fault', () => {
    const root = mkTmp('muse-grepaint-root-');
    writeBuiltHostFixture(root);
    const bin = writeStubBin({
      ctest: { exit: 0 },
      adb: { exit: 1, out: 'device not found', record: true },
    });
    const res = runCli([...REPAINT_ARGS, '--serial', '127.0.0.1:5999'], withPath(bin, { cwd: root }));
    assert.equal(res.status, 1, `unreachable serial must exit 1, got ${res.status}`);
    assert.ok(
      /unreachable|device|serial|127\.0\.0\.1:5999/i.test(`${res.stdout}\n${res.stderr}`),
      'exit-1 output must name the device fault',
    );
  });

  it('missing --serial value exits 2', () => {
    const root = mkTmp('muse-grepaint-root-');
    writeBuiltHostFixture(root);
    const bin = writeStubBin({ ctest: { exit: 0 }, adb: { exit: 0 } });
    const res = runCli([...REPAINT_ARGS, '--serial'], withPath(bin, { cwd: root }));
    assert.equal(res.status, 2, `missing --serial value must exit 2, got ${res.status}`);
  });
});
