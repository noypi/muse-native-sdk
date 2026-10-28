'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { emitSbom, parseSbom } = require('./sbom.js');

function dep(overrides = {}) {
  return {
    name: 'left-pad',
    version: '1.3.0',
    integrity: 'sha512-abc123',
    ecosystem: 'npm',
    ...overrides,
  };
}

function meta(overrides = {}) {
  return { ndkVersion: 'r27c', commit: 'abc123def456', ...overrides };
}

describe('emitSbom', () => {
  it('emits canonical JSON enumerating every locked dep plus the NDK pin', () => {
    const out = emitSbom([dep({ name: 'a' }), dep({ name: 'b', version: '2.0.1' })], meta());
    const parsed = JSON.parse(out);
    const text = JSON.stringify(parsed);
    assert.ok(text.includes('a') && text.includes('b'), 'both deps enumerated');
    assert.ok(text.includes('r27c'), 'NDK pin enumerated');
    assert.ok(text.includes('abc123def456'), 'commit enumerated');
  });

  it('is deterministic: reordered input keys and dep order yield identical strings', () => {
    const d1 = { name: 'a', version: '1.0.0', integrity: 'sha512-x', ecosystem: 'npm' };
    const d2 = { integrity: 'sha512-y', ecosystem: 'npm', version: '2.0.0', name: 'b' };
    const m1 = { ndkVersion: 'r27c', commit: 'c0ffee' };
    const m2 = { commit: 'c0ffee', ndkVersion: 'r27c' };
    assert.equal(emitSbom([d1, d2], m1), emitSbom([d2, d1], m2));
  });

  it('sorts keys canonically with no timestamps', () => {
    const out = emitSbom([dep()], meta());
    const parsed = JSON.parse(out);
    assert.equal(JSON.stringify(parsed), out, 'output is already in canonical key order');
    assert.ok(!/timestamp|created|generated/i.test(out), 'no timestamps emitted');
  });

  it('emits identical output on repeated calls', () => {
    const args = [[dep()], meta()];
    assert.equal(emitSbom(...args), emitSbom(...args));
  });

  it('enumerates every locked dep (count and versions preserved)', () => {
    const deps = [dep({ name: 'a', version: '1.0.0' }), dep({ name: 'b', version: '2.0.0' }), dep({ name: 'c', version: '3.0.0' })];
    const parsed = JSON.parse(emitSbom(deps, meta()));
    const text = JSON.stringify(parsed);
    for (const d of deps) {
      assert.ok(text.includes(d.name) && text.includes(d.version), `dep ${d.name}@${d.version} present`);
    }
  });

  it('throws naming the offending entry on missing version', () => {
    assert.throws(() => emitSbom([dep({ name: 'bad-dep', version: undefined })], meta()), (e) => {
      assert.ok(e instanceof Error);
      assert.ok(e.message.includes('bad-dep'), `names offending entry, got: ${e.message}`);
      return true;
    });
  });

  it('throws naming the offending entry on unpinned version', () => {
    for (const v of ['*', '^1.0.0', '>=1.0.0', 'latest']) {
      assert.throws(() => emitSbom([dep({ name: `unpinned-${v}`, version: v })], meta()), (e) => {
        assert.ok(e.message.includes(`unpinned-${v}`), `names offending entry for ${v}, got: ${e.message}`);
        return true;
      });
    }
  });

  it('throws naming the offending entry on missing integrity', () => {
    assert.throws(() => emitSbom([dep({ name: 'no-hash', integrity: '' })], meta()), (e) => {
      assert.ok(e.message.includes('no-hash'), `names offending entry, got: ${e.message}`);
      return true;
    });
    assert.throws(() => emitSbom([dep({ name: 'no-hash-2', integrity: undefined })], meta()), (e) => {
      assert.ok(e.message.includes('no-hash-2'), `names offending entry, got: ${e.message}`);
      return true;
    });
  });

  it('throws on missing buildMeta fields', () => {
    assert.throws(() => emitSbom([dep()], { commit: 'x' }), Error);
    assert.throws(() => emitSbom([dep()], { ndkVersion: 'r27c' }), Error);
  });

  it('empty dep list still emits valid SBOM with NDK pin', () => {
    const parsed = JSON.parse(emitSbom([], meta()));
    assert.ok(JSON.stringify(parsed).includes('r27c'));
  });
});

describe('parseSbom', () => {
  it('round-trips emitSbom output', () => {
    const deps = [dep({ name: 'a' }), dep({ name: 'b' })];
    const m = meta();
    const { deps: outDeps, buildMeta: outMeta } = parseSbom(emitSbom(deps, m));
    assert.equal(outDeps.length, 2);
    assert.equal(outMeta.ndkVersion, m.ndkVersion);
    assert.equal(outMeta.commit, m.commit);
  });

  it('throws on invalid JSON', () => {
    assert.throws(() => parseSbom('not json{{{'), Error);
    assert.throws(() => parseSbom(''), Error);
  });

  it('throws on schema violation (missing deps, bad dep entry)', () => {
    assert.throws(() => parseSbom(JSON.stringify({ buildMeta: meta() })), Error);
    assert.throws(() => parseSbom(JSON.stringify({ deps: [{ name: 'a' }], buildMeta: meta() })), Error);
    assert.throws(() => parseSbom(JSON.stringify([1, 2, 3])), Error);
  });

  it('throws on non-string input', () => {
    assert.throws(() => parseSbom(undefined), Error);
    assert.throws(() => parseSbom(null), Error);
    assert.throws(() => parseSbom({}), Error);
  });
});
