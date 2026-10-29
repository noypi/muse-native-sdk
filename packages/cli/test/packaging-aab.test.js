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
    const results = await runStages('package', { config: withRelease(config), appDir: dir });
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
    await runStages('package', { config: withRelease(config), appDir: dir });
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
    const results = await runStages('package', { config: withRelease(config), appDir: dir });
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
    const results = await runStages('package', { config: withRelease(config), appDir: dir });
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
    const results = await runStages('package', { config, appDir: dir });
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(dir)));
    assert.ok(!fs.existsSync(aabPath(dir)), 'no AAB outside release');
  });

  it('release package failure surfaces ok:false with message + location and writes nothing', async () => {
    const dir = mkTmp('muse-pack-aab-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'name: empty\nversion: 0.0.1\n', 'utf8');
    const { config } = resolveConfig([], {}, dir);
    const results = await runStages('package', { config: withRelease(config), appDir: dir });
    assert.equal(results[0].stage, 'package');
    assert.equal(results[0].ok, false);
    assert.equal(typeof results[0].error.message, 'string');
    assert.equal(typeof results[0].error.location, 'string');
    assert.equal(typeof results[0].durationMs, 'number');
    assert.ok(!fs.existsSync(aabPath(dir)));
    assert.ok(!fs.existsSync(fatPath(dir)));
  });
});
