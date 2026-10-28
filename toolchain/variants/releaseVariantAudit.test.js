'use strict';
// T10 — release variant audit gate (NFR-BUILD-018 / AC-BUILD-067).
// Hermetic: node:test + node:assert/strict only. No network, no secrets,
// no live services, never real binaries (artifact is an in-memory descriptor).
// Red state: ./releaseVariantAudit.js does not exist yet, so every case below
// fails (MODULE_NOT_FOUND) until T10 lands.
//
// Interface under test (plan verbatim):
//   auditReleaseVariant(artifact: { validationLayerSymbols: string[], debugMarkers: boolean }): { ok: boolean, violations: string[] }
// Any validation-layer symbol or debug marker present is a violation naming
// the symbol.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./releaseVariantAudit.js');
}

describe('T10 — auditReleaseVariant contract shape', () => {
  it('returns { ok: boolean, violations: string[] } on a clean artifact', () => {
    const { auditReleaseVariant } = loadImpl();
    const result = auditReleaseVariant({ validationLayerSymbols: [], debugMarkers: false });
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T10 — clean release artifact passes (AC-BUILD-067)', () => {
  it('no validation symbols and no debug markers passes', () => {
    const { auditReleaseVariant } = loadImpl();
    const result = auditReleaseVariant({ validationLayerSymbols: [], debugMarkers: false });
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T10 — validation-layer symbols are violations (AC-BUILD-067)', () => {
  it('one validation-layer symbol fails naming that symbol', () => {
    const { auditReleaseVariant } = loadImpl();
    const result = auditReleaseVariant({
      validationLayerSymbols: ['vkCreateValidationCacheEXT'],
      debugMarkers: false,
    });
    assert.equal(result.ok, false);
    const named = result.violations.some((v) => v.includes('vkCreateValidationCacheEXT'));
    assert.ok(
      named,
      `a violation must name the symbol, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('each of several validation-layer symbols is named', () => {
    const { auditReleaseVariant } = loadImpl();
    const result = auditReleaseVariant({
      validationLayerSymbols: ['vkCreateValidationCacheEXT', 'vkDebugMarkerSetObjectNameEXT'],
      debugMarkers: false,
    });
    assert.equal(result.ok, false);
    for (const sym of ['vkCreateValidationCacheEXT', 'vkDebugMarkerSetObjectNameEXT']) {
      assert.ok(
        result.violations.some((v) => v.includes(sym)),
        `a violation must name ${sym}, got: ${JSON.stringify(result.violations)}`
      );
    }
  });
});

describe('T10 — debug markers are violations (AC-BUILD-067)', () => {
  it('debug markers present fail naming the marker', () => {
    const { auditReleaseVariant } = loadImpl();
    const result = auditReleaseVariant({ validationLayerSymbols: [], debugMarkers: true });
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
    const named = result.violations.some((v) => /debug/i.test(v));
    assert.ok(
      named,
      `a violation must name the debug marker, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a symbol plus a debug marker both fail (violations name each cause)', () => {
    const { auditReleaseVariant } = loadImpl();
    const result = auditReleaseVariant({
      validationLayerSymbols: ['vkCreateValidationCacheEXT'],
      debugMarkers: true,
    });
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => v.includes('vkCreateValidationCacheEXT')),
      `must name the symbol, got: ${JSON.stringify(result.violations)}`
    );
    assert.ok(
      result.violations.some((v) => /debug/i.test(v)),
      `must name the debug marker, got: ${JSON.stringify(result.violations)}`
    );
  });
});

describe('T10 — boundary and fail-closed behavior', () => {
  it('a missing artifact descriptor fails closed, never throws', () => {
    // ASSUMPTION: a gate that cannot inspect the artifact fails closed.
    const { auditReleaseVariant } = loadImpl();
    let result;
    assert.doesNotThrow(() => {
      result = auditReleaseVariant(undefined);
    });
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
  });
});
