'use strict';
// Batch 2 — T5 AAB alongside APK selection (TC-PACKAGING-007, AC-PACKAGING-007).
// Scope: runStages package lane only — release run additionally writes one
// AAB covering config.archs together with the T4 APK output in the same run;
// no second configuration or rebuild; errors surface as
// `{ ok: false, error: { message, location } }` at package.
// MUST RED until packages/cli/lib/stages.js packageStage writes the AAB.
// Hermetic: node:test + node:assert/strict + stdlib only. Imports
// lib/stages.js in-process; tmp app dirs via fs.mkdtemp; placeholder
// file-layout outputs (arch-tagged APK entries + AAB marker per plan A1);
// no signing/bundletool, no network/secrets/device.
// ASSUMPTIONS (plan A1 + Detail pin placeholders, not real binaries):
// - A-T5a: AAB placeholder is `dist/app.aab` (UTF-8 marker) listing every
//   selected arch; APK layout reuses the T4 convention (`dist/app-fat.apk`,
//   `dist/app-<arch>.apk`).
// - A-T5b: `release: true` in ctx.config selects the release run that emits
//   the AAB; a non-release run emits APKs only (AC-PACKAGING-007 scopes the
//   AAB to release builds).
// - A-T5c: one `runStages('package', ctx)` call produces APK(s) + AAB —
//   "no second configuration or rebuild" is verified by asserting all files
//   exist after a single call with a single ctx.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { resolveConfig } = require('../lib/config.js');
const { runStages } = require('../lib/stages.js');

// FR-PACKAGING-006 Stage 2 §6: specified-behavior hermetic inputs —
// synthetic minimal ELF per ABI (AC-PACKAGING-014 gate) + dex-magic seam +
// fake sign lane (AC-PACKAGING-016 hermetic seam). Containment/shape
// assertions below are unchanged; only the interim inputs were replaced.
const MACHINE_FOR_ABI_F6 = { 'arm64-v8a': 183, 'armeabi-v7a': 40, x86_64: 62, x86: 3 };
function makeElfF6(abi) {
  const buf = Buffer.alloc(64);
  buf[0] = 0x7f;
  buf.write('ELF', 1);
  buf.writeUInt16LE(MACHINE_FOR_ABI_F6[abi], 18);
  return Buffer.concat([buf, Buffer.from(`RUNTIME-F6-${abi}`, 'utf8')]);
}
function pkgCtx(config, appDir) {
  return {
    config,
    appDir,
    _hostInputs: {
      dexBytes: Buffer.concat([Buffer.from('dex\n035\0', 'binary'), Buffer.from('F6-SEAM')]),
      resourcesArsc: Buffer.from('ARSC-F6', 'utf8'),
      nativeLibs: config.archs.map((abi) => ({ abi, name: 'libRuntime.so', bytes: makeElfF6(abi) })),
    },
    _signApk: async (buf) => Buffer.concat([Buffer.from(buf), Buffer.from('-signed-F6')]),
  };
}

const ALL_FOUR = ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'];

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

