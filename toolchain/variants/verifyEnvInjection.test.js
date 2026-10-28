'use strict';
// T12 — per-flavor environment injection gate (NFR-BUILD-018 / AC-BUILD-069).
// Hermetic: node:test + node:assert/strict only. Every flavor value below is
// an obviously-fake placeholder (`.test.invalid` / `not-real` markers) — no
// real credentials anywhere in these fixtures. No network, no `.env` reads,
// no live services. Red state: ./verifyEnvInjection.js does not exist yet, so
// every case below fails (MODULE_NOT_FOUND) until T12 lands.
//
// Interface under test (plan verbatim):
//   verifyEnvInjection(flavor: { name: string, values: Record<string, string>, committedSecrets: string[] }): { ok: boolean, violations: string[] }
// Any production secret in the committed set is a violation; every flavor
// resolves its endpoints/keys.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./verifyEnvInjection.js');
}

// Obviously-fake fixtures — never real credentials.
const DEV_VALUES = {
  API_ENDPOINT: 'https://api-dev.test.invalid',
  API_KEY: 'dev-api-key-not-real',
};
const STAGING_VALUES = {
  API_ENDPOINT: 'https://api-staging.test.invalid',
  API_KEY: 'staging-api-key-not-real',
};
const PROD_VALUES = {
  API_ENDPOINT: 'https://api-prod.test.invalid',
  API_KEY: 'prod-api-key-not-real-0000',
};

describe('T12 — verifyEnvInjection contract shape', () => {
  it('returns { ok: boolean, violations: string[] } for a clean dev flavor', () => {
    const { verifyEnvInjection } = loadImpl();
    const result = verifyEnvInjection({ name: 'dev', values: DEV_VALUES, committedSecrets: [] });
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T12 — clean flavors pass (AC-BUILD-069)', () => {
  it('dev flavor with resolved endpoints/keys and no committed secrets passes', () => {
    const { verifyEnvInjection } = loadImpl();
    const result = verifyEnvInjection({ name: 'dev', values: DEV_VALUES, committedSecrets: [] });
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });

  it('staging flavor with resolved endpoints/keys and no committed secrets passes', () => {
    const { verifyEnvInjection } = loadImpl();
    const result = verifyEnvInjection({
      name: 'staging',
      values: STAGING_VALUES,
      committedSecrets: [],
    });
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });

  it('production flavor resolves from injection with nothing committed passes', () => {
    const { verifyEnvInjection } = loadImpl();
    const result = verifyEnvInjection({
      name: 'production',
      values: PROD_VALUES,
      committedSecrets: [],
    });
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T12 — committed production secrets are violations (AC-BUILD-069)', () => {
  it('a committed production secret fails naming the key', () => {
    const { verifyEnvInjection } = loadImpl();
    const result = verifyEnvInjection({
      name: 'production',
      values: PROD_VALUES,
      committedSecrets: ['PROD_API_KEY'],
    });
    assert.equal(result.ok, false);
    const named = result.violations.some((v) => v.includes('PROD_API_KEY'));
    assert.ok(
      named,
      `a violation must name the committed key, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('each committed secret is named', () => {
    const { verifyEnvInjection } = loadImpl();
    const result = verifyEnvInjection({
      name: 'production',
      values: PROD_VALUES,
      committedSecrets: ['PROD_API_KEY', 'PROD_DB_PASSWORD'],
    });
    assert.equal(result.ok, false);
    for (const key of ['PROD_API_KEY', 'PROD_DB_PASSWORD']) {
      assert.ok(
        result.violations.some((v) => v.includes(key)),
        `a violation must name ${key}, got: ${JSON.stringify(result.violations)}`
      );
    }
  });
});

describe('T12 — unresolved flavor values fail (AC-BUILD-069)', () => {
  it('a flavor missing its endpoint fails naming the missing key', () => {
    // ASSUMPTION: every flavor must resolve its endpoints/keys — an empty or
    // missing value is a violation naming the key.
    const { verifyEnvInjection } = loadImpl();
    const result = verifyEnvInjection({
      name: 'dev',
      values: { API_ENDPOINT: '', API_KEY: 'dev-api-key-not-real' },
      committedSecrets: [],
    });
    assert.equal(result.ok, false);
    const named = result.violations.some((v) => v.includes('API_ENDPOINT'));
    assert.ok(
      named,
      `a violation must name the unresolved key, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a missing flavor descriptor fails closed, never throws', () => {
    // ASSUMPTION: a gate that cannot read the flavor fails closed.
    const { verifyEnvInjection } = loadImpl();
    let result;
    assert.doesNotThrow(() => {
      result = verifyEnvInjection(undefined);
    });
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
  });
});
