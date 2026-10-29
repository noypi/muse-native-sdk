'use strict';
// TC-WORKFLOW-004 (AC-WORKFLOW-004) + TC-WORKFLOW-005 (AC-WORKFLOW-005) — T3.
// `muse-native build` dispatches through seven stages with per-stage
// outcome/duration, and any stage can run alone via `--stage <name>`.
// Hermetic: node:test + node:assert/strict + stdlib only. Spawns
// `node bin/muse-native.js` against fs.mkdtemp dirs; env is passed per-spawn
// (never mutates process.env); no network, no secrets, no live services.
// Flag position follows the plan: `build <appDir> [--stage <name>]` (flags
// after the positional <appDir>).
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T3a: per-stage stdout line format is `<stage>: ok (<ms>ms)` on success
//   and `<stage>: FAIL <first-error> at <location>` on failure (plan Detail).
// - A-T3b: a project with no `.tsx` sources fails closed at the `configure`
//   stage (validation runs at configure per spec Notes), so its output names
//   the `configure` stage with a `FAIL ... at ...` line and writes no `dist/`.
// - A-T3c: an unknown `--stage` value is a usage error: nonzero exit with the
//   bad value named on stderr (T6 owns the exact code/message; here only
//   nonzero + names-the-value is asserted).
// - A-T3d: a failing build stops at the first failure — no later `ok` stage
//   line appears after the `FAIL` line.

const { describe, it, before, after, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync, execFileSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

const STAGES = [
  'configure',
  'typecheck',
  'compile',
  'emit',
  'native-build',
  'package',
  'install',
];

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
  const a = Array.isArray(args) ? args.slice() : [];
  // FR-PACKAGING-006 Stage 2 §6 keystore fixture: spawned full builds reach
  // the real sign lane, which needs a REAL keystore. Generated at runtime in
  // before() (never committed — GR-05 R5.3). Stage-failure/print-config
  // cases never reach signing and are unaffected by the flags.
  const needsKs = a[0] === 'build' && DEBUG_KS && !a.includes('--keystore');
  const finalArgs = needsKs
    ? [...a, '--keystore', DEBUG_KS, '--key-alias', 'androiddebugkey']
    : a;
  const env = { ...((options && options.env) || process.env) };
  if (needsKs && env.MUSE_NATIVE_STOREPASS === undefined) env.MUSE_NATIVE_STOREPASS = 'android';
  return spawnSync(process.execPath, [BIN_NATIVE, ...finalArgs], {
    encoding: 'utf8',
    ...options,
    env,
  });
}

// Runtime debug keystore (once per file): keytool ships with the JDK, no SDK
// dependency for this step. No key material is committed to the repo.
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


