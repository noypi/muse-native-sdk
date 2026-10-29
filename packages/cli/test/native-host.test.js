'use strict';
// Batch 3 (FR-PACKAGING-004 / FR-PACKAGING-005) — T5 Android host sources +
// T6 package stage cutover. Must RED until the T5 template sources and the T6
// stage cutover exist. Hermetic: node:test + node:assert/strict + stdlib only.
// No Android SDK/NDK, no device, no network, no secrets, no live toolchain.
// dex/aapt2/resources.arsc generation stays stubbed/fixture-driven (plan A3/A6);
// real compile is proven in the T6/T7 lane. File-content (grep-style)
// assertions only for T5 sources; runStages('package') is driven with an
// in-memory app dir under os.tmpdir() plus injected fixture host inputs.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-H1: T6 receives compiled/stubbed host bytes via
//   `ctx._hostInputs = { dexBytes, resourcesArsc, nativeLibs }` where
//   `nativeLibs` is `[{ abi, name, bytes }]` (per-arch libRuntime.so supplied
//   via explicit path input per plan risk note). Absent inputs fall back to
//   the coder's real toolchain; tests always inject fixtures.
// - A-H2: T6 signing passes through the T3 lane injectably via optional
//   `ctx._signApk = async (unsignedBytes) => signedBytes`. When absent the
//   coder shells to the real T3 lane; tests always inject the fake so no
//   zipalign/apksigner/SDK runs.
// - A-H3: `ctx._tree` (Buffer from the compile/emit lane) seeds
//   assets/tree.mui.bin; when absent the stage lowers sources in memory.
// - A-H4: per-arch libRuntime.so bytes in `_hostInputs.nativeLibs` are keyed
//   by exact `config.archs` entries; a missing arch fails closed.
// - A-H5: non-release runs emit APKs only (no app.aab); release runs
//   additionally write dist/app.aab from the same config.archs.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const CLI_DIR = path.resolve(__dirname, '..');
const ANDROID_DIR = path.join(CLI_DIR, 'templates', 'android');
const MANIFEST_PATH = path.join(ANDROID_DIR, 'AndroidManifest.xml');
const ACTIVITY_PATH = path.join(ANDROID_DIR, 'MainActivity.java');
const BRIDGE_PATH = path.join(ANDROID_DIR, 'TreeBridge.cpp');

function readOrThrow(p) {
  // Throws MODULE/file-missing style error when the T5 source is absent —
  // the expected RED state (sources missing), not a malformed test.
  if (!fs.existsSync(p)) {
    const err = new Error(`Cannot find module '${p}'`);
    err.code = 'MODULE_NOT_FOUND';
    throw err;
  }
  return fs.readFileSync(p, 'utf8');
}

// Minimal ZIP reader (stored + deflated), mirroring apk-builder.test.js so
// T6 pins behavior without a third-party ZIP dependency.
function parseZipEntries(buf) {
  assert.ok(Buffer.isBuffer(buf), 'APK output must be a Buffer');
  assert.ok(buf.length > 22, 'buffer long enough to hold EOCD');
  let eocd = -1;
  for (let i = buf.length - 22; i >= 0; i--) {
    if (buf[i] === 0x50 && buf[i + 1] === 0x4b && buf[i + 2] === 0x05 && buf[i + 3] === 0x06) {
      eocd = i;
      break;
    }
  }
  assert.ok(eocd >= 0, 'EOCD record (PK\\x05\\x06) present — valid ZIP');
  const count = buf.readUInt16LE(eocd + 10);
  const cdOffset = buf.readUInt32LE(eocd + 16);
  const names = [];
  let off = cdOffset;
  for (let n = 0; n < count; n++) {
    assert.equal(buf.readUInt32LE(off), 0x02014b50, `central header #${n} signature`);
    const nameLen = buf.readUInt16LE(off + 28);
    const extraLen = buf.readUInt16LE(off + 30);
    const commentLen = buf.readUInt16LE(off + 32);
    names.push(buf.toString('utf8', off + 46, off + 46 + nameLen));
    off += 46 + nameLen + extraLen + commentLen;
  }
  return names;
}

// --- T5: Android host sources --------------------------------------------

