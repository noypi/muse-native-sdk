'use strict';
// TC-WORKFLOW-014 (T5: FR-WORKFLOW-005 AC-WORKFLOW-014 — --strict promotes
// warnings to errors, nothing else changes).
// T5 scope: `strict` reflected through the CLI > env > file > default
// precedence chain into the printed resolution, plus the pure `applyStrict`
// promotion. Binds ONLY to the pinned T5 `Interface:` lines:
//   resolveConfig(argv, env, cwd) — `strict:true` reflected with winning layer.
//   applyStrict(severity, strict) — `warn→error` when strict is true,
//     identity otherwise; pure, never throws.
// Hermetic: node:test + node:assert/strict + stdlib only. `resolveConfig` is
// called in-process with fs.mkdtemp dirs (removed in afterEach) and a plain
// env object argument (process.env never read/mutated); `applyStrict` /
// `resolveSeverity` are pure in-process calls with inline values; the two
// build-equivalence cases spawn `node bin/muse-native.js` against mkdtemp
// dirs with per-spawn env. No network, no secrets, no live services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T5a: `applyStrict` lives in `packages/compiler/lib/diagnostics.js`
//   beside `resolveSeverity` (the pure severity-logic home established by T4;
//   the plan lists three candidate files without settling the export site).
//   Required as `../../compiler/lib/diagnostics.js` — a relative-path import
//   of a pure module, not a package dependency.
// - A-T5b: env form is `MUSE_NATIVE_STRICT` via the existing `envNameFor`
//   rule (A1); boolean CLI form is bare `--strict` meaning true; file form is
//   the `strict: true/false` yaml scalar; default is `false` with layer
//   `default`.
// - A-T5c: "changes no other build behaviour" is pinned as: the same project
//   builds byte-identical `dist/ir.json` with and without `--strict` (both
//   exit 0), so strict only affects diagnostic severity, never targets,
//   artefacts or durations reporting.

const { describe, it, before, after, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync, execFileSync } = require('node:child_process');

const { resolveConfig, ConfigError } = require('../lib/config.js');
const compilerDiagnostics = require('../../compiler/lib/diagnostics.js');
const { resolveSeverity } = compilerDiagnostics;
const { applyStrict } = compilerDiagnostics;

const BIN_NATIVE = path.join(path.resolve(__dirname, '..'), 'bin', 'muse-native.js');


const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

// FR-PACKAGING-006 Stage 2 §6 keystore fixture: spawned full builds reach
// the real sign lane, which needs a REAL keystore. Generated at runtime in
// before() via keytool (JDK, no SDK dependency) — never committed (GR-05
// R5.3). signArgs() appends the signing inputs; signEnv() supplies the
// storepass. Print-config/resolveConfig cases never reach signing.
let DEBUG_KS = null;
let DEBUG_KS_DIR = null;
before(() => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-debug-ks-'));
  DEBUG_KS_DIR = dir;
  DEBUG_KS = path.join(dir, 'debug.keystore');
  execFileSync('keytool', [
    '-genkeypair',
    '-keystore', DEBUG_KS,
    '-alias', 'androiddebugkey',
    '-storepass', 'android',
    '-keypass', 'android',
    '-keyalg', 'RSA',
    '-keysize', '2048',
    '-validity', '3650',
    '-dname', 'CN=Android Debug',
  ]);
});
after(() => {
  if (DEBUG_KS_DIR) fs.rmSync(DEBUG_KS_DIR, { recursive: true, force: true });
  DEBUG_KS = null;
  DEBUG_KS_DIR = null;
});
function signArgs(extra) {
  return [...extra, '--keystore', DEBUG_KS, '--key-alias', 'androiddebugkey'];
}
function signEnv(base) {
  const env = { ...(base || process.env) };
  if (env.MUSE_NATIVE_STOREPASS === undefined) env.MUSE_NATIVE_STOREPASS = 'android';
  return env;
}

