'use strict';
// TC-WORKFLOW-007 (T1: FR-WORKFLOW-003 AC-WORKFLOW-007; FR-WORKFLOW-005 AC-WORKFLOW-014 strict key).
// T1 scope: typed config schema + printable resolution only (resolveConfig / getOptionSchema).
// No coder running; these tests must RED until packages/cli/lib/config.js gains the new keys.
// Hermetic: node:test + node:assert/strict + stdlib only. Imports lib/config.js
// in-process; file layer uses fs.mkdtemp dirs; env is a plain object argument
// (process.env never read/mutated); no network, no secrets, no live services.
// ASSUMPTION A1 (contract genuinely silent): boolean keys (withSample,
// release, strict) use bare CLI flags (`--with-sample`, `--release`,
// `--strict` meaning true) and `key: true/false` yaml scalars; value keys
// (abi, modules, output) use `--<key> <value>` / string scalars; env form is
// MUSE_NATIVE_<UPPER_SNAKE> via existing envNameFor; defaults are
// withSample:false, release:false, strict:false, abi/modules/output undefined,
// diagnostics undefined. Positional-target equivalence is T2, not tested here.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { resolveConfig, getOptionSchema, ConfigError } = require('../lib/config.js');

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

describe('T1 targets — withSample/strict/diagnostics schema + resolution', () => {
  it('defaults: withSample:false, strict:false, diagnostics undefined, all layer "default"', () => {
    const dir = mkTmp('muse-t1-targets-');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(config.withSample, false);
    assert.equal(config.strict, false);
    assert.equal(config.diagnostics, undefined);
    assert.equal(winningLayer.withSample, 'default');
    assert.equal(winningLayer.strict, 'default');
  });

  it('CLI --with-sample selects the sample with layer "cli"', () => {
    const dir = mkTmp('muse-t1-targets-');
    const { config, winningLayer } = resolveConfig(['--with-sample'], {}, dir);
    assert.equal(config.withSample, true);
    assert.equal(winningLayer.withSample, 'cli');
  });

  it('file key withSample:true resolves with layer "file"', () => {
    const dir = mkTmp('muse-t1-targets-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'withSample: true\n', 'utf8');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(config.withSample, true);
    assert.equal(winningLayer.withSample, 'file');
  });

  it('env MUSE_NATIVE_WITH_SAMPLE beats file with layer "env"', () => {
    const dir = mkTmp('muse-t1-targets-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'withSample: false\n', 'utf8');
    const { config, winningLayer } = resolveConfig([], { MUSE_NATIVE_WITH_SAMPLE: 'true' }, dir);
    assert.equal(winningLayer.withSample, 'env');
    assert.equal(config.withSample, true);
  });

  it('CLI --strict is reflected with layer "cli"', () => {
    const dir = mkTmp('muse-t1-targets-');
    const { config, winningLayer } = resolveConfig(['--strict'], {}, dir);
    assert.equal(config.strict, true);
    assert.equal(winningLayer.strict, 'cli');
  });

  it('wrong type for withSample throws ConfigError naming key/file/expected, no partial return', () => {
    const dir = mkTmp('muse-t1-targets-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'withSample: 42\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.key, 'withSample');
      assert.ok(String(err.message).includes('withSample'));
      return true;
    });
  });

  it('file diagnostics nested block resolves to an object map', () => {
    const dir = mkTmp('muse-t1-targets-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'diagnostics:\n  CAPO0012: warn\n', 'utf8');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.deepEqual(config.diagnostics, { CAPO0012: 'warn' });
    assert.equal(winningLayer.diagnostics, 'file');
  });

  it('file diagnostics flat diagnostics.CODE line resolves to an object map', () => {
    const dir = mkTmp('muse-t1-targets-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'diagnostics.CAPO0012: error\n', 'utf8');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.deepEqual(config.diagnostics, { CAPO0012: 'error' });
    assert.equal(winningLayer.diagnostics, 'file');
  });

  it('bad diagnostics severity fails closed naming the code', () => {
    const dir = mkTmp('muse-t1-targets-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'diagnostics:\n  CAPO0012: scream\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.key, 'CAPO0012');
      assert.ok(String(err.message).includes('CAPO0012'));
      return true;
    });
  });

  it('dangerous diagnostics key fails closed with ConfigError naming the key', () => {
    const dir = mkTmp('muse-t1-targets-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'diagnostics:\n  __proto__: warn\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.key, '__proto__');
      assert.ok(String(err.message).includes('__proto__'));
      return true;
    });
  });

  it('getOptionSchema includes withSample/strict/diagnostics from live SCHEMA', () => {
    const names = new Map(getOptionSchema().map((e) => [e.name, e]));
    for (const key of ['withSample', 'strict', 'diagnostics']) {
      assert.ok(names.has(key), `schema must include ${key}`);
    }
    assert.equal(names.get('withSample').default, false);
    assert.equal(names.get('strict').default, false);
  });
});