function writePassingApp(appDir) {
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(
    path.join(appDir, 'muse-native.yaml'),
    'name: stages-app\nversion: 0.0.1\n',
    'utf8',
  );
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

function writeNoSourcesApp(appDir) {
  fs.writeFileSync(
    path.join(appDir, 'muse-native.yaml'),
    'name: empty-app\nversion: 0.0.1\n',
    'utf8',
  );
}

function okLine(stage) {
  return new RegExp(`^${stage}: ok \\(\\d+ms\\)$`, 'm');
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

describe('TC-WORKFLOW-004 — full build reports all seven stage outcomes with durations', () => {
  it('full build prints one `ok` line per stage in order with exit 0', () => {
    const appDir = mkTmp('muse-stages-');
    writePassingApp(appDir);
    const r = runCli(['build', appDir]);
    assert.equal(r.status, 0, `full build must exit 0, got ${r.status}: ${r.stderr}`);
    let lastIndex = -1;
    for (const stage of STAGES) {
      assert.match(r.stdout, okLine(stage), `stdout must carry \`${stage}: ok (<ms>ms)\``);
      const idx = r.stdout.indexOf(`${stage}: ok`);
      assert.ok(idx > lastIndex, `stage \`${stage}\` must print after the previous stage`);
      lastIndex = idx;
    }
  });

  it('every stage line carries a numeric duration in milliseconds', () => {
    const appDir = mkTmp('muse-stages-');
    writePassingApp(appDir);
    const r = runCli(['build', appDir]);
    assert.equal(r.status, 0, `full build must exit 0, got ${r.status}: ${r.stderr}`);
    const lines = r.stdout.split('\n').filter((line) => /: ok \(\d+ms\)$/.test(line));
    assert.equal(lines.length, 7, `must print exactly seven \`<stage>: ok (<ms>ms)\` lines, got ${lines.length}`);
  });

  it('a failing build names the stage plus first error with source location and exits nonzero', () => {
    const appDir = mkTmp('muse-stages-');
    writeNoSourcesApp(appDir);
    const r = runCli(['build', appDir]);
    const combined = `${r.stdout}\n${r.stderr}`;
    assert.notEqual(r.status, 0, 'failing build must exit nonzero');
    assert.match(
      combined,
      /^configure: FAIL .+ at .+$/m,
      'failure must name the stage plus first error with source location (`<stage>: FAIL <error> at <location>`)',
    );
  });

  it('a failing build stops at the first failure: no later stage reports ok', () => {
    const appDir = mkTmp('muse-stages-');
    writeNoSourcesApp(appDir);
    const r = runCli(['build', appDir]);
    assert.notEqual(r.status, 0, 'failing build must exit nonzero');
    const failIdx = r.stdout.indexOf('FAIL');
    assert.ok(failIdx >= 0, 'stdout must carry the FAIL line');
    const after = r.stdout.slice(failIdx);
    for (const stage of STAGES) {
      assert.doesNotMatch(after, okLine(stage), `no stage may report ok after the failure (got \`${stage}: ok\`)`);
    }
  });

  it('a failing build stays fail-closed: no partial dist/ artefacts are written', () => {
    const appDir = mkTmp('muse-stages-');
    writeNoSourcesApp(appDir);
    const r = runCli(['build', appDir]);
    assert.notEqual(r.status, 0, 'failing build must exit nonzero');
    assert.ok(!fs.existsSync(path.join(appDir, 'dist', 'ir.json')), 'dist/ir.json must not exist after failure');
    assert.ok(!fs.existsSync(path.join(appDir, 'dist', 'tree.mui.bin')), 'dist/tree.mui.bin must not exist after failure');
  });
});

describe('TC-WORKFLOW-005 — any stage can run alone via --stage <name>', () => {
  it('--stage typecheck runs only that stage with exit 0', () => {
    const appDir = mkTmp('muse-stages-');
    writePassingApp(appDir);
    const r = runCli(['build', appDir, '--stage', 'typecheck']);
    assert.equal(r.status, 0, `--stage typecheck must exit 0, got ${r.status}: ${r.stderr}`);
    assert.match(r.stdout, okLine('typecheck'), 'stdout must carry `typecheck: ok (<ms>ms)`');
    for (const stage of STAGES) {
      if (stage === 'typecheck') continue;
      assert.doesNotMatch(r.stdout, new RegExp(`^${stage}: ok`, 'm'), `single-stage run must not print \`${stage}\``);
    }
  });

  it('--stage keeps the <appDir> positional with flags after it', () => {
    const appDir = mkTmp('muse-stages-');
    writePassingApp(appDir);
    const r = runCli(['build', appDir, '--stage', 'compile']);
    assert.equal(r.status, 0, `flags-after-positional run must exit 0, got ${r.status}: ${r.stderr}`);
    assert.match(r.stdout, okLine('compile'), 'stdout must carry `compile: ok (<ms>ms)`');
  });

  it('an unknown --stage value fails nonzero and names the bad value', () => {
    const appDir = mkTmp('muse-stages-');
    writePassingApp(appDir);
    const r = runCli(['build', appDir, '--stage', 'frobnicate']);
    const combined = `${r.stdout}\n${r.stderr}`;
    assert.notEqual(r.status, 0, 'unknown --stage must exit nonzero');
    assert.ok(combined.includes('frobnicate'), 'error output must name the bad stage value');
  });
});

describe('Security — stage-failure stderr never echoes secrets or stacks (AC-WORKFLOW-004/011)', () => {
  it('a stage FAIL with secret env values set prints neither the canary nor a stack trace', () => {
    const appDir = mkTmp('muse-stages-');
    writeNoSourcesApp(appDir);
    const canary = 'CANARY-MUSE-NATIVE-9f8e7d6c5b';
    const r = runCli(['build', appDir], {
      env: { ...process.env, MUSE_NATIVE_NAME: canary, MUSE_NATIVE_VERSION: canary },
    });
    assert.notEqual(r.status, 0, 'failing build must exit nonzero');
    const combined = `${r.stdout}\n${r.stderr}`;
    assert.ok(!combined.includes(canary), 'neither stdout nor stderr may echo the secret env value');
    assert.doesNotMatch(combined, /^\s+at\s/m, 'must not print a stack trace (`at ...` frame line)');
    assert.ok(!combined.includes('node:internal'), 'must not leak runtime internals');
    for (const line of r.stderr.split('\n').filter((l) => l.trim())) {
      assert.match(line, /^Error: /, `stderr line must be an option/key/location-shaped message, got: ${JSON.stringify(line)}`);
    }
  });
});
