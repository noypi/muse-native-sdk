'use strict';
// TC-WORKFLOW-003 (T2: FR-WORKFLOW-001, AC-WORKFLOW-003) — Batch 1.
// An unknown key, a wrong value type or an option that does not exist in the
// schema fails the build before compilation, naming the key, the file and
// the expected type. Never returns a partial config: on failure no `dist/`
// artefacts are written.
// Hermetic: node:test + node:assert/strict + stdlib only. Spawns
// `node bin/muse-native.js` against fs.mkdtemp dirs; no network, no secrets,
// no live services. Error format per the plan:
//   `Error: <key> in <file>: expected <type>`
// Exit code: nonzero here; the distinct config-error code (2) is wired and
// pinned by T6 (Batch 3), so this file asserts only fail-closed behavior.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T2a: schema owns at least `name: string` / `version: string`; a
//   numeric yaml scalar for `name` is the wrong-type case.
// - A-T2b: `withSample`/`strict` are in-scope schema keys (FR-WORKFLOW-003/
//   005, T1); genuinely unknown keys (`bogusKey`, `abi` when not in schema,
//   hostile path keys) are still rejected as unknown keys fail-closed.

const { describe, it, before, after, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync, execFileSync } = require('node:child_process');

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
  const a = Array.isArray(args) ? args.slice() : [];
  // FR-PACKAGING-006 Stage 2 §6 keystore fixture: spawned full builds reach
  // the real sign lane, which needs a REAL keystore. Generated at runtime in
  // before() (never committed — GR-05 R5.3). Config-failure/print-config
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

function writeApp(appDir, yamlText) {
  fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), yamlText, 'utf8');
  addEngineLibs_F6(appDir);
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(
    path.join(appDir, 'src', 'App.tsx'),
    "import { View } from '@muse/native';\nexport default function App() { return (<View />); }\n",
    'utf8',
  );
}

