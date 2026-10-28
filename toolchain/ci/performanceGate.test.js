'use strict';
// T10 — Performance gate (TEST-007) + artifact-retention manifest
// (NFR-BUILD-015 / AC-BUILD-054, AC-BUILD-055; TC-BUILD-054, TC-BUILD-055).
// Hermetic: node:test + node:assert/strict + stdlib fs/path only; in-memory
// gate inputs; the manifest/README assertions are read-only reads of the
// coder-committed files under dist/artifacts/.
// Red state: ./performanceGate.js and ./artifactManifest.js do not exist yet,
// and dist/artifacts/manifest.json + README.md are not committed, so every
// case below fails (MODULE_NOT_FOUND / ENOENT) until T10 lands.
//
// Interfaces under test (plan verbatim):
//   performanceGate(results:{suite:string, passed:boolean, budget:string|null}[]): {ok:boolean, failures:string[]}
//   artifactManifest(): {artifacts:string[]} returning exactly the 7 retained
//   names: release.aab, universal-release.apk, mapping.txt, unstripped/ (dir),
//   sourcemaps/ (dir), sbom.json, provenance.json.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const EXPECTED_ARTIFACTS = [
  'release.aab',
  'universal-release.apk',
  'mapping.txt',
  'unstripped/',
  'sourcemaps/',
  'sbom.json',
  'provenance.json',
];

function loadPerf() {
  return require('./performanceGate.js');
}

function loadManifest() {
  return require('./artifactManifest.js');
}

describe('T10 — performanceGate contract shape', () => {
  it('returns { ok:boolean, failures:string[] } on an all-passing run', () => {
    const { performanceGate } = loadPerf();
    const result = performanceGate([{ suite: 'startup', passed: true, budget: 'cold-start<2s' }]);
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.failures));
    assert.equal(result.ok, true);
    assert.deepEqual(result.failures, []);
  });
});

describe('T10 — performance gate (TC-BUILD-054)', () => {
  it('all suites passing passes with no failures', () => {
    const { performanceGate } = loadPerf();
    const result = performanceGate([
      { suite: 'startup', passed: true, budget: 'cold-start<2s' },
      { suite: 'scroll', passed: true, budget: null },
    ]);
    assert.equal(result.ok, true);
    assert.deepEqual(result.failures, []);
  });

  it('a failed suite blocks naming that suite', () => {
    const { performanceGate } = loadPerf();
    const result = performanceGate([
      { suite: 'startup', passed: true, budget: 'cold-start<2s' },
      { suite: 'scroll-jank', passed: false, budget: 'jank<5%' },
    ]);
    assert.equal(result.ok, false);
    assert.ok(
      result.failures.some((f) => f.includes('scroll-jank')),
      `must name the failed suite, got: ${JSON.stringify(result.failures)}`
    );
  });

  it('empty results fail closed', () => {
    const { performanceGate } = loadPerf();
    const result = performanceGate([]);
    assert.equal(result.ok, false);
    assert.ok(result.failures.length > 0);
  });

  it('ok is false iff failures is non-empty', () => {
    const { performanceGate } = loadPerf();
    const good = performanceGate([{ suite: 'a', passed: true, budget: null }]);
    assert.equal(good.ok, good.failures.length === 0);
    const bad = performanceGate([{ suite: 'a', passed: false, budget: null }]);
    assert.equal(bad.ok, bad.failures.length === 0);
  });
});

describe('T10 — artifact manifest (TC-BUILD-055)', () => {
  it('artifactManifest() returns exactly the 7 retained names', () => {
    const { artifactManifest } = loadManifest();
    const result = artifactManifest();
    assert.ok(Array.isArray(result.artifacts));
    assert.deepEqual(result.artifacts, EXPECTED_ARTIFACTS);
  });

  it('dist/artifacts/manifest.json is committed and lists the 7 entries', () => {
    const manifestPath = path.join(__dirname, '..', '..', 'dist', 'artifacts', 'manifest.json');
    const raw = fs.readFileSync(manifestPath, 'utf8');
    const parsed = JSON.parse(raw);
    const list = Array.isArray(parsed) ? parsed : parsed.artifacts;
    assert.deepEqual(list, EXPECTED_ARTIFACTS);
  });

  it('dist/artifacts/README.md exists and maps each entry to its symbolication use', () => {
    const readmePath = path.join(__dirname, '..', '..', 'dist', 'artifacts', 'README.md');
    const readme = fs.readFileSync(readmePath, 'utf8');
    for (const entry of ['release.aab', 'mapping.txt', 'sourcemaps', 'sbom.json', 'provenance.json']) {
      assert.ok(readme.includes(entry), `README must document ${entry}`);
    }
  });
});
