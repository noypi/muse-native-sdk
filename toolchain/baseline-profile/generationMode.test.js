'use strict';
// T3 — Profile generation requires release-mode R8-minified build
// (NFR-BUILD-006 / AC-BUILD-007).
// Hermetic: node:test + node:assert/strict only. Pure in-memory inputs; no
// filesystem, no network, no secrets, no live services.
// Red state: ./generationMode.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND via loadImpl) until the coder lands it.
//
// Interface under test (plan verbatim):
//   generationAllowed(build: {mode:string, minified:boolean}): {ok:boolean, reason:string}
// ok:true only when mode === 'release' && minified === true; otherwise
// ok:false with reason naming the offending field.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./generationMode.js');
}

describe('T3 — contract shape (AC-BUILD-007)', () => {
  it('returns { ok: boolean, reason: string } for release+minified', () => {
    const { generationAllowed } = loadImpl();
    const result = generationAllowed({ mode: 'release', minified: true });
    assert.equal(typeof result.ok, 'boolean');
    assert.equal(typeof result.reason, 'string');
    assert.equal(result.ok, true);
  });
});

describe('T3 — release+minified passes (AC-BUILD-007)', () => {
  it('release mode with minified true is allowed', () => {
    const { generationAllowed } = loadImpl();
    const result = generationAllowed({ mode: 'release', minified: true });
    assert.equal(result.ok, true);
  });
});

describe('T3 — non-release or non-minified fails naming the field', () => {
  it('debug build fails with a reason naming mode', () => {
    const { generationAllowed } = loadImpl();
    const result = generationAllowed({ mode: 'debug', minified: true });
    assert.equal(result.ok, false);
    assert.match(result.reason, /mode/i);
  });

  it('release non-minified build fails with a reason naming minified', () => {
    const { generationAllowed } = loadImpl();
    const result = generationAllowed({ mode: 'release', minified: false });
    assert.equal(result.ok, false);
    assert.match(result.reason, /minified/i);
  });

  it('debug non-minified build fails', () => {
    const { generationAllowed } = loadImpl();
    const result = generationAllowed({ mode: 'debug', minified: false });
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('missing mode field fails closed naming mode', () => {
    const { generationAllowed } = loadImpl();
    const result = generationAllowed({ minified: true });
    assert.equal(result.ok, false);
    assert.match(result.reason, /mode/i);
  });

  it('missing minified field fails closed naming minified', () => {
    const { generationAllowed } = loadImpl();
    const result = generationAllowed({ mode: 'release' });
    assert.equal(result.ok, false);
    assert.match(result.reason, /minified/i);
  });

  it('empty build object fails closed', () => {
    const { generationAllowed } = loadImpl();
    const result = generationAllowed({});
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('non-string mode fails closed', () => {
    // ASSUMPTION: mode must be the exact string 'release'; truthy
    // non-strings (1, true) must not pass.
    const { generationAllowed } = loadImpl();
    assert.equal(generationAllowed({ mode: 1, minified: true }).ok, false);
    assert.equal(generationAllowed({ mode: true, minified: true }).ok, false);
  });

  it('truthy non-boolean minified fails closed', () => {
    // ASSUMPTION: minified must be boolean true, not 'true'/1.
    const { generationAllowed } = loadImpl();
    assert.equal(generationAllowed({ mode: 'release', minified: 'true' }).ok, false);
    assert.equal(generationAllowed({ mode: 'release', minified: 1 }).ok, false);
  });

  it('mode match is case-sensitive: Release fails', () => {
    // ASSUMPTION: exact 'release' only; 'Release'/'RELEASE' are not release.
    const { generationAllowed } = loadImpl();
    const result = generationAllowed({ mode: 'Release', minified: true });
    assert.equal(result.ok, false);
    assert.match(result.reason, /mode/i);
  });
});