// TC-WORKFLOW-007 (T3: FR-WORKFLOW-003 AC-WORKFLOW-007 — build honours target
// selection; sample excluded by default). T3 scope: runStages/build behavior
// only — resolveConfig layer reporting is T1 (above), flag acceptance is T2.
// Binds ONLY to the pinned T3 `Interface:` line:
//   runStages(stageName|undefined, ctx:{config,appDir}): Promise<StageResult[]>
//   — `configure` asserts resolved `withSample` selection and target
//   presence; default config never loads/compiles/packages `examples/m3-gallery`.
// Hermetic: node:test + node:assert/strict + stdlib only. Spawns
// `node bin/muse-native.js` with an explicit child `cwd` (the fake repo root;
// the parent process never chdirs); fixtures are fs.mkdtemp dirs; env is a
// plain per-spawn object (process.env never read/mutated); no network, no
// secrets, no live services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T3a: the sample target is `examples/m3-gallery` resolved relative to the
//   build working directory (child `cwd`), i.e. `<cwd>/examples/m3-gallery` —
//   the same relative form the spec and the T2 positional operand name.
// - A-T3b: selection means source inclusion: a `--with-sample` build compiles
//   and packages the sample sources into the same artefacts (so the sample
//   marker text appears in `<appDir>/dist/ir.json`); a default build's
//   artefacts contain no sample content even when the sample is present.
// - A-T3c: `--with-sample` with an absent sample target fails closed at
//   `configure`, before any compilation or `dist/` write: nonzero exit
//   (exit 2, the config-error class, same as AC-WORKFLOW-009/T2), stderr names
//   the target `examples/m3-gallery` and the expected path, no `dist/` written.
// - A-T3d: `--print-config` prints the resolved resolution JSON (existing
//   behavior) stating the selection (`config.withSample`).
// - A-T3e: in-process `runStages` sample tests are NOT written: without a
//   settled sample-path parameter on `ctx`, they would resolve against the
//   test runner's own cwd (order/machine-dependent, not hermetic). All sample
//   behavior is pinned through the spawned CLI with explicit `cwd`, which
//   drives the same `runStages(stageName, ctx)` boundary.

const { spawnSync } = require('node:child_process');

const { runStages } = require('../lib/stages.js');

const BIN_NATIVE_T3 = path.join(path.resolve(__dirname, '..'), 'bin', 'muse-native.js');
const SAMPLE_MARKER = 'SAMPLE-MARKER-7f3a';

function writeApp(appDir, name) {
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), `name: ${name}\nversion: 0.0.1\n`, 'utf8');
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

function writeSample(repoRoot) {
  const sampleDir = path.join(repoRoot, 'examples', 'm3-gallery');
  fs.mkdirSync(path.join(sampleDir, 'src'), { recursive: true });
  fs.writeFileSync(
    path.join(sampleDir, 'src', 'Gallery.tsx'),
    [
      "import { View, Text } from '@muse/native';",
      'export default function Gallery() {',
      `  return (<View><Text>${SAMPLE_MARKER}</Text></View>);`,
      '}',
      '',
    ].join('\n'),
    'utf8',
  );
  return sampleDir;
}

