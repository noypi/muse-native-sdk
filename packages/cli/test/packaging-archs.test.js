'use strict';
// Batch 1 (T1 config validation + T3 template default resolution).
// T1 scope: resolveConfig archs/packaging defaults + fail-closed validation
//   (AC-PACKAGING-005, RULE-WORKFLOW-006).
// T3 scope: scaffold documents archs + unedited scaffold resolves to default
//   all-arch via T1 (AC-PACKAGING-004).
// Hermetic: node:test + node:assert/strict + stdlib only. Imports
// lib/config.js in-process; file layer uses fs.mkdtemp dirs; env is a plain
// object argument; no network, no secrets, no live services.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { resolveConfig, ConfigError } = require('../lib/config.js');

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

describe('T1 defaults — archs all four + packaging fat', () => {
  it('default resolution yields all four ABIs + fat with layers "default"', () => {
    const dir = mkTmp('muse-pack-archs-');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ALL_FOUR);
    assert.equal(config.packaging, 'fat');
    assert.equal(winningLayer.archs, 'default');
    assert.equal(winningLayer.packaging, 'default');
  });
});

describe('T1 fail-closed — unknown ABI (AC-PACKAGING-005)', () => {
  it('unknown ABI in file fails at configure naming value + accepted set', () => {
    const dir = mkTmp('muse-pack-archs-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'archs: arm64-v8a, mips\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /mips/);
      assert.match(String(err.message), /arm64-v8a, armeabi-v7a, x86_64, x86/);
      return true;
    });
  });

  it('unknown ABI via CLI fails naming value + accepted set', () => {
    const dir = mkTmp('muse-pack-archs-');
    assert.throws(() => resolveConfig(['--archs', 'x86_64,mips'], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /mips/);
      assert.match(String(err.message), /arm64-v8a, armeabi-v7a, x86_64, x86/);
      return true;
    });
  });
});

describe('T1 fail-closed — bad packaging value', () => {
  it('bad packaging value fails naming value + fat, split, both', () => {
    const dir = mkTmp('muse-pack-archs-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'packaging: universal\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /universal/);
      assert.match(String(err.message), /fat, split, both/);
      return true;
    });
  });
});

describe('T1 fail-closed — empty selection never defaults silently', () => {
  it('empty archs selection is an error', () => {
    const dir = mkTmp('muse-pack-archs-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'archs: \n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      return true;
    });
  });
});