describe('T5 Android host sources (AC-PACKAGING-008/010/011)', () => {
  it('AndroidManifest.xml declares the launcher activity and package name', () => {
    const xml = readOrThrow(MANIFEST_PATH);
    assert.match(xml, /package="[^"]+"/, 'manifest declares a package name');
    assert.match(xml, /android\.intent\.action\.MAIN/, 'manifest has MAIN action');
    assert.match(xml, /android\.intent\.category\.LAUNCHER/, 'activity is exported launcher');
    assert.match(xml, /MainActivity/, 'manifest names MainActivity');
    assert.match(xml, /uses-sdk[^>]*minSdkVersion="26"/, 'minSdk 26 declared');
  });

  it('MainActivity loads libRuntime.so and bridges the tree asset to the engine', () => {
    const src = readOrThrow(ACTIVITY_PATH);
    assert.match(src, /System\.loadLibrary\(["']Runtime["']\)/, 'loads libRuntime.so');
    assert.match(src, /tree\.mui\.bin/, 'reads assets/tree.mui.bin');
    assert.match(src, /nativeLoadTree/, 'calls bridge to instantiate engine tree');
    assert.match(src, /nativeRenderFrame/, 'attaches render surface via bridge');
  });

  it('TreeBridge.cpp exports nativeLoadTree returning an opaque handle (0 on failure)', () => {
    const src = readOrThrow(BRIDGE_PATH);
    assert.match(src, /Java_com_muse_host_MainActivity_nativeLoadTree/, 'exports nativeLoadTree JNI symbol');
    assert.match(src, /jbyteArray/, 'nativeLoadTree takes tree bytes (jbyteArray)');
    assert.match(src, /jlong/, 'nativeLoadTree returns opaque handle (jlong)');
    assert.match(src, /return 0/, 'returns 0 on failure');
    assert.match(src, /Runtime_create|Runtime_destroy/, 'reuses engine runtime create/destroy');
  });

  it('TreeBridge.cpp exports no-op-on-0 nativeRenderFrame and never throws across JNI', () => {
    const src = readOrThrow(BRIDGE_PATH);
    assert.match(src, /Java_com_muse_host_MainActivity_nativeRenderFrame/, 'exports nativeRenderFrame JNI symbol');
    assert.match(src, /handle\s*==\s*0|!handle|handle == nullptr/, 'nativeRenderFrame no-ops on handle 0');
    // BUG-BUILD-009 single-definition invariant: the template must NOT define
    // JNI_OnLoad — the one non-test definition lives in
    // engine/runtime/src/jni_bridge.cpp (header comments may still mention the
    // gate, so match the definition signature, not the bare name).
    assert.doesNotMatch(src, /jint\s+JNI_OnLoad\s*\(/, 'template defines no JNI_OnLoad — single definition lives in engine/runtime/src/jni_bridge.cpp');
  });

  it('TreeBridge.cpp reuses the engine jni_bridge Runtime pattern without duplicating JNI_OnLoad', () => {
    const src = readOrThrow(BRIDGE_PATH);
    assert.match(src, /jni_bridge/, 'references engine jni_bridge pattern');
    assert.match(src, /Runtime_create/, 'creates engine tree via Runtime_create');
    assert.doesNotMatch(src, /jint\s+JNI_OnLoad\s*\(/, 'template defines no JNI_OnLoad definition');
    // The single surviving definition is the engine null-VM gate.
    const enginePath = path.join(CLI_DIR, '..', '..', 'engine', 'runtime', 'src', 'jni_bridge.cpp');
    const engineSrc = readOrThrow(enginePath);
    const defs = engineSrc.match(/jint\s+JNI_OnLoad\s*\(/g) || [];
    assert.equal(defs.length, 1, 'exactly one JNI_OnLoad definition lives in engine/runtime/src/jni_bridge.cpp');
    assert.match(engineSrc, /JNI_ERR/, 'engine gate fails closed (null VM -> JNI_ERR)');
    assert.match(engineSrc, /JNI_VERSION_1_6/, 'engine gate reports JNI_VERSION_1_6');
  });
});

// --- T6: package stage cutover --------------------------------------------

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

function writePassingApp(appDir) {
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: t6-app\nversion: 0.0.1\n', 'utf8');
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

// FR-PACKAGING-006 Stage 2 §6: per-arch libs are synthetic minimal ELF
// headers (magic + e_machine u16LE at offset 18) so the AC-PACKAGING-014
// arch gate accepts the slot ABI. The `_signApk` fake below is unchanged.
const MACHINE_FOR_ABI_T6 = {
  'arm64-v8a': 183,
  'armeabi-v7a': 40,
  x86_64: 62,
  x86: 3,
};

function makeElfT6(abi) {
  const buf = Buffer.alloc(64);
  buf[0] = 0x7f;
  buf.write('ELF', 1);
  buf.writeUInt16LE(MACHINE_FOR_ABI_T6[abi], 18);
  return Buffer.concat([buf, Buffer.from(`RUNTIME-T6-${abi}`, 'utf8')]);
}

function hostInputs(archs) {
  const list = archs || ['arm64-v8a', 'x86_64'];
  return {
    dexBytes: Buffer.from('DEX-STUB-T6-classes', 'utf8'),
    resourcesArsc: Buffer.from('ARSC-STUB-T6-resources', 'utf8'),
    nativeLibs: list.map((abi) => ({
      abi,
      name: 'libRuntime.so',
      bytes: makeElfT6(abi),
    })),
  };
}

function packageCtx(appDir, overrides) {
  return {
    appDir,
    config: {
      archs: ['arm64-v8a', 'x86_64'],
      packaging: 'fat',
      release: false,
      ...(overrides && overrides.config),
    },
    _hostInputs: hostInputs((overrides && overrides.config && overrides.config.archs) || undefined),
    _signApk: async (unsignedBytes) => Buffer.concat([Buffer.from(unsignedBytes), Buffer.from('-signed-T6')]),
    ...(overrides && overrides.ctx),
  };
}

describe('T6 package stage cutover (AC-PACKAGING-008/009)', () => {
  it("runStages('package') keeps its signature and writes a real signed APK for fat", async () => {
    const { runStages } = require('../lib/stages.js');
    const appDir = mkTmp('t6-fat-');
    writePassingApp(appDir);
    const ctx = packageCtx(appDir);
    const results = await runStages('package', ctx);
    assert.equal(results.length, 1);
    assert.equal(results[0].stage, 'package');
    assert.equal(results[0].ok, true);
    const apk = fs.readFileSync(path.join(appDir, 'dist', 'app-fat.apk'));
    const names = parseZipEntries(apk);
    for (const required of ['AndroidManifest.xml', 'classes.dex', 'resources.arsc']) {
      assert.ok(names.includes(required), `APK lists ${required} (TC-PACKAGING-008)`);
    }
    assert.ok(names.includes('assets/tree.mui.bin'), 'APK embeds the built tree');
    assert.ok(apk.includes(Buffer.from('-signed-T6')), 'APK passed through T3 sign (TC-PACKAGING-009)');
  });

  it("writes one dist/app-<arch>.apk per arch for split and keeps fat/split/both branching", async () => {
    const { runStages } = require('../lib/stages.js');
    const appDir = mkTmp('t6-split-');
    writePassingApp(appDir);
    const ctx = packageCtx(appDir, { config: { packaging: 'split' } });
    const results = await runStages('package', ctx);
    assert.equal(results[0].ok, true);
    for (const arch of ['arm64-v8a', 'x86_64']) {
      const apk = fs.readFileSync(path.join(appDir, 'dist', `app-${arch}.apk`));
      const names = parseZipEntries(apk);
      assert.ok(names.includes('AndroidManifest.xml'), `${arch} APK is a valid ZIP with manifest`);
      assert.ok(names.includes(`lib/${arch}/libRuntime.so`), `${arch} APK carries its libRuntime.so`);
    }
    assert.ok(!fs.existsSync(path.join(appDir, 'dist', 'app-fat.apk')), 'split emits no fat APK');
  });

  it('release also writes dist/app.aab from the same config.archs; non-release emits APKs only', async () => {
    const { runStages } = require('../lib/stages.js');
    const appDir = mkTmp('t6-aab-');
    writePassingApp(appDir);
    const ctx = packageCtx(appDir, { config: { packaging: 'both', release: true } });
    const results = await runStages('package', ctx);
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(path.join(appDir, 'dist', 'app.aab')), 'release writes dist/app.aab');
    const aabNames = parseZipEntries(fs.readFileSync(path.join(appDir, 'dist', 'app.aab')));
    assert.ok(aabNames.includes('AndroidManifest.xml'), 'AAB is a real bundle (valid ZIP with manifest)');
    assert.ok(fs.existsSync(path.join(appDir, 'dist', 'app-fat.apk')), 'both still writes fat APK');

    const appDir2 = mkTmp('t6-nonrel-');
    writePassingApp(appDir2);
    const ctx2 = packageCtx(appDir2);
    const results2 = await runStages('package', ctx2);
    assert.equal(results2[0].ok, true);
    assert.ok(!fs.existsSync(path.join(appDir2, 'dist', 'app.aab')), 'non-release emits APKs only');
  });

  it('failure leaves dist/ untouched (fail-closed) and keeps the path-confinement guard', async () => {
    const { runStages } = require('../lib/stages.js');
    const appDir = mkTmp('t6-fail-');
    writePassingApp(appDir);
    const ctx = packageCtx(appDir, {
      config: { archs: ['../evil'], packaging: 'split' },
      ctx: {
        _hostInputs: {
          dexBytes: Buffer.from('DEX-STUB-T6', 'utf8'),
          resourcesArsc: Buffer.from('ARSC-STUB-T6', 'utf8'),
          nativeLibs: [],
        },
      },
    });
    const results = await runStages('package', ctx);
    assert.equal(results[0].ok, false);
    const distDir = path.join(appDir, 'dist');
    const leftovers = fs.existsSync(distDir)
      ? fs.readdirSync(distDir).filter((f) => f.endsWith('.apk') || f.endsWith('.aab'))
      : [];
    assert.deepEqual(leftovers, [], 'no APK/AAB artefact left behind on failure');
  });

  it('rejects a missing per-arch libRuntime.so instead of emitting an APK without it', async () => {
    const { runStages } = require('../lib/stages.js');
    const appDir = mkTmp('t6-missingso-');
    writePassingApp(appDir);
    const ctx = packageCtx(appDir);
    ctx._hostInputs.nativeLibs = ctx._hostInputs.nativeLibs.slice(0, 1); // drop x86_64
    const results = await runStages('package', ctx);
    assert.equal(results[0].ok, false);
  });
});
