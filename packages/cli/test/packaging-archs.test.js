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
    const results = await runStages('package', pkgCtx(config, dir));
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
    const results = await runStages('package', pkgCtx(config, dir));
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
    await runStages('package', pkgCtx(config, dir));
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
    const results = await runStages('package', pkgCtx(config, dir));
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
    await runStages('package', pkgCtx(config, dir));
    assert.ok(fs.existsSync(splitPath(dir, 'x86_64')));
    assert.ok(!fs.existsSync(fatPath(dir)), 'no fat APK in split mode');
  });
});

describe('T4 TC-PACKAGING-003 — both emits fat + full split set in one run', () => {
  it('both produces fat plus every split from the same run without reconfiguration', async () => {
    const dir = mkTmp('muse-pack-t4-');
    writeStageApp(dir);
    const { config } = resolveConfig(['--archs', 'arm64-v8a,x86_64', '--packaging', 'both'], {}, dir);
    const results = await runStages('package', pkgCtx(config, dir));
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
    const results = await runStages('package', pkgCtx(config, dir));
    assert.equal(results[0].stage, 'package');
    assert.equal(typeof results[0].durationMs, 'number');
  });

  it('package with no sources fails closed with message + location and writes no APKs', async () => {
    const dir = mkTmp('muse-pack-t4-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'name: empty\nversion: 0.0.1\n', 'utf8');
    const { config } = resolveConfig([], {}, dir);
    const results = await runStages('package', pkgCtx(config, dir));
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
    const results = await runStages('package', pkgCtx(config, appDir));
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
    const results = await runStages('package', pkgCtx(config, appDir));
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
    const results = await runStages('package', pkgCtx(config, appDir));
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
    const results = await runStages('package', pkgCtx(config, appDir));
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

// Security review coverage (RULE-PACKAGING-002 defense-in-depth; path
// traversal defense-in-depth). Scope: the `packageStage` sink reached by
// direct API misuse bypassing `resolveConfig` (all production callers route
// through `build.js` configure first, so these pin the sink itself).
// Hermetic: same node:test/stdlib/tmp-dir conventions as the T4 cases above.

describe('Security — stage sink fail-closed on invalid packaging value', () => {
  it("direct-API packaging 'universal' errors instead of ok:true with zero APKs", async () => {
    const dir = mkTmp('muse-pack-sec-');
    writeStageApp(dir);
    const { config } = resolveConfig([], {}, dir);
    const results = await runStages(
      'package',
      { config: { ...config, packaging: 'universal' }, appDir: dir },
    );
    assert.equal(results.length, 1);
    assert.equal(results[0].ok, false, 'truthy-invalid packaging at the sink must fail closed, never ok:true');
    assert.equal(typeof results[0].error.message, 'string');
    assert.ok(!fs.existsSync(fatPath(dir)), 'no fat APK on fail-closed run');
    for (const arch of ALL_FOUR) {
      assert.ok(!fs.existsSync(splitPath(dir, arch)), `no split ${arch} on fail-closed run`);
    }
  });
});

describe('Security — duplicate-ABI semantics documented (not changed)', () => {
  it('DOCUMENTED: resolveConfig preserves duplicate archs in author order (no silent dedupe, no reject)', () => {
    const dir = mkTmp('muse-pack-sec-');
    const { config } = resolveConfig(['--archs', 'arm64-v8a,arm64-v8a'], {}, dir);
    assert.deepEqual(config.archs, ['arm64-v8a', 'arm64-v8a']);
  });

  it('DOCUMENTED: stage tolerates preserved duplicates — same-path rewrite, still ok:true', async () => {
    const dir = mkTmp('muse-pack-sec-');
    writeStageApp(dir);
    const { config } = resolveConfig(
      ['--archs', 'arm64-v8a,arm64-v8a', '--packaging', 'split'],
      {},
      dir,
    );
    assert.deepEqual(config.archs, ['arm64-v8a', 'arm64-v8a']);
    const results = await runStages('package', pkgCtx(config, dir));
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(splitPath(dir, 'arm64-v8a')), 'split APK present after redundant rewrite');
  });
});

describe('Security — arch path-separator containment at the filename sink', () => {
  it("hostile arch '../evil' fails closed: ok:false, zero artefacts, nothing outside dist", async () => {
    const dir = mkTmp('muse-pack-sec-');
    writeStageApp(dir);
    const { config } = resolveConfig([], {}, dir);
    const results = await runStages(
      'package',
      { config: { ...config, packaging: 'split', archs: ['../evil'] }, appDir: dir },
    );
    assert.equal(results[0].ok, false, 'separator arch at the sink must fail closed');
    assert.equal(typeof results[0].error.message, 'string');
    // FR-PACKAGING-006 Stage 2 §6: the fail-closed lane rejects the hostile
    // arch before creating dist/ — a missing dist/ is zero artefacts.
    const distEntries = fs.existsSync(path.join(dir, 'dist'))
      ? fs.readdirSync(path.join(dir, 'dist'))
      : [];
    assert.deepEqual(distEntries, [], 'dist left with zero artefacts');
    const topEntries = fs.readdirSync(dir).sort();
    assert.deepEqual(
      topEntries,
      topEntries.includes('dist') ? ['dist', 'muse-native.yaml', 'src'] : ['muse-native.yaml', 'src'],
    );
    assert.ok(!fs.existsSync(path.join(dir, 'evil.apk')));
    assert.ok(!fs.existsSync(path.join(dir, 'app-evil.apk')));
  });

  it('deep-escape arch never writes outside dist (sink allowlist guard required)', async () => {
    const root = mkTmp('muse-pack-sec-');
    const dir = path.join(root, 'inner', 'app');
    writeStageApp(dir);
    const { config } = resolveConfig([], {}, dir);
    // dist/inner/app/dist + 'app-x/../../../evil.apk' normalizes to
    // root/inner/evil.apk — outside appDir/dist but inside the tmp root so
    // the test stays hermetic (afterEach removes the tmp root).
    const results = await runStages(
      'package',
      { config: { ...config, packaging: 'split', archs: ['x/../../../evil'] }, appDir: dir },
    );
    assert.equal(results.length, 1);
    assert.equal(results[0].ok, false, 'escaping arch at the sink must fail closed, never ok:true');
    assert.ok(
      !fs.existsSync(path.join(root, 'inner', 'evil.apk')),
      'no file may be written outside dist',
    );
  });
});

// Batch FR-PACKAGING-004/005 T2 — keep-alive regression for selection shapes
// (regression guard for done FR-PACKAGING-001/002/003, TC-PACKAGING-001/002/003;
// no behavior change to selection logic). Scope: the fat/split/both output
// sets still resolve from the same config.archs per RULE-PACKAGING-001, now
// backed by T1 buffers (`buildApk` from lib/apk.js) instead of the
// placeholder arch-list text. These cases MUST RED until lib/apk.js lands
// (T1); every case above stays green throughout.
// Hermetic: node:test + node:assert/strict + stdlib only. Fixture bytes are
// inline mocks; ZIP name-listing is an inline Buffer-reader (stored and
// deflated entries); no disk writes (buffers only), no network, no secrets,
// no live device, no existing app config. Path-confinement assertions above
// are untouched — this block adds no dist/ writes.

// Lazy require (inside T2 helpers only): lib/apk.js does not exist until T1
// lands, and a top-level require would fail the whole file — including the
// green TC-001..007 cases above. Lazy loading keeps this file green except
// for the new T2 cases, which red individually with MODULE_NOT_FOUND.
function t2BuildApk() {
  return require('../lib/apk.js').buildApk;
}

const T2_MANIFEST = '<?xml version="1.0" encoding="utf-8"?><manifest package="com.muse.gallery" />';
const T2_DEX = Buffer.from('DEX-FIXTURE-T2-classes', 'utf8');
const T2_ARSC = Buffer.from('ARSC-FIXTURE-T2-resources', 'utf8');
const T2_TREE = Buffer.from('TREE-FIXTURE-T2-mui-bin', 'utf8');

function t2Libs(abis) {
  return abis.map((abi) => ({
    abi,
    name: 'libRuntime.so',
    bytes: Buffer.from(`SO-FIXTURE-T2-${abi}`, 'utf8'),
  }));
}

// Minimal central-directory name lister (mirrors apk-builder.test.js).
function t2ZipNames(buf) {
  assert.ok(Buffer.isBuffer(buf));
  let eocd = -1;
  for (let i = buf.length - 22; i >= 0; i--) {
    if (buf[i] === 0x50 && buf[i + 1] === 0x4b && buf[i + 2] === 0x05 && buf[i + 3] === 0x06) {
      eocd = i;
      break;
    }
  }
  assert.ok(eocd >= 0, 'EOCD present — valid ZIP');
  const count = buf.readUInt16LE(eocd + 10);
  const cdOffset = buf.readUInt32LE(eocd + 16);
  const names = [];
  let p = cdOffset;
  for (let n = 0; n < count; n++) {
    assert.equal(buf.readUInt32LE(p), 0x02014b50, `central header signature for entry ${n}`);
    const nLen = buf.readUInt16LE(p + 28);
    const xLen = buf.readUInt16LE(p + 30);
    const cLen = buf.readUInt16LE(p + 32);
    names.push(buf.toString('utf8', p + 46, p + 46 + nLen));
    p += 46 + nLen + xLen + cLen;
  }
  return names;
}

function t2FatBufferFor(config) {
  const buildApk = t2BuildApk();
  return buildApk({
    manifestXml: T2_MANIFEST,
    dexBytes: T2_DEX,
    resourcesArsc: T2_ARSC,
    nativeLibs: t2Libs(config.archs),
    treeBin: T2_TREE,
  });
}

function t2SplitBuffersFor(config) {
  const buildApk = t2BuildApk();
  return new Map(
    config.archs.map((arch) => [
      arch,
      buildApk({
        manifestXml: T2_MANIFEST,
        dexBytes: T2_DEX,
        resourcesArsc: T2_ARSC,
        nativeLibs: t2Libs([arch]),
        treeBin: T2_TREE,
      }),
    ]),
  );
}

describe('T2 TC-PACKAGING-001 — fat output set resolves from config.archs via T1 buffers', () => {
  it('default config archs back a fat buffer holding every arch lib + required entries', () => {
    const dir = mkTmp('muse-pack-t2-');
    const { config } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ALL_FOUR);
    const names = t2ZipNames(t2FatBufferFor(config));
    for (const arch of config.archs) {
      assert.ok(names.includes(`lib/${arch}/libRuntime.so`), `fat buffer holds ${arch}`);
    }
    for (const required of ['AndroidManifest.xml', 'classes.dex', 'resources.arsc', 'assets/tree.mui.bin']) {
      assert.ok(names.includes(required), `fat buffer holds ${required}`);
    }
  });

  it('subset archs back a fat buffer covering only the selection', () => {
    const dir = mkTmp('muse-pack-t2-');
    const { config } = resolveConfig(['--archs', 'arm64-v8a,x86_64'], {}, dir);
    const names = t2ZipNames(t2FatBufferFor(config));
    assert.ok(names.includes('lib/arm64-v8a/libRuntime.so'));
    assert.ok(names.includes('lib/x86_64/libRuntime.so'));
    assert.ok(!names.some((n) => n.startsWith('lib/armeabi-v7a/')));
  });
});

describe('T2 TC-PACKAGING-002 — split output set resolves from config.archs via T1 buffers', () => {
  it('one buffer per selected arch, each holding only its own lib', () => {
    const dir = mkTmp('muse-pack-t2-');
    const { config } = resolveConfig(['--archs', 'arm64-v8a,x86_64', '--packaging', 'split'], {}, dir);
    const splits = t2SplitBuffersFor(config);
    assert.equal(splits.size, 2);
    for (const [arch, buf] of splits) {
      const names = t2ZipNames(buf);
      assert.ok(names.includes(`lib/${arch}/libRuntime.so`), `split ${arch} holds its lib`);
      for (const other of config.archs) {
        if (other !== arch) assert.ok(!names.some((n) => n.startsWith(`lib/${other}/`)), `split ${arch} isolates ${other}`);
      }
      assert.ok(names.includes('AndroidManifest.xml'));
      assert.ok(names.includes('classes.dex'));
      assert.ok(names.includes('resources.arsc'));
    }
  });

  it('single-arch selection backs exactly one split buffer and no fat lib set', () => {
    const dir = mkTmp('muse-pack-t2-');
    const { config } = resolveConfig(['--archs', 'x86_64', '--packaging', 'split'], {}, dir);
    const splits = t2SplitBuffersFor(config);
    assert.equal(splits.size, 1);
    const names = t2ZipNames(splits.get('x86_64'));
    assert.ok(names.includes('lib/x86_64/libRuntime.so'));
    assert.ok(!names.some((n) => n.startsWith('lib/arm64-v8a/')));
  });
});

describe('T2 TC-PACKAGING-003 — both resolves fat + full split set from the same config.archs', () => {
  it('fat buffer and split buffers share one selection without reconfiguration', () => {
    const dir = mkTmp('muse-pack-t2-');
    const { config } = resolveConfig(['--archs', 'arm64-v8a,x86_64', '--packaging', 'both'], {}, dir);
    const fatNames = t2ZipNames(t2FatBufferFor(config));
    const splits = t2SplitBuffersFor(config);
    assert.ok(fatNames.includes('lib/arm64-v8a/libRuntime.so'));
    assert.ok(fatNames.includes('lib/x86_64/libRuntime.so'));
    assert.equal(splits.size, config.archs.length);
    for (const arch of config.archs) {
      assert.ok(t2ZipNames(splits.get(arch)).includes(`lib/${arch}/libRuntime.so`));
    }
  });
});
