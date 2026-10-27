'use strict';
// T3 — Final-artifact alignment gate over the shipped artifact set (AC-BUILD-018).
// Hermetic: node:test + node:assert/strict only (pure-data gate, no fs, no
// network, no secrets, no live services). Covers the base APK + every dynamic
// feature module + every prebuilt addon (final artifact, not engine tree).
// Red state: ./artifactAlignmentGate.js does not exist yet, so every case
// below fails (MODULE_NOT_FOUND) until the coder lands the gate.
//
// Interface under test (plan verbatim):
//   checkArtifactAlignment(artifact: { baseApk: string[], featureModules: string[][], addons: string[][] }): { ok: boolean, violations: string[] } — inputs are per-`.so` alignment booleans keyed by name; violations name the failing module and file.
//
// ASSUMPTION (entry encoding — the plan types each entry as string while
// describing "booleans keyed by name"; the tests below pin the string encoding
// so the gate is implementable): each entry is "<name>.so:<flag>" where flag
// "1" means 16 KB-aligned and "0" means misaligned, e.g. "libmuse.so:1".
// `featureModules`/`addons` group entries per module: each inner array is one
// module's entries, and violations name the failing module index and file.
// A failure in any module blocks release (ok:false naming module+file).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./artifactAlignmentGate.js');
}

const GOOD = 'libmuse.so:1';
const BAD = 'libbad.so:0';

describe('checkArtifactAlignment', () => {
  it('fully aligned artifact yields ok:true', () => {
    const { checkArtifactAlignment } = loadImpl();
    assert.deepEqual(
      checkArtifactAlignment({
        baseApk: [GOOD],
        featureModules: [[GOOD]],
        addons: [[GOOD]],
      }),
      { ok: true, violations: [] }
    );
  });

  it('misaligned .so in the base APK blocks release naming the file', () => {
    const { checkArtifactAlignment } = loadImpl();
    const result = checkArtifactAlignment({
      baseApk: [GOOD, BAD],
      featureModules: [],
      addons: [],
    });
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => v.includes('libbad.so')),
      `violations must name libbad.so, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('failure in any feature module blocks release naming module and file', () => {
    const { checkArtifactAlignment } = loadImpl();
    const result = checkArtifactAlignment({
      baseApk: [GOOD],
      featureModules: [[GOOD], [BAD]],
      addons: [],
    });
    assert.equal(result.ok, false);
    const hit = result.violations.find((v) => v.includes('libbad.so'));
    assert.ok(hit, `violations must name libbad.so, got: ${JSON.stringify(result.violations)}`);
    assert.ok(
      /module/i.test(hit) || hit.includes('1'),
      `violation must name the failing module, got: ${JSON.stringify(hit)}`
    );
  });

  it('misaligned prebuilt addon blocks release naming the file', () => {
    const { checkArtifactAlignment } = loadImpl();
    const result = checkArtifactAlignment({
      baseApk: [GOOD],
      featureModules: [],
      addons: [[BAD]],
    });
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => v.includes('libbad.so')),
      `violations must name libbad.so, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('empty artifact set yields ok:true', () => {
    // ASSUMPTION: the plan is silent on the empty set; the most defensible
    // behavior is vacuous pass (nothing misaligned).
    const { checkArtifactAlignment } = loadImpl();
    assert.deepEqual(
      checkArtifactAlignment({ baseApk: [], featureModules: [], addons: [] }),
      { ok: true, violations: [] }
    );
  });
});