describe('T1 archs file forms', () => {
  it('CSV scalar file form parses to array', () => {
    const dir = mkTmp('muse-pack-archs-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'archs: arm64-v8a, x86_64\n', 'utf8');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
    assert.equal(winningLayer.archs, 'file');
  });

  it('YAML flow-list file form parses to array', () => {
    const dir = mkTmp('muse-pack-archs-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'archs: [arm64-v8a, x86_64]\n', 'utf8');
    const { config } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
  });
});

describe('T3 scaffold — template documents archs + resolves to default (AC-PACKAGING-004)', () => {  it('template file mentions archs', () => {
    const template = path.resolve(__dirname, '..', 'templates', 'muse-native.yaml');
    const text = fs.readFileSync(template, 'utf8');
    assert.match(text, /archs/);
  });

  it('template ships packaging fat default key', () => {
    const template = path.resolve(__dirname, '..', 'templates', 'muse-native.yaml');
    const text = fs.readFileSync(template, 'utf8');
    assert.match(text, /packaging/);
  });

  it('unedited scaffold resolves to all four ABIs via resolveConfig', () => {
    const template = path.resolve(__dirname, '..', 'templates', 'muse-native.yaml');
    const dir = mkTmp('muse-pack-archs-');
    fs.copyFileSync(template, path.join(dir, 'muse-native.yaml'));
    const { config } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ALL_FOUR);
  });
});

// Batch 2 — T4 stage-output cases (TC-PACKAGING-001…003).
// Scope: runStages package lane only (AC-PACKAGING-001/002/003,
// RULE-PACKAGING-001/002). T1/T3 cases above are passthrough (must stay
// green after Batch 1 commit 35779f0); these T4 cases MUST RED until
// packages/cli/lib/stages.js packageStage writes per-config APK outputs.
// Hermetic: node:test + node:assert/strict + stdlib only. Imports
// lib/stages.js in-process; tmp app dirs via fs.mkdtemp; no signing,
// no bundletool, no network/secrets/device.
// ASSUMPTIONS (plan A1 + Detail "arch-named filenames; per-arch payload
// isolation" pin the placeholder layout, not real binaries):
// - A-T4a: fat placeholder is `dist/app-fat.apk` (UTF-8 text) listing every
//   selected arch (one arch-tagged entry per line).
// - A-T4b: split placeholders are `dist/app-<arch>.apk`, one per selected
//   arch, each containing only that arch's entry.
// - A-T4c: `both` writes fat + full split set in the same run.
// - A-T4d: `tree.mui.bin` passthrough is not asserted (T4 owns APKs only).

const { runStages } = require('../lib/stages.js');

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

function fatPath(appDir) {
  return path.join(appDir, 'dist', 'app-fat.apk');
}

function splitPath(appDir, arch) {
  return path.join(appDir, 'dist', `app-${arch}.apk`);
}

describe('T4 TC-PACKAGING-001 — fat emits single APK covering all archs', () => {
  it('fat default produces one fat APK containing every selected arch', async () => {
    const dir = mkTmp('muse-pack-t4-');
    writeStageApp(dir);
    const { config } = resolveConfig([], {}, dir);
    assert.equal(config.packaging, 'fat');
    const results = await runStages('package', { config, appDir: dir });
    assert.equal(results.length, 1);
    assert.equal(results[0].stage, 'package');
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(dir)), 'dist/app-fat.apk must exist');
    const body = fs.readFileSync(fatPath(dir), 'utf8');
    for (const arch of ALL_FOUR) assert.match(body, new RegExp(arch));
  });

  it('fat with subset archs covers only the selection (never silently narrows or widens)', async () => {
    const dir = mkTmp('muse-pack-t4-');
    writeStageApp(dir);
    const { config } = resolveConfig(['--archs', 'arm64-v8a,x86_64'], {}, dir);
    const results = await runStages('package', { config, appDir: dir });
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(dir)));
    const body = fs.readFileSync(fatPath(dir), 'utf8');
    assert.match(body, /arm64-v8a/);
    assert.match(body, /x86_64/);
    assert.doesNotMatch(body, /armeabi-v7a/);
    assert.doesNotMatch(body, /x86(?!_)/);
  });

  it('fat run writes no per-arch split APKs', async () => {
    const dir = mkTmp('muse-pack-t4-');
    writeStageApp(dir);
    const { config } = resolveConfig([], {}, dir);
    await runStages('package', { config, appDir: dir });
    for (const arch of ALL_FOUR) {
      assert.ok(!fs.existsSync(splitPath(dir, arch)), `no split ${arch} in fat mode`);
    }
  });
});

describe('T4 TC-PACKAGING-002 — split emits one arch-named APK per arch', () => {
  it('split produces one APK per arch each containing only its arch entry', async () => {
    const dir = mkTmp('muse-pack-t4-');
    writeStageApp(dir);
    const { config } = resolveConfig(['--archs', 'arm64-v8a,x86_64', '--packaging', 'split'], {}, dir);
    const results = await runStages('package', { config, appDir: dir });
    assert.equal(results[0].ok, true);
    for (const arch of ['arm64-v8a', 'x86_64']) {
      assert.ok(fs.existsSync(splitPath(dir, arch)), `dist/app-${arch}.apk must exist`);
      const body = fs.readFileSync(splitPath(dir, arch), 'utf8');
      assert.match(body, new RegExp(arch));
    }
    const armBody = fs.readFileSync(splitPath(dir, 'arm64-v8a'), 'utf8');
    assert.doesNotMatch(armBody, /x86_64/);
    const x86Body = fs.readFileSync(splitPath(dir, 'x86_64'), 'utf8');
    assert.doesNotMatch(x86Body, /arm64-v8a/);
  });

  it('single-arch split produces exactly one APK and no fat', async () => {
    const dir = mkTmp('muse-pack-t4-');
    writeStageApp(dir);
    const { config } = resolveConfig(['--archs', 'x86_64', '--packaging', 'split'], {}, dir);
    await runStages('package', { config, appDir: dir });
    assert.ok(fs.existsSync(splitPath(dir, 'x86_64')));
    assert.ok(!fs.existsSync(fatPath(dir)), 'no fat APK in split mode');
  });
});

