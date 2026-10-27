'use strict';
// T4 — release-pipeline integration gate + dist artifacts (NFR-BUILD-012 /
// AC-BUILD-036, AC-BUILD-039, AC-BUILD-040; TC-BUILD-036/039/040 file checks).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Composes T1 (emitBundle) + T2 (budgetGate); no new callable surface.
// All artifacts live under os.tmpdir() — including the `dist/`-named fixture
// copies (`index.android.bundle` + `.map`). No `dist/` binaries are committed
// (plan A4; choice flagged in the T4 contract).
// Red state: this file is new to batch 3; T1+T2 impls already landed, so the
// pass-half is expected GREEN and only a composition break would fail.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { emitBundle } = require('./emitBundle.js');
const { checkBudget } = require('./budgetGate.js');

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
});

const ENTRY_SRC = `'use strict';\nconsole.log('hello-muse-release');\n`;

function mkEntry(dir, name = 'index.js') {
  const entry = path.join(dir, name);
  fs.writeFileSync(entry, ENTRY_SRC);
  return entry;
}

// Mirror the plan's `dist/` artifact names inside a hermetic tmpdir fixture.
// Nothing is written to the repo's `dist/` and nothing is committed.
function stageDistArtifacts(root, bundlePath, mapPath) {
  const distDir = path.join(root, 'dist');
  fs.mkdirSync(distDir, { recursive: true });
  const distBundle = path.join(distDir, 'index.android.bundle');
  const distMap = path.join(distDir, 'index.android.bundle.map');
  fs.copyFileSync(bundlePath, distBundle);
  fs.copyFileSync(mapPath, distMap);
  return { distBundle, distMap };
}

function runReleasePipeline() {
  const root = mkTmp('release-pipeline-');
  const entry = mkEntry(root);
  const outDir = path.join(root, 'out');
  const emitted = emitBundle({ entry, mode: 'release', outDir });
  const staged = stageDistArtifacts(root, emitted.bundlePath, emitted.mapPath);
  return { root, entry, emitted, ...staged };
}

describe('T4 — release pipeline emits bundle + map (AC-BUILD-036/039)', () => {
  it('release emission flags bytecode:true with a Hermes marker bundle', () => {
    const { emitted } = runReleasePipeline();
    assert.equal(emitted.bytecode, true);
    const bundle = fs.readFileSync(emitted.bundlePath, 'utf8');
    assert.ok(bundle.includes('HERMES_BYTECODE'));
  });

  it('dist bundle artifact exists and is non-empty (TC-BUILD-036 file check)', () => {
    const { distBundle } = runReleasePipeline();
    const stat = fs.statSync(distBundle);
    assert.ok(stat.isFile());
    assert.ok(stat.size > 0);
  });

  it('dist source-map artifact exists, is non-empty, and names the entry (TC-BUILD-039 file check)', () => {
    const { distMap } = runReleasePipeline();
    const stat = fs.statSync(distMap);
    assert.ok(stat.isFile());
    assert.ok(stat.size > 0);
    const map = JSON.parse(fs.readFileSync(distMap, 'utf8'));
    assert.ok(typeof map.mappings === 'string' && map.mappings.length > 0);
    assert.ok(Array.isArray(map.sources) && map.sources.some((s) => String(s).includes('index.js')));
  });

  it('budget gate passes on the release pipeline output (AC-BUILD-037)', () => {
    const { distBundle } = runReleasePipeline();
    const manifest = [
      { module: 'startup', bytes: 120 },
      { module: 'screens/Home', bytes: 80 },
    ];
    const verdict = checkBudget({ bundlePath: distBundle, budgetBytes: 1024, manifest });
    assert.equal(verdict.ok, true);
    assert.equal(verdict.totalBytes, 200);
    assert.equal(verdict.offender, undefined);
  });
});

describe('T4 — oversized-budget variant fails naming the offender (AC-BUILD-040 full)', () => {
  it('overrun returns ok:false (never throws) naming the largest module', () => {
    const { distBundle } = runReleasePipeline();
    const manifest = [
      { module: 'startup', bytes: 120 },
      { module: 'screens/Heavy', bytes: 900 },
    ];
    let verdict;
    assert.doesNotThrow(() => {
      verdict = checkBudget({ bundlePath: distBundle, budgetBytes: 512, manifest });
    });
    assert.equal(verdict.ok, false);
    assert.equal(verdict.totalBytes, 1020);
    assert.equal(verdict.offender, 'screens/Heavy');
  });

  it('offender tracks a different largest module on another fixture', () => {
    const { distBundle } = runReleasePipeline();
    const manifest = [
      { module: 'screens/Heavy', bytes: 100 },
      { module: 'vendor/Charts', bytes: 950 },
    ];
    const verdict = checkBudget({ bundlePath: distBundle, budgetBytes: 512, manifest });
    assert.equal(verdict.ok, false);
    assert.equal(verdict.offender, 'vendor/Charts');
  });
});
