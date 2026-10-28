'use strict';
// T6 — Baseline accept gate: non-empty profile + measured improvement + fresh
// (NFR-BUILD-006 / AC-BUILD-010; composes T4 + T5 plus non-empty rules — all
// inputs below are synthetic in-memory doubles, no T4/T5 module imports).
// Hermetic: node:test + node:assert/strict only. Pure in-memory inputs; no
// filesystem, no network, no secrets, no live services.
// Red state: ./acceptGate.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND via loadImpl) until the coder lands it.
//
// Interface under test (plan verbatim):
//   acceptRelease(profile: {rules:string, sourceStamp:number}, sources: {stamp:number}, baselineMs: number[], candidateMs: number[]): {ok:boolean, reasons:string[]}
// composes T4 + T5 plus non-empty rules; ok:true only when all three pass;
// reasons name every failing leg.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./acceptGate.js');
}

function goodArgs() {
  return {
    profile: { rules: 'HSPLhs/bootstrap\nHSPLhs/hermes-isolate', sourceStamp: 200 },
    sources: { stamp: 100 },
    baselineMs: [200, 210, 220],
    candidateMs: [150, 160, 170],
  };
}

describe('T6 — contract shape (AC-BUILD-010)', () => {
  it('returns { ok: boolean, reasons: string[] } for a fully passing release', () => {
    const { acceptRelease } = loadImpl();
    const { profile, sources, baselineMs, candidateMs } = goodArgs();
    const result = acceptRelease(profile, sources, baselineMs, candidateMs);
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.reasons));
    assert.equal(result.ok, true);
    assert.deepEqual(result.reasons, []);
  });
});

describe('T6 — all three legs green passes (AC-BUILD-010)', () => {
  it('fresh non-empty improved profile passes', () => {
    const { acceptRelease } = loadImpl();
    const { profile, sources, baselineMs, candidateMs } = goodArgs();
    assert.equal(acceptRelease(profile, sources, baselineMs, candidateMs).ok, true);
  });
});

describe('T6 — each failing leg fails naming that leg', () => {
  it('empty rules fail naming the rules leg', () => {
    const { acceptRelease } = loadImpl();
    const { sources, baselineMs, candidateMs } = goodArgs();
    const result = acceptRelease(
      { rules: '', sourceStamp: 200 },
      sources,
      baselineMs,
      candidateMs
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => /rule/i.test(r)),
      `reasons must name the rules leg, got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('whitespace-only rules fail naming the rules leg', () => {
    // ASSUMPTION: blank rules are equivalent to empty — no verifiable profile.
    const { acceptRelease } = loadImpl();
    const { sources, baselineMs, candidateMs } = goodArgs();
    const result = acceptRelease(
      { rules: '   \n  ', sourceStamp: 200 },
      sources,
      baselineMs,
      candidateMs
    );
    assert.equal(result.ok, false);
    assert.ok(result.reasons.some((r) => /rule/i.test(r)));
  });

  it('missing rules fail naming the rules leg', () => {
    const { acceptRelease } = loadImpl();
    const { sources, baselineMs, candidateMs } = goodArgs();
    const result = acceptRelease(
      { sourceStamp: 200 },
      sources,
      baselineMs,
      candidateMs
    );
    assert.equal(result.ok, false);
    assert.ok(result.reasons.some((r) => /rule/i.test(r)));
  });

  it('stale stamp fails naming the staleness leg', () => {
    const { acceptRelease } = loadImpl();
    const { sources, baselineMs, candidateMs } = goodArgs();
    const result = acceptRelease(
      { rules: 'HSPLhs/bootstrap', sourceStamp: 50 },
      sources,
      baselineMs,
      candidateMs
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => /stal|stamp|fresh/i.test(r)),
      `reasons must name the staleness leg, got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('non-improving delta fails naming the benchmark leg', () => {
    const { acceptRelease } = loadImpl();
    const { profile, sources } = goodArgs();
    const result = acceptRelease(
      profile,
      sources,
      [150, 160, 170],
      [200, 210, 220]
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => /benchmark|startup|delta|improv|median|regress/i.test(r)),
      `reasons must name the benchmark leg, got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('equal medians fail (no strict improvement)', () => {
    const { acceptRelease } = loadImpl();
    const { profile, sources } = goodArgs();
    const result = acceptRelease(
      profile,
      sources,
      [200, 200, 200],
      [200, 200, 200]
    );
    assert.equal(result.ok, false);
  });

  it('combined failures name every failing leg', () => {
    const { acceptRelease } = loadImpl();
    const { sources } = goodArgs();
    const result = acceptRelease(
      { rules: '', sourceStamp: 50 },
      sources,
      [150, 160],
      [250, 260]
    );
    assert.equal(result.ok, false);
    assert.ok(result.reasons.some((r) => /rule/i.test(r)), `got: ${JSON.stringify(result.reasons)}`);
    assert.ok(
      result.reasons.some((r) => /stal|stamp|fresh/i.test(r)),
      `got: ${JSON.stringify(result.reasons)}`
    );
    assert.ok(
      result.reasons.some((r) => /benchmark|startup|delta|improv|median|regress/i.test(r)),
      `got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('invalid benchmark inputs fail naming the benchmark leg', () => {
    const { acceptRelease } = loadImpl();
    const { profile, sources } = goodArgs();
    const result = acceptRelease(profile, sources, [], [150, 160]);
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length > 0);
  });

  it('missing sources stamp fails closed', () => {
    const { acceptRelease } = loadImpl();
    const { profile, baselineMs, candidateMs } = goodArgs();
    const result = acceptRelease(profile, {}, baselineMs, candidateMs);
    assert.equal(result.ok, false);
  });
});