describe('T4 TC-PACKAGING-003 — both emits fat + full split set in one run', () => {
  it('both produces fat plus every split from the same run without reconfiguration', async () => {
    const dir = mkTmp('muse-pack-t4-');
    writeStageApp(dir);
    const { config } = resolveConfig(['--archs', 'arm64-v8a,x86_64', '--packaging', 'both'], {}, dir);
    const results = await runStages('package', { config, appDir: dir });
    assert.equal(results.length, 1);
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(dir)), 'fat present');
    for (const arch of ['arm64-v8a', 'x86_64']) {
      assert.ok(fs.existsSync(splitPath(dir, arch)), `split ${arch} present`);
    }
    const fatBody = fs.readFileSync(fatPath(dir), 'utf8');
    assert.match(fatBody, /arm64-v8a/);
    assert.match(fatBody, /x86_64/);
  });

  it('package result carries stage/ok/durationMs shape', async () => {
    const dir = mkTmp('muse-pack-t4-');
    writeStageApp(dir);
    const { config } = resolveConfig([], {}, dir);
    const results = await runStages('package', { config, appDir: dir });
    assert.equal(results[0].stage, 'package');
    assert.equal(typeof results[0].durationMs, 'number');
  });

  it('package with no sources fails closed with message + location and writes no APKs', async () => {
    const dir = mkTmp('muse-pack-t4-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'name: empty\nversion: 0.0.1\n', 'utf8');
    const { config } = resolveConfig([], {}, dir);
    const results = await runStages('package', { config, appDir: dir });
    assert.equal(results[0].ok, false);
    assert.equal(typeof results[0].error.message, 'string');
    assert.equal(typeof results[0].error.location, 'string');
    assert.ok(!fs.existsSync(fatPath(dir)));
  });
});

// Batch 3 — T6 reference-target proofs (TC-PACKAGING-001/002/003/004/005).
// Scope: the T4 fat/split/both shapes plus T1 fail-closed and T3 template
// behavior, proven with the `examples/m3-gallery` reference target selected
// (`--with-sample`) from a tmp fake repo modeled on the committed gallery
// (app at `<repo>/my-app`, gallery fixture at
// `<repo>/examples/m3-gallery/src/Gallery.tsx`, mirroring the
// sample-target fixture pattern from targets.test.js). Hermetic: tmp dirs
// only, stdlib only; no device/emulator/network/signing.
// ASSUMPTION (plan Interface `none — test-only` pins no new surface): these
// cases wire the existing `resolveConfig`/`runStages` surfaces only; the
// gallery fixture stands in for the committed reference target so no
// checkout layout or device is required.

const GALLERY_MARKER_T6 = 'GALLERY-REF-m3-t6';

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
      `  return (<View><Text>${GALLERY_MARKER_T6}</Text></View>);`,
      '}',
      '',
    ].join('\n'),
    'utf8',
  );
  return appDir;
}

function writeBareAppT6(repo) {
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
      "import { View } from '@muse/native';",
      'export default function App() {',
      '  return (<View />);',
      '}',
      '',
    ].join('\n'),
    'utf8',
  );
  return appDir;
}

