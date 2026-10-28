'use strict';
// T9 — Consolidated release compliance gate (NFR-BUILD-015 / AC-BUILD-052, TC-BUILD-052).
// Hermetic: node:test + node:assert/strict only; in-memory inputs, no I/O,
// no network, no secrets, no live services.
// Red state: ./releaseComplianceGate.js does not exist yet, so every case
// below fails with MODULE_NOT_FOUND until T9 lands.
//
// Interface under test (plan verbatim):
//   releaseCompliance({targetSdk:number, enforcedTargetSdk:number, manifest:{predictiveBack:boolean, foregroundServiceTypes:boolean}, nativeLibs:{path:string, elfAligned16k:boolean}[]}): {ok:boolean, violations:string[]}
// Each failing leg (stale target, missing back opt-in, missing service types,
// any unaligned lib) fails naming that leg; empty lib list fails closed.
//
// ASSUMPTIONS (contract silent):
//   A1. Stale target = targetSdk < enforcedTargetSdk; equal or newer passes.
//   A2. Missing manifest object or falsy flags fail naming that declaration.
//   A3. Only boolean true passes for elfAligned16k; any other value fails
//       naming the lib path (or its index when path is missing).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./releaseComplianceGate.js');
}

function greenInput(overrides = {}) {
  return {
    targetSdk: 35,
    enforcedTargetSdk: 35,
    manifest: { predictiveBack: true, foregroundServiceTypes: true },
    nativeLibs: [{ path: 'lib/arm64-v8a/libmuse.so', elfAligned16k: true }],
    ...overrides,
  };
}

describe('T9 — releaseCompliance contract shape', () => {
  it('returns { ok:boolean, violations:string[] } on a compliant input', () => {
    const { releaseCompliance } = loadImpl();
    const result = releaseCompliance(greenInput());
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T9 — all-green pass (TC-BUILD-052)', () => {
  it('current target + manifest opt-ins + aligned lib passes', () => {
    const { releaseCompliance } = loadImpl();
    const result = releaseCompliance(greenInput());
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });

  it('a targetSdk above the enforced level stays passing', () => {
    const { releaseCompliance } = loadImpl();
    const result = releaseCompliance(greenInput({ targetSdk: 36 }));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T9 — failing legs', () => {
  it('stale targetSdk fails naming the target leg', () => {
    const { releaseCompliance } = loadImpl();
    const result = releaseCompliance(greenInput({ targetSdk: 34 }));
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => /target/i.test(v)),
      `must name the target leg, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('missing predictive-back opt-in fails naming it', () => {
    const { releaseCompliance } = loadImpl();
    const result = releaseCompliance(
      greenInput({ manifest: { predictiveBack: false, foregroundServiceTypes: true } })
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => /predictive|back/i.test(v)),
      `must name predictive-back, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('missing foreground-service-types fails naming it', () => {
    const { releaseCompliance } = loadImpl();
    const result = releaseCompliance(
      greenInput({ manifest: { predictiveBack: true, foregroundServiceTypes: false } })
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => /foreground|service/i.test(v)),
      `must name foreground-service-types, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('an unaligned native lib fails naming its path', () => {
    const { releaseCompliance } = loadImpl();
    const result = releaseCompliance(
      greenInput({ nativeLibs: [{ path: 'lib/arm64-v8a/libmuse.so', elfAligned16k: false }] })
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => v.includes('lib/arm64-v8a/libmuse.so') && /16|align/i.test(v)),
      `must name the lib path and alignment, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('empty native-lib list fails closed', () => {
    const { releaseCompliance } = loadImpl();
    const result = releaseCompliance(greenInput({ nativeLibs: [] }));
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0);
  });

  it('ok is false iff violations is non-empty and every violation is a string', () => {
    const { releaseCompliance } = loadImpl();
    const good = releaseCompliance(greenInput());
    assert.equal(good.ok, good.violations.length === 0);
    const bad = releaseCompliance(greenInput({ targetSdk: 33 }));
    assert.equal(bad.ok, bad.violations.length === 0);
    assert.ok(bad.violations.every((v) => typeof v === 'string'));
  });
});