function assertFailsClosed(r, key) {

  assert.notEqual(r.status, 0, `invalid config must fail, got exit ${r.status}`);
  assert.match(r.stderr, new RegExp(`Error: ${key} in \\S+: expected \\S+`), `stderr must name key, file and expected type for '${key}', got: ${JSON.stringify(r.stderr)}`);
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

describe('TC-WORKFLOW-003 — unknown key / wrong type fails before compilation', () => {
  it('unknown key fails closed naming the key, the file and the expected type', () => {
    const appDir = mkTmp('muse-schema-');
    writeApp(appDir, 'name: bad-app\nbogusKey: 1\n');
    const r = runCli(['build', appDir]);
    assertFailsClosed(r, 'bogusKey');
    assert.ok(r.stderr.includes('muse-native.yaml'), 'stderr must name the config file');
  });

  it('wrong value type fails closed naming the key and the expected type', () => {
    const appDir = mkTmp('muse-schema-');
    writeApp(appDir, 'name: 123\n');
    const r = runCli(['build', appDir]);
    assertFailsClosed(r, 'name');
    assert.match(r.stderr, /string/, 'stderr must state the expected type');
  });

  it('in-scope target key withSample is accepted and resolves as boolean (AC-WORKFLOW-007)', () => {
    const appDir = mkTmp('muse-schema-');
    writeApp(appDir, 'name: good-app\nwithSample: true\n');
    const r = runCli(['build', appDir, '--print-config']);
    assert.equal(r.status, 0, `withSample:true must validate, got exit ${r.status}: ${r.stderr}`);
    const parsed = JSON.parse(r.stdout.trim());
    assert.equal(parsed.config.withSample, true, 'withSample:true must resolve as boolean true');
    const defDir = mkTmp('muse-schema-');
    writeApp(defDir, 'name: good-app\nversion: 0.0.1\n');
    const d = runCli(['build', defDir, '--print-config']);
    assert.equal(d.status, 0, `default config must exit 0, got ${d.status}: ${d.stderr}`);
    assert.equal(JSON.parse(d.stdout.trim()).config.withSample, false, 'withSample must default to false');
  });

  it('in-scope severity key strict is accepted and resolves as boolean (AC-WORKFLOW-014)', () => {
    const appDir = mkTmp('muse-schema-');
    writeApp(appDir, 'name: good-app\nstrict: true\n');
    const r = runCli(['build', appDir, '--print-config']);
    assert.equal(r.status, 0, `strict:true must validate, got exit ${r.status}: ${r.stderr}`);
    const parsed = JSON.parse(r.stdout.trim());
    assert.equal(parsed.config.strict, true, 'strict:true must resolve as boolean true');
    const defDir = mkTmp('muse-schema-');
    writeApp(defDir, 'name: good-app\nversion: 0.0.1\n');
    const d = runCli(['build', defDir, '--print-config']);
    assert.equal(d.status, 0, `default config must exit 0, got ${d.status}: ${d.stderr}`);
    assert.equal(JSON.parse(d.stdout.trim()).config.strict, false, 'strict must default to false');
  });

  it('failure happens before compilation: no dist/ artefacts are written (no partial config)', () => {
    const appDir = mkTmp('muse-schema-');
    writeApp(appDir, 'name: bad-app\nbogusKey: 1\n');
    const r = runCli(['build', appDir]);
    assert.notEqual(r.status, 0, `invalid config must fail, got exit ${r.status}`);
    assert.ok(!fs.existsSync(path.join(appDir, 'dist', 'ir.json')), 'must not write ir.json on config failure');
    assert.ok(
      !fs.existsSync(path.join(appDir, 'dist', 'tree.mui.bin')),
      'must not write tree.mui.bin on config failure',
    );
  });

  it('--print-config with an invalid file also fails closed instead of printing a partial config', () => {
    const appDir = mkTmp('muse-schema-');
    writeApp(appDir, 'name: 123\n');
    const r = runCli(['build', appDir, '--print-config']);
    assertFailsClosed(r, 'name');
    assert.ok(!r.stdout.trim().startsWith('{'), 'must not print a resolved-config JSON object on failure');
  });

  it('hostile key with path segments fails closed as an unknown key with no outside writes', () => {
    const parent = mkTmp('muse-schema-');
    const appDir = path.join(parent, 'app');
    fs.mkdirSync(appDir);
    writeApp(appDir, 'name: bad-app\n../../evil: 1\n');
    const r = runCli(['build', appDir]);
    assert.notEqual(r.status, 0, `hostile key must fail, got exit ${r.status}`);
    assert.ok(r.stderr.length > 0, 'must print an error to stderr');
    assert.ok(!fs.existsSync(path.join(parent, 'evil')), 'must not write outside the app dir');
    assert.ok(!fs.existsSync(path.join(appDir, 'dist', 'ir.json')), 'must not write ir.json on config failure');
  });

  it('valid config passes validation: --print-config exits 0', () => {
    const appDir = mkTmp('muse-schema-');
    writeApp(appDir, 'name: good-app\nversion: 0.0.1\n');
    const r = runCli(['build', appDir, '--print-config']);
    assert.equal(r.status, 0, `valid config must exit 0, got ${r.status}: ${r.stderr}`);
    const parsed = JSON.parse(r.stdout.trim());
    assert.equal(parsed.config.name, 'good-app', 'valid file value must resolve');
  });
});

describe('Security — hostile config VALUE containment (AC-WORKFLOW-003/004)', () => {
  // A traversal-shaped VALUE (`../../evil`) is an inert string: it must
  // resolve verbatim through every layer, never steer a path/shell sink,
  // and leave every write under `<appDir>/dist` with nothing outside.
  const EVIL = '../../evil';

  function writeGoodApp(appDir) {
    writeApp(appDir, 'name: good-app\nversion: 0.0.1\n');
  }

  function assertContained(parent, appDir, r) {
    assert.equal(r.status, 0, `traversal value must stay inert (exit 0), got ${r.status}: ${r.stderr}`);
    assert.deepEqual(fs.readdirSync(parent).sort(), ['app'], 'nothing may be created outside the app dir');
    assert.ok(!fs.existsSync(path.join(parent, 'evil')), 'must not create an `evil` dir outside the app dir');
    assert.ok(
      fs.existsSync(path.join(appDir, 'dist', 'ir.json')),
      'artefacts must still land under <appDir>/dist',
    );
    assert.ok(
      fs.existsSync(path.join(appDir, 'dist', 'tree.mui.bin')),
      'artefacts must still land under <appDir>/dist',
    );
  }

  it('CLI layer: --name ../../evil resolves as an inert string with writes under <appDir>/dist', () => {
    const parent = mkTmp('muse-schema-');
    const appDir = path.join(parent, 'app');
    fs.mkdirSync(appDir);
    writeGoodApp(appDir);
    const r = runCli(['build', appDir, '--name', EVIL]);
    assertContained(parent, appDir, r);
    const resolved = runCli(['build', appDir, '--print-config', '--name', EVIL]);
    assert.equal(resolved.status, 0, `--print-config must exit 0, got ${resolved.status}: ${resolved.stderr}`);
    assert.equal(JSON.parse(resolved.stdout.trim()).config.name, EVIL, 'CLI value must resolve verbatim');
  });

  it('env layer: MUSE_NATIVE_NAME=../../evil resolves as an inert string with writes under <appDir>/dist', () => {
    const parent = mkTmp('muse-schema-');
    const appDir = path.join(parent, 'app');
    fs.mkdirSync(appDir);
    writeGoodApp(appDir);
    const r = runCli(['build', appDir], { env: { ...process.env, MUSE_NATIVE_NAME: EVIL } });
    assertContained(parent, appDir, r);
  });

  it('file layer: `name: ../../evil` resolves as an inert string with writes under <appDir>/dist', () => {
    const parent = mkTmp('muse-schema-');
    const appDir = path.join(parent, 'app');
    fs.mkdirSync(appDir);
    writeApp(appDir, `name: ${EVIL}\nversion: 0.0.1\n`);
    const r = runCli(['build', appDir]);
    assertContained(parent, appDir, r);
    const resolved = runCli(['build', appDir, '--print-config']);
    assert.equal(JSON.parse(resolved.stdout.trim()).config.name, EVIL, 'file value must resolve verbatim');
  });

  it('symlink inside src/ is skipped, never read (Dirent semantics pinned)', () => {
    // collectTsxFiles only collects Dirent.isFile() entries, which is false
    // for symlinks — so a symlinked source is skipped rather than followed.
    // If a future refactor changes the walker, this pin must fail loudly.
    const parent = mkTmp('muse-schema-');
    const appDir = path.join(parent, 'app');
    fs.mkdirSync(appDir);
    writeGoodApp(appDir);
    const outside = path.join(parent, 'outside-secret.txt');
    fs.writeFileSync(outside, 'OUTSIDE-MARKER-9f8e7d6c5b', 'utf8');
    fs.symlinkSync(outside, path.join(appDir, 'src', 'link.tsx'));
    const r = runCli(['build', appDir]);
    assert.equal(r.status, 0, `build must exit 0 with the symlink skipped, got ${r.status}: ${r.stderr}`);
    const irJson = fs.readFileSync(path.join(appDir, 'dist', 'ir.json'), 'utf8');
    assert.ok(!irJson.includes('OUTSIDE-MARKER-9f8e7d6c5b'), 'symlink target contents must never be read');
    assert.ok(!irJson.includes('link.tsx'), 'symlinked source must not appear in the build');
    assert.ok(!r.stdout.includes('link.tsx'), 'stdout must not name the symlinked source');
  });
});