function writeApp(appDir, name) {
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), `name: ${name}\nversion: 0.0.1\n`, 'utf8');
  addEngineLibs_F6(appDir);
  fs.writeFileSync(
    path.join(appDir, 'src', 'App.tsx'),
    [
      "import { View, Text } from '@muse/native';",
      'export default function App() {',
      '  return (<View><Text>hi</Text></View>);',
      '}',
      '',
    ].join('\n'),
    'utf8',
  );
}

// FR-PACKAGING-006 Stage 2 §6: specified-behavior engine inputs for spawned
// builds — synthetic minimal ELF per ABI written under the tmp app dir and
// referenced via the `engine_libs:` file layer, so the package stage
// (AC-PACKAGING-013/014) and the real in-build sign lane (AC-PACKAGING-016)
// succeed hermetically. Build purposes below are unchanged; only the
// interim (no-lib) inputs were replaced.
const ENGINE_LIB_ABIS_F6 = ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'];
const MACHINE_FOR_ABI_F6 = { 'arm64-v8a': 183, 'armeabi-v7a': 40, x86_64: 62, x86: 3 };
function writeEngineLibFiles_F6(dir) {
  fs.mkdirSync(dir, { recursive: true });
  const entries = [];
  for (const abi of ENGINE_LIB_ABIS_F6) {
    const buf = Buffer.alloc(64);
    buf[0] = 0x7f;
    buf.write('ELF', 1);
    buf.writeUInt16LE(MACHINE_FOR_ABI_F6[abi], 18);
    const soPath = path.join(dir, `libRuntime-${abi}.so`);
    fs.writeFileSync(soPath, Buffer.concat([buf, Buffer.from(`RUNTIME-F6-${abi}`, 'utf8')]));
    entries.push(`${abi}=${soPath}`);
  }
  return entries;
}
function addEngineLibs_F6(appDir) {
  const entries = writeEngineLibFiles_F6(path.join(appDir, 'engine-libs'));
  fs.appendFileSync(path.join(appDir, 'muse-native.yaml'), `engine_libs: ${entries.join(', ')}\n`);
  return entries;
}

describe('T5 applyStrict — warn→error promotion, identity otherwise (AC-014)', () => {
  it('exports applyStrict as a function', () => {
    assert.equal(typeof applyStrict, 'function', 'diagnostics.js must export applyStrict()');
  });

  it('promotes warn to error when strict is true', () => {
    assert.equal(applyStrict('warn', true), 'error');
  });

  it('leaves warn unchanged when strict is false', () => {
    assert.equal(applyStrict('warn', false), 'warn');
  });

  it('is the identity for error and off regardless of strict', () => {
    assert.equal(applyStrict('error', true), 'error');
    assert.equal(applyStrict('error', false), 'error');
    assert.equal(applyStrict('off', true), 'off');
    assert.equal(applyStrict('off', false), 'off');
  });

  it('is pure and never throws on its domain (repeatable, no I/O)', () => {
    for (const [severity, strict] of [['warn', true], ['warn', false], ['error', true], ['off', false]]) {
      const first = applyStrict(severity, strict);
      const second = applyStrict(severity, strict);
      assert.equal(first, second, `applyStrict(${severity}, ${strict}) must be deterministic`);
    }
  });

  it('composition: an application warn diagnostic exits as error under --strict', () => {
    const effective = resolveSeverity('CAPO0012', undefined, 'application');
    assert.equal(effective, 'warn');
    assert.equal(applyStrict(effective, true), 'error');
    assert.equal(applyStrict(effective, false), 'warn');
  });

  it('composition: an explicit error stays error with or without strict', () => {
    const effective = resolveSeverity('CAPO0012', 'error', 'application');
    assert.equal(applyStrict(effective, true), 'error');
    assert.equal(applyStrict(effective, false), 'error');
  });
});