describe('T6 TC-PACKAGING-001 — fat shape with gallery reference target selected', () => {
  it('withSample fat packages the gallery selection covering all four archs', async () => {
    const repo = mkTmp('muse-pack-t6-');
    const appDir = writeGalleryRepoT6(repo);
    const { config } = resolveConfig(['--with-sample'], {}, appDir);
    assert.equal(config.withSample, true);
    const results = await runStages('package', { config, appDir });
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(appDir)), 'dist/app-fat.apk must exist');
    const body = fs.readFileSync(fatPath(appDir), 'utf8');
    for (const arch of ALL_FOUR) assert.match(body, new RegExp(arch));
  });

  it('withSample with absent gallery fails closed naming examples/m3-gallery', async () => {
    const repo = mkTmp('muse-pack-t6-');
    const appDir = writeBareAppT6(repo);
    assert.ok(!fs.existsSync(path.join(repo, 'examples', 'm3-gallery')));
    const { config } = resolveConfig(['--with-sample'], {}, appDir);
    const results = await runStages('package', { config, appDir });
    assert.equal(results[0].ok, false);
    assert.match(results[0].error.message, /examples\/m3-gallery/);
    assert.ok(!fs.existsSync(fatPath(appDir)));
  });
});

describe('T6 TC-PACKAGING-002/003 — split/both shapes with gallery reference target selected', () => {
  it('withSample split emits one isolated APK per arch', async () => {
    const repo = mkTmp('muse-pack-t6-');
    const appDir = writeGalleryRepoT6(repo);
    const { config } = resolveConfig(
      ['--with-sample', '--archs', 'arm64-v8a,x86_64', '--packaging', 'split'],
      {},
      appDir,
    );
    const results = await runStages('package', { config, appDir });
    assert.equal(results[0].ok, true);
    for (const arch of ['arm64-v8a', 'x86_64']) {
      assert.ok(fs.existsSync(splitPath(appDir, arch)), `dist/app-${arch}.apk must exist`);
      assert.match(fs.readFileSync(splitPath(appDir, arch), 'utf8'), new RegExp(arch));
    }
    assert.doesNotMatch(fs.readFileSync(splitPath(appDir, 'arm64-v8a'), 'utf8'), /x86_64/);
    assert.ok(!fs.existsSync(fatPath(appDir)), 'no fat APK in split mode');
  });

  it('withSample both emits fat + full split set in one run', async () => {
    const repo = mkTmp('muse-pack-t6-');
    const appDir = writeGalleryRepoT6(repo);
    const { config } = resolveConfig(['--with-sample', '--packaging', 'both'], {}, appDir);
    const results = await runStages('package', { config, appDir });
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(appDir)), 'fat present');
    for (const arch of ALL_FOUR) {
      assert.ok(fs.existsSync(splitPath(appDir, arch)), `split ${arch} present`);
    }
  });
});

describe('T6 TC-PACKAGING-004/005 — template grep + fail-closed against gallery app', () => {
  it('template documents all four ABIs, combos, and the packaging default', () => {
    const template = path.resolve(__dirname, '..', 'templates', 'muse-native.yaml');
    const text = fs.readFileSync(template, 'utf8');
    assert.match(text, /archs/);
    for (const arch of ALL_FOUR) assert.match(text, new RegExp(arch));
    assert.match(text, /packaging/);
  });

  it('unknown ABI fails closed for the gallery app naming value + accepted set', () => {
    const repo = mkTmp('muse-pack-t6-');
    const appDir = writeGalleryRepoT6(repo);
    assert.throws(
      () => resolveConfig(['--with-sample', '--archs', 'arm64-v8a,mips'], {}, appDir),
      (err) => {
        assert.ok(err instanceof ConfigError);
        assert.match(String(err.message), /mips/);
        assert.match(String(err.message), /arm64-v8a, armeabi-v7a, x86_64, x86/);
        return true;
      },
    );
  });
});
