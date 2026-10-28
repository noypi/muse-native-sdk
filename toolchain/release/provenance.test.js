'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { emitProvenance, verifyProvenance } = require('./provenance.js');

const SHA_A = 'a'.repeat(64);
const SHA_B = 'b'.repeat(64);
const SHA_C = 'c'.repeat(64);

function subjects() {
  return [
    { path: 'dist/libmuse.so', sha256: SHA_A },
    { path: 'dist/libmuse-arm64.so', sha256: SHA_B },
  ];
}

function buildMeta(overrides = {}) {
  return {
    builder: 'muse-release-builder',
    commit: 'abc123def456',
    ndkVersion: 'r27c',
    lockedInputsDigest: 'd'.repeat(64),
    ...overrides,
  };
}

describe('emitProvenance', () => {
  it('emits canonical JSON binding digests to builder, commit, NDK pin and locked-inputs digest', () => {
    const out = emitProvenance(subjects(), buildMeta());
    const parsed = JSON.parse(out);
    const text = JSON.stringify(parsed);
    assert.ok(text.includes(SHA_A) && text.includes(SHA_B), 'subject digests bound');
    assert.ok(text.includes('muse-release-builder'), 'builder identity bound');
    assert.ok(text.includes('abc123def456'), 'source commit bound');
    assert.ok(text.includes('r27c'), 'NDK pin bound');
    assert.ok(text.includes('d'.repeat(64)), 'locked-inputs digest bound');
  });

  it('uses deterministic serialization: sorted keys, no timestamps, repeat calls identical', () => {
    const s1 = [{ path: 'b.so', sha256: SHA_B }, { path: 'a.so', sha256: SHA_A }];
    const s2 = [{ path: 'a.so', sha256: SHA_A }, { path: 'b.so', sha256: SHA_B }];
    const m1 = buildMeta();
    const m2 = { lockedInputsDigest: m1.lockedInputsDigest, ndkVersion: m1.ndkVersion, commit: m1.commit, builder: m1.builder };
    assert.equal(emitProvenance(s1, m1), emitProvenance(s2, m2));
    const out = emitProvenance(subjects(), buildMeta());
    assert.equal(JSON.stringify(JSON.parse(out)), out, 'output already in canonical key order');
    assert.ok(!/timestamp|created|generated/i.test(out), 'no timestamps emitted');
  });

  it('throws on malformed subject entries', () => {
    assert.throws(() => emitProvenance([{ path: 'a.so' }], buildMeta()), Error);
    assert.throws(() => emitProvenance([{ path: 'a.so', sha256: 'zzz' }], buildMeta()), Error);
    assert.throws(() => emitProvenance('nope', buildMeta()), Error);
  });

  it('throws on missing buildMeta fields', () => {
    assert.throws(() => emitProvenance(subjects(), { ...buildMeta(), builder: undefined }), Error);
    assert.throws(() => emitProvenance(subjects(), { ...buildMeta(), commit: '' }), Error);
    assert.throws(() => emitProvenance(subjects(), { ...buildMeta(), ndkVersion: '' }), Error);
    assert.throws(() => emitProvenance(subjects(), { ...buildMeta(), lockedInputsDigest: '' }), Error);
  });
});

describe('verifyProvenance', () => {
  it('returns ok:true with empty failures when every subject digest matches', () => {
    const att = emitProvenance(subjects(), buildMeta());
    assert.deepEqual(verifyProvenance(att, subjects()), { ok: true, failures: [] });
  });

  it('returns ok:false naming the tampered subject on digest mismatch (fails closed)', () => {
    const att = emitProvenance(subjects(), buildMeta());
    const tampered = [
      { path: 'dist/libmuse.so', sha256: SHA_C },
      { path: 'dist/libmuse-arm64.so', sha256: SHA_B },
    ];
    const res = verifyProvenance(att, tampered);
    assert.equal(res.ok, false);
    assert.ok(res.failures.some((f) => f.includes('dist/libmuse.so')), `names tampered subject, got: ${res.failures}`);
  });

  it('returns ok:false naming the missing subject', () => {
    const att = emitProvenance(subjects(), buildMeta());
    const res = verifyProvenance(att, [{ path: 'dist/libmuse.so', sha256: SHA_A }]);
    assert.equal(res.ok, false);
    assert.ok(res.failures.some((f) => f.includes('dist/libmuse-arm64.so')), `names missing subject, got: ${res.failures}`);
  });

  it('returns ok:false with failures on unparsable attestation (fails closed)', () => {
    const res = verifyProvenance('not json{{{', subjects());
    assert.equal(res.ok, false);
    assert.ok(res.failures.length > 0);
  });

  it('returns ok:false with failures on schema-violating attestation', () => {
    const res = verifyProvenance(JSON.stringify({ hello: 'world' }), subjects());
    assert.equal(res.ok, false);
    assert.ok(res.failures.length > 0);
  });

  it('detects subjects absent from the attestation (extra actual subject fails)', () => {
    const att = emitProvenance([{ path: 'a.so', sha256: SHA_A }], buildMeta());
    const res = verifyProvenance(att, [
      { path: 'a.so', sha256: SHA_A },
      { path: 'evil.so', sha256: SHA_C },
    ]);
    assert.equal(res.ok, false);
    assert.ok(res.failures.some((f) => f.includes('evil.so')), `names unexpected subject, got: ${res.failures}`);
  });
});
