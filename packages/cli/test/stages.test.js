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

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const stages = require('../lib/stages.js');
const ir = require('../lib/ir.js');

const CLI_DIR = path.resolve(__dirname, '..');
const REPO_ROOT = path.resolve(CLI_DIR, '..', '..');
const GALLERY_DIR = path.join(REPO_ROOT, 'examples', 'm3-gallery');
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
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function writePassingApp(appDir) {
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(
    path.join(appDir, 'muse-native.yaml'),
    'name: stages-app\nversion: 0.0.1\n',
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

// BUG-SAMPLE-002 — gallery-is-sample regression (FR-SAMPLE-007, AC-SAMPLE-018).
// `loadEffectiveSourcesOrThrow` is internal to `../lib/stages.js`, so these
// cases drive it through its public boundary `runStages('compile', ctx)`
// (exported) and read the effective source count back as
// `ctx._tree.roots.length` — `lowerToIR` maps one root per source, so roots
// length === effective sources length. `config: { withSample: true }`
// selects the sample merge without touching process env, network, or the
// real filesystem outside hermetic tmp dirs (case (b)) and the read-only
// repo gallery (case (a)); `compile` writes nothing.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T2a: the gallery source count is read live (`ir.loadSources`), not
//   pinned to plan A2's `4`: this checkout holds 3 `.tsx` files, so the
//   single-load assertion is `roots === appCount` (currently 3). A drift
//   that changes the gallery file set keeps the test meaningful.
// - A-T2b: a hermetic out-of-tree tmp app cannot see the checkout's sample
//   (`resolveSampleDir` fails closed off-checkout), so case (b) builds a
//   hermetic tmp repo `<w>/app` + `<w>/examples/m3-gallery` (a recursive
//   copy of the real gallery) to exercise the intended app+sample merge.
describe('BUG-SAMPLE-002 — gallery-is-sample returns app sources once (AC-SAMPLE-018)', () => {
  it('sample target with withSample compiles from its own sources exactly once (no doubled concat)', async () => {
    const appSources = ir.loadSources(GALLERY_DIR);
    assert.ok(appSources.length > 0, 'gallery fixture must hold at least one .tsx source');
    const ctx = { appDir: GALLERY_DIR, config: { withSample: true } };
    const results = await stages.runStages('compile', ctx);
    assert.equal(results.length, 1, 'single-stage run must return exactly one result');
    assert.equal(results[0].ok, true, `compile of the sample target must succeed, got: ${JSON.stringify(results[0].error)}`);
    assert.equal(
      ctx._tree.roots.length,
      appSources.length,
      `gallery-is-sample must return app sources unconcatenated (roots ${ctx._tree.roots.length} !== app sources ${appSources.length}); unfixed code doubles to ${2 * appSources.length}`,
    );
  });

  it('non-sample app with withSample still concatenates app + sample sources (merge preserved)', async () => {
    const work = mkTmp('muse-sample-merge-');
    const appDir = path.join(work, 'app');
    fs.mkdirSync(appDir, { recursive: true });
    writePassingApp(appDir);
    const sampleDir = path.join(work, 'examples', 'm3-gallery');
    fs.mkdirSync(path.dirname(sampleDir), { recursive: true });
    fs.cpSync(GALLERY_DIR, sampleDir, { recursive: true });
    const appSources = ir.loadSources(appDir);
    const sampleSources = ir.loadSources(sampleDir);
    assert.ok(appSources.length > 0 && sampleSources.length > 0, 'both app and sample fixtures must hold sources');
    const ctx = { appDir, config: { withSample: true } };
    const results = await stages.runStages('compile', ctx);
    assert.equal(results.length, 1, 'single-stage run must return exactly one result');
    assert.equal(results[0].ok, true, `compile of the non-sample app must succeed, got: ${JSON.stringify(results[0].error)}`);
    assert.equal(
      ctx._tree.roots.length,
      appSources.length + sampleSources.length,
      `non-sample withSample build must concatenate (roots ${ctx._tree.roots.length} !== app ${appSources.length} + sample ${sampleSources.length}); an over-correction dropping the merge returns only ${appSources.length}`,
    );
  });

  it('sample-dir guard normalizes appDir aliases — trailing slash, dot segment, and relative spelling still single-load (FR-SAMPLE-007)', async () => {
    const appSources = ir.loadSources(GALLERY_DIR);
    assert.ok(appSources.length > 0, 'gallery fixture must hold at least one .tsx source');
    const dotAlias = GALLERY_DIR + path.sep + '.';
    const parentAlias = GALLERY_DIR + path.sep + '..' + path.sep + 'm3-gallery';
    const relativeAlias = path.relative(process.cwd(), GALLERY_DIR);
    // Precondition: every alias is textually distinct yet resolves identically.
    for (const alias of [dotAlias, parentAlias, relativeAlias]) {
      assert.notEqual(alias, GALLERY_DIR, `alias must differ textually from the canonical path, got ${JSON.stringify(alias)}`);
      assert.equal(path.resolve(alias), path.resolve(GALLERY_DIR), `alias must resolve identically: ${JSON.stringify(alias)}`);
    }
    for (const appDir of [GALLERY_DIR + path.sep, dotAlias, parentAlias, relativeAlias]) {
      const ctx = { appDir, config: { withSample: true } };
      const results = await stages.runStages('compile', ctx);
      assert.equal(results.length, 1, 'single-stage run must return exactly one result');
      assert.equal(results[0].ok, true, `compile of aliased sample target ${JSON.stringify(appDir)} must succeed, got: ${JSON.stringify(results[0].error)}`);
      assert.equal(
        ctx._tree.roots.length,
        appSources.length,
        `aliased sample target ${JSON.stringify(appDir)} must single-load (roots ${ctx._tree.roots.length} !== app sources ${appSources.length}); a string-equality guard evaded by the alias doubles to ${2 * appSources.length}`,
      );
    }
  });

  it('withSample with an absent sample target fails closed naming the expected sample dir (AC-SAMPLE-018)', async () => {
    const work = mkTmp('muse-sample-absent-');
    const appDir = path.join(work, 'app');
    fs.mkdirSync(appDir, { recursive: true });
    writePassingApp(appDir);
    // Out-of-tree tmp app: `resolveSampleDir` falls through to the
    // app-anchored sibling, which does not exist here.
    const expectedSampleDir = path.join(path.dirname(path.resolve(appDir)), 'examples', 'm3-gallery');
    assert.ok(!fs.existsSync(expectedSampleDir), `precondition: sample dir must be absent, but exists at ${expectedSampleDir}`);
    const ctx = { appDir, config: { withSample: true } };
    const results = await stages.runStages('compile', ctx);
    assert.equal(results.length, 1, 'single-stage run must return exactly one result');
    assert.equal(results[0].ok, false, 'compile with an absent sample target must fail closed (ok:false), not silently build app-only');
    const message = (results[0].error && results[0].error.message) || '';
    assert.ok(
      message.includes(expectedSampleDir),
      `stage failure must name the expected sample dir ${JSON.stringify(expectedSampleDir)}, got: ${JSON.stringify(message)}`,
    );
  });
});