function readIr(appDir) {
  return fs.readFileSync(path.join(appDir, 'dist', 'ir.json'), 'utf8');
}

describe('T3 target selection — sample excluded by default, included when selected', () => {
  it('runStages interface: configure resolves ok with default (unselected) config', async () => {
    const repo = mkTmp('muse-t3-targets-');
    const appDir = path.join(repo, 'my-app');
    fs.mkdirSync(appDir, { recursive: true });
    writeApp(appDir, 't3-app');
    const { config } = resolveConfig([], {}, appDir);
    assert.equal(config.withSample, false);
    const results = await runStages('configure', { config, appDir });
    assert.equal(results.length, 1);
    assert.equal(results[0].stage, 'configure');
    assert.equal(results[0].ok, true);
    assert.equal(typeof results[0].durationMs, 'number');
  });

  it('default build excludes the sample even when it is present', () => {
    const repo = mkTmp('muse-t3-targets-');
    const appDir = path.join(repo, 'my-app');
    fs.mkdirSync(appDir, { recursive: true });
    writeApp(appDir, 't3-app');
    writeSample(repo);
    const r = spawnSync(process.execPath, [BIN_NATIVE_T3, 'build', 'my-app'], {
      encoding: 'utf8',
      cwd: repo,
    });
    assert.equal(r.status, 0, `default build must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(!readIr(appDir).includes(SAMPLE_MARKER), 'default build artefacts must not contain sample content');
  });

  it('--with-sample build compiles and packages the sample sources', () => {
    const repo = mkTmp('muse-t3-targets-');
    const appDir = path.join(repo, 'my-app');
    fs.mkdirSync(appDir, { recursive: true });
    writeApp(appDir, 't3-app');
    writeSample(repo);
    const r = spawnSync(process.execPath, [BIN_NATIVE_T3, 'build', 'my-app', '--with-sample'], {
      encoding: 'utf8',
      cwd: repo,
    });
    assert.equal(r.status, 0, `selected build must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(readIr(appDir).includes(SAMPLE_MARKER), 'selected build artefacts must contain sample content');
  });

  it('--with-sample with an absent sample target fails closed before compilation', () => {
    const repo = mkTmp('muse-t3-targets-');
    const appDir = path.join(repo, 'my-app');
    fs.mkdirSync(appDir, { recursive: true });
    writeApp(appDir, 't3-app');
    assert.ok(!fs.existsSync(path.join(repo, 'examples', 'm3-gallery')));
    const r = spawnSync(process.execPath, [BIN_NATIVE_T3, 'build', 'my-app', '--with-sample'], {
      encoding: 'utf8',
      cwd: repo,
    });
    assert.equal(r.status, 2, `absent sample target must exit 2 (config-error), got ${r.status}: ${r.stdout}`);
    assert.match(r.stderr, /examples\/m3-gallery/, 'stderr must name the missing sample target');
    assert.match(r.stderr, /Expected/, 'stderr must state the expected path');
    assert.ok(!fs.existsSync(path.join(appDir, 'dist')), 'must not write dist/ when the sample target is absent');
  });

  it('--print-config states the withSample selection', () => {
    const repo = mkTmp('muse-t3-targets-');
    const appDir = path.join(repo, 'my-app');
    fs.mkdirSync(appDir, { recursive: true });
    writeApp(appDir, 't3-app');
    const r = spawnSync(
      process.execPath,
      [BIN_NATIVE_T3, 'build', 'my-app', '--with-sample', '--print-config'],
      { encoding: 'utf8', cwd: repo },
    );
    assert.equal(r.status, 0, `--print-config must exit 0, got ${r.status}: ${r.stderr}`);
    const printed = JSON.parse(r.stdout);
    assert.equal(printed.config.withSample, true, 'printed resolution must state withSample:true');
  });
});