describe('T5 strict resolution — CLI > env > file > default with winning layer (AC-014)', () => {
  it('defaults to strict:false with layer "default"', () => {
    const dir = mkTmp('muse-t5-strict-');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(config.strict, false);
    assert.equal(winningLayer.strict, 'default');
  });

  it('CLI --strict wins with layer "cli"', () => {
    const dir = mkTmp('muse-t5-strict-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'strict: false\n', 'utf8');
    const { config, winningLayer } = resolveConfig(['--strict'], { MUSE_NATIVE_STRICT: 'false' }, dir);
    assert.equal(config.strict, true);
    assert.equal(winningLayer.strict, 'cli');
  });

  it('env MUSE_NATIVE_STRICT beats file with layer "env"', () => {
    const dir = mkTmp('muse-t5-strict-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'strict: false\n', 'utf8');
    const { config, winningLayer } = resolveConfig([], { MUSE_NATIVE_STRICT: 'true' }, dir);
    assert.equal(config.strict, true);
    assert.equal(winningLayer.strict, 'env');
  });

  it('file strict:true resolves with layer "file"', () => {
    const dir = mkTmp('muse-t5-strict-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'strict: true\n', 'utf8');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(config.strict, true);
    assert.equal(winningLayer.strict, 'file');
  });

  it('bare --strict means true; --strict false means false', () => {
    const dir = mkTmp('muse-t5-strict-');
    const bare = resolveConfig(['--strict'], {}, dir);
    assert.equal(bare.config.strict, true);
    assert.equal(bare.winningLayer.strict, 'cli');
    const negated = resolveConfig(['--strict', 'false'], {}, dir);
    assert.equal(negated.config.strict, false);
    assert.equal(negated.winningLayer.strict, 'cli');
  });

  it('--strict with a garbage token throws ConfigError naming strict', () => {
    const dir = mkTmp('muse-t5-strict-');
    assert.throws(() => resolveConfig(['--strict', 'foo'], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.key, 'strict');
      assert.ok(String(err.message).includes('strict'));
      return true;
    });
  });

  it('--print-config reports strict active', () => {
    const repo = mkTmp('muse-t5-strict-');
    const appDir = path.join(repo, 'my-app');
    fs.mkdirSync(appDir, { recursive: true });
    writeApp(appDir, 't5-app');
    const r = spawnSync(
      process.execPath,
      [BIN_NATIVE, 'build', 'my-app', '--strict', '--print-config'],
      { encoding: 'utf8', cwd: repo },
    );
    assert.equal(r.status, 0, `--print-config must exit 0, got ${r.status}: ${r.stderr}`);
    const printed = JSON.parse(r.stdout);
    assert.equal(printed.config.strict, true, 'printed resolution must report strict:true');
    assert.equal(printed.winningLayer.strict, 'cli', 'printed resolution must name the winning layer');
  });

  it('strict changes no other build behaviour: identical artefacts with and without --strict', () => {
    const plain = mkTmp('muse-t5-strict-plain-');
    const strict = mkTmp('muse-t5-strict-strict-');
    const plainApp = path.join(plain, 'my-app');
    const strictApp = path.join(strict, 'my-app');
    fs.mkdirSync(plainApp, { recursive: true });
    fs.mkdirSync(strictApp, { recursive: true });
    writeApp(plainApp, 't5-app');
    writeApp(strictApp, 't5-app');
    const rPlain = spawnSync(process.execPath, [BIN_NATIVE, 'build', 'my-app', ...signArgs([])], {
      encoding: 'utf8',
      cwd: plain,
      env: signEnv(process.env),
    });
    const rStrict = spawnSync(process.execPath, [BIN_NATIVE, 'build', 'my-app', '--strict', ...signArgs([])], {
      encoding: 'utf8',
      cwd: strict,
      env: signEnv(process.env),
    });
    assert.equal(rPlain.status, 0, `plain build must exit 0, got ${rPlain.status}: ${rPlain.stderr}`);
    assert.equal(rStrict.status, 0, `strict build must exit 0, got ${rStrict.status}: ${rStrict.stderr}`);
    const irPlain = fs.readFileSync(path.join(plainApp, 'dist', 'ir.json'), 'utf8');
    const irStrict = fs.readFileSync(path.join(strictApp, 'dist', 'ir.json'), 'utf8');
    assert.equal(irStrict, irPlain, 'strict must not change build artefacts');
  });
});
