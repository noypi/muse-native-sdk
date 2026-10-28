'use strict';
// T8 — Upload gate (NFR-BUILD-015 / AC-BUILD-051, TC-BUILD-051).
// Hermetic: node:test + node:assert/strict only; in-memory inputs, no I/O,
// no network, no secrets, no live services.
// Red state: ./uploadGate.js does not exist yet, so every case below fails
// with MODULE_NOT_FOUND until T8 lands.
//
// Interface under test (plan verbatim):
//   uploadGate({signed:boolean, debugSigned:boolean, reproducible:boolean}): {ok:boolean, reasons:string[]}
// Unsigned OR debug-signed OR non-reproducible fails with named reasons;
// only signed + non-debug + reproducible passes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./uploadGate.js');
}

describe('T8 — uploadGate contract shape', () => {
  it('returns { ok:boolean, reasons:string[] } on the green path', () => {
    const { uploadGate } = loadImpl();
    const result = uploadGate({ signed: true, debugSigned: false, reproducible: true });
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.reasons));
    assert.equal(result.ok, true);
    assert.deepEqual(result.reasons, []);
  });
});

describe('T8 — green path (TC-BUILD-051)', () => {
  it('signed + non-debug + reproducible passes with no reasons', () => {
    const { uploadGate } = loadImpl();
    const result = uploadGate({ signed: true, debugSigned: false, reproducible: true });
    assert.equal(result.ok, true);
    assert.deepEqual(result.reasons, []);
  });
});

describe('T8 — refusal legs', () => {
  it('unsigned build fails naming the signature leg', () => {
    const { uploadGate } = loadImpl();
    const result = uploadGate({ signed: false, debugSigned: false, reproducible: true });
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length > 0);
    assert.ok(result.reasons.some((r) => /sign/i.test(r)), `must name signing, got: ${JSON.stringify(result.reasons)}`);
  });

  it('debug-signed build fails naming the debug leg even when signed', () => {
    // A debug-signed release masquerading as signed must still be refused.
    const { uploadGate } = loadImpl();
    const result = uploadGate({ signed: true, debugSigned: true, reproducible: true });
    assert.equal(result.ok, false);
    assert.ok(result.reasons.some((r) => /debug/i.test(r)), `must name debug signing, got: ${JSON.stringify(result.reasons)}`);
  });

  it('non-reproducible build fails naming the reproducibility leg', () => {
    const { uploadGate } = loadImpl();
    const result = uploadGate({ signed: true, debugSigned: false, reproducible: false });
    assert.equal(result.ok, false);
    assert.ok(result.reasons.some((r) => /reproduc/i.test(r)), `must name reproducibility, got: ${JSON.stringify(result.reasons)}`);
  });

  it('combined refusal names every failing leg', () => {
    const { uploadGate } = loadImpl();
    const result = uploadGate({ signed: false, debugSigned: true, reproducible: false });
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length >= 3, `must name all three legs, got: ${JSON.stringify(result.reasons)}`);
  });

  it('ok is false iff reasons is non-empty', () => {
    const { uploadGate } = loadImpl();
    const good = uploadGate({ signed: true, debugSigned: false, reproducible: true });
    assert.equal(good.ok, good.reasons.length === 0);
    const bad = uploadGate({ signed: false, debugSigned: false, reproducible: true });
    assert.equal(bad.ok, bad.reasons.length === 0);
    assert.ok(bad.reasons.every((r) => typeof r === 'string'));
  });
});
