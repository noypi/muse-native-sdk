'use strict';
// Batch 1 T2 (AC-PACKAGING-006, FR-WORKFLOW-003, AC-WORKFLOW-008).
// Scope: CLI --archs/--packaging overrides + --print-config winning layer,
// via validateOptions/resolveConfig only. Must RED until
// packages/cli/lib/cli.js + packages/cli/lib/config.js gain the new flags.
// Hermetic: node:test + node:assert/strict + stdlib only. Imports
// lib/cli.js + lib/config.js in-process; file layer uses fs.mkdtemp dirs;
// env is a plain object argument; no network, no secrets, no live services.
// ASSUMPTION (contract genuinely silent): `--archs <csv>` and
// `--packaging <fat|split|both>` are space-form value flags like the existing
// VALUE_FLAGS pattern; env names MUSE_NATIVE_ARCHS / MUSE_NATIVE_PACKAGING.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { validateOptions, OptionError } = require('../lib/cli.js');
const { resolveConfig, ConfigError } = require('../lib/config.js');

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

describe('T2 validateOptions accepts --archs/--packaging', () => {
  it('accepts --archs <csv>', () => {
    validateOptions(['build', 'app', '--archs', 'arm64-v8a,x86_64']);
  });

  it('accepts --packaging split', () => {
    validateOptions(['build', 'app', '--packaging', 'split']);
  });

  it('rejects invalid --packaging value naming accepted set', () => {
    assert.throws(() => validateOptions(['build', 'app', '--packaging', 'universal']), (err) => {
      assert.ok(err instanceof OptionError);
      assert.equal(err.option, '--packaging');
      assert.match(String(err.accepted), /fat.*split.*both/);
      return true;
    });
  });

  it('rejects unknown option naming option + accepted', () => {
    assert.throws(() => validateOptions(['build', 'app', '--nope', 'x']), (err) => {
      assert.ok(err instanceof OptionError);
      assert.match(String(err.option), /--nope/);
      return true;
    });
  });
});

describe('T2 resolveConfig CLI overrides + winning layer (TC-006)', () => {
  it('CLI --archs/--packaging override file values with layer "cli"', () => {
    const dir = mkTmp('muse-pack-over-');
    fs.writeFileSync(
      path.join(dir, 'muse-native.yaml'),
      'archs: x86\npackaging: fat\n',
      'utf8',
    );
    const { config, winningLayer } = resolveConfig(
      ['--archs', 'arm64-v8a,x86_64', '--packaging', 'split'],
      {},
      dir,
    );
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
    assert.equal(config.packaging, 'split');
    assert.equal(winningLayer.archs, 'cli');
    assert.equal(winningLayer.packaging, 'cli');
  });

  it('env layer resolves with layer "env"', () => {
    const dir = mkTmp('muse-pack-over-');
    const { config, winningLayer } = resolveConfig(
      [],
      { MUSE_NATIVE_ARCHS: 'arm64-v8a,x86_64', MUSE_NATIVE_PACKAGING: 'both' },
      dir,
    );
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
    assert.equal(config.packaging, 'both');
    assert.equal(winningLayer.archs, 'env');
    assert.equal(winningLayer.packaging, 'env');
  });

  it('file layer resolves with layer "file"', () => {
    const dir = mkTmp('muse-pack-over-');
    fs.writeFileSync(
      path.join(dir, 'muse-native.yaml'),
      'archs: arm64-v8a, x86_64\npackaging: both\n',
      'utf8',
    );
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
    assert.equal(config.packaging, 'both');
    assert.equal(winningLayer.archs, 'file');
    assert.equal(winningLayer.packaging, 'file');
  });

  it('CLI beats env beats file for packaging with named winning layer', () => {
    const dir = mkTmp('muse-pack-over-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'packaging: fat\n', 'utf8');
    const fromEnv = resolveConfig([], { MUSE_NATIVE_PACKAGING: 'split' }, dir);
    assert.equal(fromEnv.config.packaging, 'split');
    assert.equal(fromEnv.winningLayer.packaging, 'env');
    const fromCli = resolveConfig(
      ['--packaging', 'both'],
      { MUSE_NATIVE_PACKAGING: 'split' },
      dir,
    );
    assert.equal(fromCli.config.packaging, 'both');
    assert.equal(fromCli.winningLayer.packaging, 'cli');
  });

  it('invalid CLI packaging value fails at configure naming accepted set', () => {
    const dir = mkTmp('muse-pack-over-');
    assert.throws(() => resolveConfig(['--packaging', 'universal'], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /universal/);
      assert.match(String(err.message), /fat, split, both/);
      return true;
    });
  });
});

// Batch 3 — T6 TC-PACKAGING-006 vs gallery reference target.
// Scope: CLI `--archs`/`--packaging` overrides win over the gallery app's
// file values with winning layer `cli`, and the resulting split output
// reflects the CLI selection — proven from a tmp fake repo modeled on
// `examples/m3-gallery` (same fixture pattern as targets.test.js).
// Hermetic: tmp dirs only, stdlib only; no device/emulator/network/signing.
// ASSUMPTION (plan Interface `none — test-only` pins no new surface): these
// cases wire the existing `resolveConfig`/`runStages` surfaces only.

const { runStages: runStagesT6 } = require('../lib/stages.js');

function writeGalleryRepoT6(repo) {
  const appDir = path.join(repo, 'my-app');
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(
    path.join(appDir, 'muse-native.yaml'),
    'name: gallery-app\nversion: 0.0.1\narchs: x86\npackaging: fat\n',
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

describe('T6 TC-PACKAGING-006 — CLI overrides win for the gallery app', () => {
  it('CLI --archs/--packaging override gallery file values with layer cli', async () => {
    const repo = mkTmp('muse-pack-t6over-');
    const appDir = writeGalleryRepoT6(repo);
    const { config, winningLayer } = resolveConfig(
      ['--with-sample', '--archs', 'arm64-v8a,x86_64', '--packaging', 'split'],
      {},
      appDir,
    );
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
    assert.equal(config.packaging, 'split');
    assert.equal(winningLayer.archs, 'cli');
    assert.equal(winningLayer.packaging, 'cli');
    const results = await runStagesT6('package', { config, appDir });
    assert.equal(results[0].ok, true);
    for (const arch of ['arm64-v8a', 'x86_64']) {
      const p = path.join(appDir, 'dist', `app-${arch}.apk`);
      assert.ok(fs.existsSync(p), `dist/app-${arch}.apk must exist`);
      assert.match(fs.readFileSync(p, 'utf8'), new RegExp(arch));
    }
    assert.ok(!fs.existsSync(path.join(appDir, 'dist', 'app-fat.apk')));
  });

  it('CLI --packaging both for the gallery app emits fat + file-arch splits in one run', async () => {
    const repo = mkTmp('muse-pack-t6over-');
    const appDir = writeGalleryRepoT6(repo);
    const { config, winningLayer } = resolveConfig(
      ['--with-sample', '--packaging', 'both'],
      {},
      appDir,
    );
    assert.equal(winningLayer.packaging, 'cli');
    assert.deepEqual(config.archs, ['x86']);
    assert.equal(winningLayer.archs, 'file');
    const results = await runStagesT6('package', { config, appDir });
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(path.join(appDir, 'dist', 'app-fat.apk')));
    assert.ok(fs.existsSync(path.join(appDir, 'dist', 'app-x86.apk')));
    assert.match(fs.readFileSync(path.join(appDir, 'dist', 'app-x86.apk'), 'utf8'), /x86/);
  });
});
