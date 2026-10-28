'use strict';
// T1 — Cache-key computation gate (poisoned-cache safety)
// (NFR-BUILD-011 / AC-BUILD-031; repoints TC-BUILD-031).
// Hermetic: node:test + node:assert/strict only. Pure in-memory synthetic
// inputs; no filesystem, no network, no secrets, no live services.
// Red state: ./cacheKey.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND via loadImpl) until the coder lands it.
//
// Interface under test (plan verbatim):
//   computeCacheKey({ndkVersion:string, cflags:string[], cmakeHash:string, generatedHash:string}): {key:string, components:{ndkVersion:string, cflags:string[], cmakeHash:string, generatedHash:string}} — throws Error('cache-key: <field> missing') fail-closed on any absent/empty field; any input change yields a different key.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./cacheKey.js');
}

function greenArgs() {
  return {
    ndkVersion: '27.2.12479018',
    cflags: ['-O2', '-flto=thin'],
    cmakeHash: 'cmake-hash-fixture-001',
    generatedHash: 'generated-hash-fixture-001',
  };
}

describe('T1 — contract shape (AC-BUILD-031)', () => {
  it('returns { key: string, components: {...} } echoing the inputs', () => {
    const { computeCacheKey } = loadImpl();
    const args = greenArgs();
    const result = computeCacheKey(args);
    assert.equal(typeof result.key, 'string');
    assert.ok(result.key.length > 0);
    assert.deepEqual(result.components.ndkVersion, args.ndkVersion);
    assert.deepEqual(result.components.cflags, [...args.cflags].sort());
    assert.deepEqual(result.components.cmakeHash, args.cmakeHash);
    assert.deepEqual(result.components.generatedHash, args.generatedHash);
  });

  it('is deterministic: same input yields the same key', () => {
    const { computeCacheKey } = loadImpl();
    assert.equal(computeCacheKey(greenArgs()).key, computeCacheKey(greenArgs()).key);
  });

  it('is order-insensitive over cflags (sorted canonical form)', () => {
    // ASSUMPTION: plan says "sha256 over canonical JSON of components
    // (sorted cflags)", so flag reorder must not change the key.
    const { computeCacheKey } = loadImpl();
    const a = greenArgs();
    const b = greenArgs();
    b.cflags = [...b.cflags].reverse();
    assert.equal(computeCacheKey(a).key, computeCacheKey(b).key);
  });
});

describe('T1 — input sensitivity: any input change yields a different key (AC-BUILD-031)', () => {
  it('changed ndkVersion yields a different key', () => {
    const { computeCacheKey } = loadImpl();
    const base = computeCacheKey(greenArgs()).key;
    const altered = greenArgs();
    altered.ndkVersion = '26.1.10909125';
    assert.notEqual(computeCacheKey(altered).key, base);
  });

  it('changed cflags yields a different key', () => {
    const { computeCacheKey } = loadImpl();
    const base = computeCacheKey(greenArgs()).key;
    const altered = greenArgs();
    altered.cflags = ['-O2', '-fno-lto'];
    assert.notEqual(computeCacheKey(altered).key, base);
  });

  it('added cflag yields a different key', () => {
    const { computeCacheKey } = loadImpl();
    const base = computeCacheKey(greenArgs()).key;
    const altered = greenArgs();
    altered.cflags = [...altered.cflags, '-g0'];
    assert.notEqual(computeCacheKey(altered).key, base);
  });

  it('changed cmakeHash yields a different key', () => {
    const { computeCacheKey } = loadImpl();
    const base = computeCacheKey(greenArgs()).key;
    const altered = greenArgs();
    altered.cmakeHash = 'cmake-hash-fixture-002';
    assert.notEqual(computeCacheKey(altered).key, base);
  });

  it('changed generatedHash yields a different key', () => {
    const { computeCacheKey } = loadImpl();
    const base = computeCacheKey(greenArgs()).key;
    const altered = greenArgs();
    altered.generatedHash = 'generated-hash-fixture-002';
    assert.notEqual(computeCacheKey(altered).key, base);
  });
});

describe('T1 — fail-closed throws Error(cache-key: <field> missing) (AC-BUILD-031)', () => {
  it('missing ndkVersion throws naming the field', () => {
    const { computeCacheKey } = loadImpl();
    const args = greenArgs();
    delete args.ndkVersion;
    assert.throws(() => computeCacheKey(args), /cache-key: ndkVersion missing/);
  });

  it('empty ndkVersion throws naming the field', () => {
    const { computeCacheKey } = loadImpl();
    const args = greenArgs();
    args.ndkVersion = '';
    assert.throws(() => computeCacheKey(args), /cache-key: ndkVersion missing/);
  });

  it('missing cflags throws naming the field', () => {
    const { computeCacheKey } = loadImpl();
    const args = greenArgs();
    delete args.cflags;
    assert.throws(() => computeCacheKey(args), /cache-key: cflags missing/);
  });

  it('empty cflags array throws naming the field', () => {
    // ASSUMPTION: "absent/empty field" covers an empty cflags list — an
    // empty flag set must not share a key with a pinned set.
    const { computeCacheKey } = loadImpl();
    const args = greenArgs();
    args.cflags = [];
    assert.throws(() => computeCacheKey(args), /cache-key: cflags missing/);
  });

  it('missing cmakeHash throws naming the field', () => {
    const { computeCacheKey } = loadImpl();
    const args = greenArgs();
    delete args.cmakeHash;
    assert.throws(() => computeCacheKey(args), /cache-key: cmakeHash missing/);
  });

  it('empty cmakeHash throws naming the field', () => {
    const { computeCacheKey } = loadImpl();
    const args = greenArgs();
    args.cmakeHash = '';
    assert.throws(() => computeCacheKey(args), /cache-key: cmakeHash missing/);
  });

  it('missing generatedHash throws naming the field', () => {
    const { computeCacheKey } = loadImpl();
    const args = greenArgs();
    delete args.generatedHash;
    assert.throws(() => computeCacheKey(args), /cache-key: generatedHash missing/);
  });

  it('empty generatedHash throws naming the field', () => {
    const { computeCacheKey } = loadImpl();
    const args = greenArgs();
    args.generatedHash = '';
    assert.throws(() => computeCacheKey(args), /cache-key: generatedHash missing/);
  });

  it('null/undefined input throws fail-closed', () => {
    const { computeCacheKey } = loadImpl();
    assert.throws(() => computeCacheKey(undefined), /cache-key: .* missing/);
    assert.throws(() => computeCacheKey(null), /cache-key: .* missing/);
  });

  it('non-string field values throw fail-closed', () => {
    const { computeCacheKey } = loadImpl();
    const args = greenArgs();
    args.ndkVersion = 27;
    assert.throws(() => computeCacheKey(args), /cache-key: ndkVersion missing/);
  });
});
