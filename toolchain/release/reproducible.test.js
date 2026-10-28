'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const {
  normalizeReleaseArtifact,
  hashArtifact,
} = require('./reproducible.js');

function entry(path, bytes, extra = {}) {
  return { path, bytes: Buffer.from(bytes), ...extra };
}

describe('normalizeReleaseArtifact', () => {
  it('sorts entries by path bytes regardless of input order', () => {
    const a = [entry('b.txt', [2]), entry('a.txt', [1]), entry('c.txt', [3])];
    const out = normalizeReleaseArtifact(a);
    assert.deepEqual(out.map((e) => e.path), ['a.txt', 'b.txt', 'c.txt']);
  });

  it('normalizes two content-equal sets built in different orders to byte-equal output', () => {
    const first = [entry('b.txt', [2]), entry('a.txt', [1])];
    const second = [entry('a.txt', [1]), entry('b.txt', [2])];
    const n1 = normalizeReleaseArtifact(first);
    const n2 = normalizeReleaseArtifact(second);
    assert.deepEqual(
      n1.map((e) => ({ path: e.path, bytes: [...e.bytes] })),
      n2.map((e) => ({ path: e.path, bytes: [...e.bytes] }))
    );
  });

  it('returns a new array and new entry objects, never mutating input', () => {
    const input = [entry('b.txt', [2]), entry('a.txt', [1])];
    const snapshot = input.map((e) => ({ path: e.path, bytes: [...e.bytes] }));
    const out = normalizeReleaseArtifact(input);
    assert.notEqual(out, input);
    assert.deepEqual(input.map((e) => e.path), ['b.txt', 'a.txt']);
    assert.deepEqual(input.map((e) => [...e.bytes]), snapshot.map((s) => s.bytes));
    for (let i = 0; i < out.length; i += 1) {
      assert.notEqual(out[i], input[i === 0 ? 1 : 0]);
    }
  });

  it('zeroes volatile metadata: extra timestamp/uid/gid fields do not affect output', () => {
    const plain = [entry('a.txt', [1])];
    const withVolatile = [
      entry('a.txt', [1], { mtime: new Date('2026-01-01T00:00:00Z'), uid: 1000, gid: 1000 }),
    ];
    const n1 = normalizeReleaseArtifact(plain);
    const n2 = normalizeReleaseArtifact(withVolatile);
    assert.deepEqual(
      n1.map((e) => ({ path: e.path, bytes: [...e.bytes] })),
      n2.map((e) => ({ path: e.path, bytes: [...e.bytes] }))
    );
  });

  it('volatile-timestamp-only difference yields equal digests', () => {
    const n1 = normalizeReleaseArtifact([
      entry('a.txt', [1], { mtime: 111 }),
    ]);
    const n2 = normalizeReleaseArtifact([
      entry('a.txt', [1], { mtime: 999 }),
    ]);
    assert.equal(hashArtifact(n1), hashArtifact(n2));
  });

  it('throws TypeError on non-array input', () => {
    assert.throws(() => normalizeReleaseArtifact('nope'), TypeError);
    assert.throws(() => normalizeReleaseArtifact(undefined), TypeError);
    assert.throws(() => normalizeReleaseArtifact({}), TypeError);
  });

  it('throws TypeError on malformed entries', () => {
    assert.throws(() => normalizeReleaseArtifact([null]), TypeError);
    assert.throws(() => normalizeReleaseArtifact([{ path: 'a.txt' }]), TypeError);
    assert.throws(() => normalizeReleaseArtifact([{ bytes: Buffer.from([1]) }]), TypeError);
    assert.throws(() => normalizeReleaseArtifact([{ path: 42, bytes: Buffer.from([1]) }]), TypeError);
    assert.throws(() => normalizeReleaseArtifact([{ path: 'a.txt', bytes: 'nope' }]), TypeError);
  });

  it('empty array normalizes to empty array', () => {
    assert.deepEqual(normalizeReleaseArtifact([]), []);
  });
});

describe('hashArtifact', () => {
  it('identical normalized inputs yield identical digests', () => {
    const n1 = normalizeReleaseArtifact([entry('b.txt', [2]), entry('a.txt', [1])]);
    const n2 = normalizeReleaseArtifact([entry('a.txt', [1]), entry('b.txt', [2])]);
    assert.equal(hashArtifact(n1), hashArtifact(n2));
  });

  it('returns SHA-256 hex (64 lowercase hex chars)', () => {
    const n = normalizeReleaseArtifact([entry('a.txt', [1])]);
    assert.match(hashArtifact(n), /^[0-9a-f]{64}$/);
  });

  it('differing content yields differing digests', () => {
    const n1 = normalizeReleaseArtifact([entry('a.txt', [1])]);
    const n2 = normalizeReleaseArtifact([entry('a.txt', [2])]);
    assert.notEqual(hashArtifact(n1), hashArtifact(n2));
  });

  it('differing paths yield differing digests even with equal bytes', () => {
    const n1 = normalizeReleaseArtifact([entry('a.txt', [1])]);
    const n2 = normalizeReleaseArtifact([entry('b.txt', [1])]);
    assert.notEqual(hashArtifact(n1), hashArtifact(n2));
  });

  it('throws TypeError on non-array input', () => {
    assert.throws(() => hashArtifact('nope'), TypeError);
    assert.throws(() => hashArtifact(undefined), TypeError);
  });
});