function writeStageApp(appDir) {
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(
    path.join(appDir, 'muse-native.yaml'),
    'name: stage-app\nversion: 0.0.1\n',
    'utf8',
  );
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

function withRelease(config) {
  return { ...config, release: true };
}

function fatPath(appDir) {
  return path.join(appDir, 'dist', 'app-fat.apk');
}

function splitPath(appDir, arch) {
  return path.join(appDir, 'dist', `app-${arch}.apk`);
}

function aabPath(appDir) {
  return path.join(appDir, 'dist', 'app.aab');
}

describe('T5 TC-PACKAGING-007 — release fat emits AAB + fat APK in one run', () => {
  it('release fat run writes fat APK and one AAB covering config.archs', async () => {
    const dir = mkTmp('muse-pack-aab-');
    writeStageApp(dir);
    const { config } = resolveConfig([], {}, dir);
    const results = await runStages('package', pkgCtx(withRelease(config), dir));
    assert.equal(results.length, 1);
    assert.equal(results[0].stage, 'package');
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(dir)), 'dist/app-fat.apk must exist');
    assert.ok(fs.existsSync(aabPath(dir)), 'dist/app.aab must exist');
    const aab = fs.readFileSync(aabPath(dir), 'utf8');
    for (const arch of ALL_FOUR) assert.match(aab, new RegExp(arch));
  });

  it('AAB covers the same subset archs as the fat APK (RULE-PACKAGING-001)', async () => {
    const dir = mkTmp('muse-pack-aab-');
    writeStageApp(dir);
    const { config } = resolveConfig(['--archs', 'arm64-v8a,x86_64'], {}, dir);
    await runStages('package', pkgCtx(withRelease(config), dir));
    const fat = fs.readFileSync(fatPath(dir), 'utf8');
    const aab = fs.readFileSync(aabPath(dir), 'utf8');
    assert.match(fat, /arm64-v8a/);
    assert.match(aab, /arm64-v8a/);
    assert.match(aab, /x86_64/);
    assert.doesNotMatch(aab, /armeabi-v7a/);
  });
});

describe('T5 TC-PACKAGING-007 — release split/both emit AAB with the split set', () => {
  it('release split run writes full split set plus AAB in the same run', async () => {
    const dir = mkTmp('muse-pack-aab-');
    writeStageApp(dir);
    const { config } = resolveConfig(
      ['--archs', 'arm64-v8a,x86_64', '--packaging', 'split'],
      {},
      dir,
    );
    const results = await runStages('package', pkgCtx(withRelease(config), dir));
    assert.equal(results[0].ok, true);
    for (const arch of ['arm64-v8a', 'x86_64']) {
      assert.ok(fs.existsSync(splitPath(dir, arch)), `dist/app-${arch}.apk must exist`);
    }
    assert.ok(fs.existsSync(aabPath(dir)), 'dist/app.aab must exist alongside splits');
    assert.ok(!fs.existsSync(fatPath(dir)), 'no fat APK in split mode');
  });

  it('release both run writes fat + full split set + AAB from one call', async () => {
    const dir = mkTmp('muse-pack-aab-');
    writeStageApp(dir);
    const { config } = resolveConfig(['--packaging', 'both'], {}, dir);
    const results = await runStages('package', pkgCtx(withRelease(config), dir));
    assert.equal(results.length, 1);
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(dir)));
    for (const arch of ALL_FOUR) {
      assert.ok(fs.existsSync(splitPath(dir, arch)), `split ${arch} present`);
    }
    assert.ok(fs.existsSync(aabPath(dir)), 'AAB present with both');
  });

  it('non-release run emits APKs but no AAB', async () => {
    const dir = mkTmp('muse-pack-aab-');
    writeStageApp(dir);
    const { config } = resolveConfig([], {}, dir);
    assert.equal(config.release, false);
    const results = await runStages('package', pkgCtx(config, dir));
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(dir)));
    assert.ok(!fs.existsSync(aabPath(dir)), 'no AAB outside release');
  });

  it('release package failure surfaces ok:false with message + location and writes nothing', async () => {
    const dir = mkTmp('muse-pack-aab-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'name: empty\nversion: 0.0.1\n', 'utf8');
    const { config } = resolveConfig([], {}, dir);
    const results = await runStages('package', pkgCtx(withRelease(config), dir));
    assert.equal(results[0].stage, 'package');
    assert.equal(results[0].ok, false);
    assert.equal(typeof results[0].error.message, 'string');
    assert.equal(typeof results[0].error.location, 'string');
    assert.equal(typeof results[0].durationMs, 'number');
    assert.ok(!fs.existsSync(aabPath(dir)));
    assert.ok(!fs.existsSync(fatPath(dir)));
  });
});

// Batch 3 — T6 TC-PACKAGING-007 vs gallery reference target.
// Scope: release runs with the `examples/m3-gallery` reference target
// selected (`--with-sample`) emit one AAB covering `config.archs` together
// with the selected APK shape in a single `runStages('package')` call —
// proven from a tmp fake repo modeled on the committed gallery (same
// fixture pattern as targets.test.js). Hermetic: tmp dirs only, stdlib
// only; no device/emulator/network/signing.
// ASSUMPTION (plan Interface `none — test-only` pins no new surface): these
// cases wire the existing `resolveConfig`/`runStages` surfaces only.

