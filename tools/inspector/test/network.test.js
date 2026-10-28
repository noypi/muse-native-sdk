'use strict';
// T3 — FR-DX-003 / AC-DX-012 / TC-DX-012 (Batch 1).
// Hermetic: node:test + node:assert/strict only. The request log is an
// in-memory append-only store inside the unit under test; the redactor
// (`redact(body, rules)`) is injected per call, defaulting to masking
// `authorization`/`token` fields. No network, no device, no secrets.
// Interface under test (plan verbatim):
//   recordRequest(entry) => { id } ;
//   describeRequest(id, redact) => { id, url, method, timings,
//     cacheDecision, body } | throws { code: 'E_INSPECTOR_UNKNOWN_REQUEST', ... }

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'network.js');

function load() {
  delete require.cache[LIB];
  return require(LIB);
}

function entry(overrides = {}) {
  return {
    url: 'https://api.example.test/v1/cards',
    method: 'GET',
    timings: { dnsMs: 4, connectMs: 12, tlsMs: 20, ttfbMs: 60, totalMs: 96 },
    cacheDecision: 'miss',
    body: { data: [1, 2, 3] },
    ...overrides,
  };
}

describe('recordRequest / describeRequest', () => {
  it('records a request and describes it back with timings and cache decision', () => {
    const { recordRequest, describeRequest } = load();
    const { id } = recordRequest(entry());
    assert.equal(typeof id, 'string');
    const out = describeRequest(id);
    assert.equal(out.id, id);
    assert.equal(out.url, 'https://api.example.test/v1/cards');
    assert.equal(out.method, 'GET');
    assert.deepEqual(out.timings, { dnsMs: 4, connectMs: 12, tlsMs: 20, ttfbMs: 60, totalMs: 96 });
    assert.equal(out.cacheDecision, 'miss');
    assert.deepEqual(out.body, { data: [1, 2, 3] });
  });

  it('preserves each cache decision verbatim (hit|miss|revalidate|bypass)', () => {
    const { recordRequest, describeRequest } = load();
    for (const cacheDecision of ['hit', 'miss', 'revalidate', 'bypass']) {
      const { id } = recordRequest(entry({ url: `https://x.test/${cacheDecision}`, cacheDecision }));
      assert.equal(describeRequest(id).cacheDecision, cacheDecision);
    }
  });

  it('masks authorization/token fields in the body by default', () => {
    const { recordRequest, describeRequest } = load();
    const { id } = recordRequest(
      entry({ body: { authorization: 'Bearer live-secret', token: 'abc123', data: 'ok' } }),
    );
    const out = describeRequest(id);
    assert.notEqual(out.body.authorization, 'Bearer live-secret');
    assert.notEqual(out.body.token, 'abc123');
    assert.equal(out.body.data, 'ok');
  });

  it('applies an injected redactor to the body (ASSUMPTION: redact passed as 2nd arg)', () => {
    const { recordRequest, describeRequest } = load();
    const { id } = recordRequest(entry({ body: { ssn: '000-00-0000' } }));
    const out = describeRequest(id, (body) => ({ ...body, ssn: '[REDACTED]' }));
    assert.equal(out.body.ssn, '[REDACTED]');
  });

  it('throws E_INSPECTOR_UNKNOWN_REQUEST for an unknown id', () => {
    const { recordRequest, describeRequest } = load();
    recordRequest(entry());
    assert.throws(
      () => describeRequest('req-does-not-exist'),
      (err) => {
        assert.equal(err.code, 'E_INSPECTOR_UNKNOWN_REQUEST');
        assert.ok(err.cause && typeof err.cause === 'string', 'cause must be present');
        assert.ok(err.action && typeof err.action === 'string', 'action must be present');
        assert.match(String(err.cause) + String(err.action), /DX-003|AC-DX-012|inspector/i);
        return true;
      },
    );
  });

  it('unknown-id error does not leak recorded bodies', () => {
    const { recordRequest, describeRequest } = load();
    recordRequest(entry({ body: { authorization: 'Bearer top-secret-value' } }));
    assert.throws(
      () => describeRequest('req-missing'),
      (err) => {
        assert.doesNotMatch(String(err.cause ?? '') + String(err.message ?? ''), /top-secret-value/);
        return err.code === 'E_INSPECTOR_UNKNOWN_REQUEST';
      },
    );
  });

  it('rejects a malformed entry with an E_INSPECTOR_* error (ASSUMPTION)', () => {
    const { recordRequest } = load();
    assert.throws(
      () => recordRequest({ url: 'not-a-url' }),
      (err) => {
        assert.match(String(err.code), /^E_INSPECTOR_/);
        return true;
      },
    );
  });
});
