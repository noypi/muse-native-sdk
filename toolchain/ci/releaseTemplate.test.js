'use strict';
// T11 — Release-template end-to-end gate (NFR-BUILD-015 / AC-BUILD-056, TC-BUILD-056).
// Hermetic: node:test + node:assert/strict only; in-memory inputs, no imports
// of sibling gates (composition via injected result objects), no I/O,
// no network, no secrets, no live services.
// Red state: ./releaseTemplate.js does not exist yet, so every case below
// fails with MODULE_NOT_FOUND until T11 lands.
// AC-BUILD-053 / TC-BUILD-053 (staged rollout + crash/ANR halt via Play
// console) is explicitly out of scope: external, report-only, no test here.
//
// Interface under test (plan verbatim):
//   releaseTemplate({upload:{signed:boolean, debugSigned:boolean, reproducible:boolean}, compliance:object, perf:{suite:string, passed:boolean}[], provenance:{artifactHash:string, attestedHash:string}}): {ok:boolean, stage:string|null, reasons:string[]}
// Evaluates upload → compliance → perf → provenance in order, short-circuits
// at the first failing stage naming it; tampered artifact
// (artifactHash !== attestedHash) fails at provenance.
//
// ASSUMPTIONS (contract silent on the injected shapes):
//   B1. compliance passes iff { ok:true } (violations array read for reasons
//       when present); any truthy-ok object passes.
//   B2. perf passes iff every entry has passed === true on a non-empty array;
//       an empty array fails at stage perf.
//   B3. provenance passes iff artifactHash and attestedHash are non-empty
//       equal strings; mismatch or missing hash fails at stage provenance.
//   B4. On success, stage is null and reasons is []; on failure, stage names
//       the first failing stage and reasons is non-empty.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./releaseTemplate.js');
}

function greenArgs(overrides = {}) {
  return {
    upload: { signed: true, debugSigned: false, reproducible: true },
    compliance: { ok: true, violations: [] },
    perf: [{ suite: 'startup', passed: true }],
    provenance: { artifactHash: 'sha256:abc123', attestedHash: 'sha256:abc123' },
    ...overrides,
  };
}

describe('T11 — releaseTemplate contract shape', () => {
  it('returns { ok:boolean, stage:null, reasons:[] } on the clean-repo path', () => {
    const { releaseTemplate } = loadImpl();
    const result = releaseTemplate(greenArgs());
    assert.equal(typeof result.ok, 'boolean');
    assert.equal(result.ok, true);
    assert.equal(result.stage, null);
    assert.deepEqual(result.reasons, []);
  });
});

describe('T11 — clean-repo green path (TC-BUILD-056)', () => {
  it('fully green inputs pass with a null stage', () => {
    const { releaseTemplate } = loadImpl();
    const result = releaseTemplate(greenArgs());
    assert.equal(result.ok, true);
    assert.equal(result.stage, null);
    assert.deepEqual(result.reasons, []);
  });
});

describe('T11 — stage short-circuit in order', () => {
  it('unsigned upload fails at stage upload', () => {
    const { releaseTemplate } = loadImpl();
    const result = releaseTemplate(
      greenArgs({ upload: { signed: false, debugSigned: false, reproducible: true } })
    );
    assert.equal(result.ok, false);
    assert.equal(result.stage, 'upload');
    assert.ok(result.reasons.length > 0);
  });

  it('failing compliance fails at stage compliance when upload passes', () => {
    const { releaseTemplate } = loadImpl();
    const result = releaseTemplate(
      greenArgs({ compliance: { ok: false, violations: ['targetSdk below enforced level'] } })
    );
    assert.equal(result.ok, false);
    assert.equal(result.stage, 'compliance');
    assert.ok(result.reasons.length > 0);
  });

  it('failed perf suite fails at stage perf when earlier stages pass', () => {
    const { releaseTemplate } = loadImpl();
    const result = releaseTemplate(
      greenArgs({ perf: [{ suite: 'scroll-jank', passed: false }] })
    );
    assert.equal(result.ok, false);
    assert.equal(result.stage, 'perf');
    assert.ok(result.reasons.length > 0);
  });

  it('tampered artifact (hash mismatch) fails at stage provenance', () => {
    const { releaseTemplate } = loadImpl();
    const result = releaseTemplate(
      greenArgs({ provenance: { artifactHash: 'sha256:tampered', attestedHash: 'sha256:abc123' } })
    );
    assert.equal(result.ok, false);
    assert.equal(result.stage, 'provenance');
    assert.ok(result.reasons.length > 0);
  });

  it('short-circuits at the first failing stage when several fail', () => {
    const { releaseTemplate } = loadImpl();
    const result = releaseTemplate(
      greenArgs({
        upload: { signed: false, debugSigned: true, reproducible: false },
        compliance: { ok: false, violations: ['stale target'] },
        perf: [{ suite: 'x', passed: false }],
        provenance: { artifactHash: 'a', attestedHash: 'b' },
      })
    );
    assert.equal(result.ok, false);
    assert.equal(result.stage, 'upload');
  });
});