function writeGalleryRepoT6(repo) {
  const appDir = path.join(repo, 'my-app');
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(
    path.join(appDir, 'muse-native.yaml'),
    'name: gallery-app\nversion: 0.0.1\n',
    'utf8',
  );
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
  const sampleSrc = path.join(repo, 'examples', 'm3-gallery', 'src');
  fs.mkdirSync(sampleSrc, { recursive: true });
  fs.writeFileSync(
    path.join(sampleSrc, 'Gallery.tsx'),
    [
      "import { View, Text } from '@muse/native';",
      'export default function Gallery() {',
      '  return (<View><Text>GALLERY-REF-m3-t6</Text></View>);',
      '}',
      '',
    ].join('\n'),
    'utf8',
  );
  return appDir;
}

describe('T6 TC-PACKAGING-007 — release gallery run emits AAB + APK shapes in one call', () => {
  it('release both + withSample writes fat + full split set + AAB from one call', async () => {
    const repo = mkTmp('muse-pack-t6aab-');
    const appDir = writeGalleryRepoT6(repo);
    const { config } = resolveConfig(['--with-sample', '--packaging', 'both'], {}, appDir);
    const results = await runStages('package', pkgCtx(withRelease(config), appDir));
    assert.equal(results.length, 1);
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(appDir)), 'fat present');
    for (const arch of ALL_FOUR) {
      assert.ok(fs.existsSync(splitPath(appDir, arch)), `split ${arch} present`);
    }
    assert.ok(fs.existsSync(aabPath(appDir)), 'AAB present with both');
    const aab = fs.readFileSync(aabPath(appDir), 'utf8');
    for (const arch of ALL_FOUR) assert.match(aab, new RegExp(arch));
  });

  it('release split + withSample writes splits + AAB and no fat', async () => {
    const repo = mkTmp('muse-pack-t6aab-');
    const appDir = writeGalleryRepoT6(repo);
    const { config } = resolveConfig(
      ['--with-sample', '--archs', 'arm64-v8a,x86_64', '--packaging', 'split'],
      {},
      appDir,
    );
    const results = await runStages('package', pkgCtx(withRelease(config), appDir));
    assert.equal(results[0].ok, true);
    for (const arch of ['arm64-v8a', 'x86_64']) {
      assert.ok(fs.existsSync(splitPath(appDir, arch)));
    }
    assert.ok(fs.existsSync(aabPath(appDir)), 'AAB alongside splits');
    const aab = fs.readFileSync(aabPath(appDir), 'utf8');
    assert.match(aab, /arm64-v8a/);
    assert.match(aab, /x86_64/);
    assert.doesNotMatch(aab, /armeabi-v7a/);
    assert.ok(!fs.existsSync(fatPath(appDir)), 'no fat APK in split mode');
  });

  it('release withSample with absent gallery fails closed writing no AAB/APK', async () => {
    const repo = mkTmp('muse-pack-t6aab-');
    const appDir = path.join(repo, 'my-app');
    fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
    fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: gallery-app\nversion: 0.0.1\n', 'utf8');
    fs.writeFileSync(
      path.join(appDir, 'src', 'App.tsx'),
      "import { View } from '@muse/native';\nexport default function App() {\n  return (<View />);\n}\n",
      'utf8',
    );
    assert.ok(!fs.existsSync(path.join(repo, 'examples', 'm3-gallery')));
    const { config } = resolveConfig(['--with-sample'], {}, appDir);
    const results = await runStages('package', pkgCtx(withRelease(config), appDir));
    assert.equal(results[0].ok, false);
    assert.match(results[0].error.message, /examples\/m3-gallery/);
    assert.ok(!fs.existsSync(aabPath(appDir)));
    assert.ok(!fs.existsSync(fatPath(appDir)));
  });
});
