'use strict';
// T4 — Asset pipeline determinism + content hashing gate (NFR-BUILD-017 / AC-BUILD-065, TC-BUILD-065).
// Hermetic: node:test + node:assert/strict only. Inline Buffer fixtures only
// (simulated T1–T3 transform outputs); no FS beyond this file, no network,
// no secrets, no services.
// Red state: ./determinism.js does not exist yet, so every impl-dependent
// case fails (MODULE_NOT_FOUND via loadImpl) until T4 lands.
//
// Interface under test (plan verbatim):
//   hashAsset(bytes: Buffer): string (stable hex digest);
//   buildAssetPack(manifest: object): { packId: string, hash: string } — same
//   manifest bytes produce byte-identical `packId`/`hash` across runs;
//   throws AssetError('unstable-input') on non-canonical manifest encoding.
//
// ASSUMPTIONS:
//   A1: "Canonical manifest encoding" = a plain JSON-serializable object
//       (plain object/array/string/number/boolean/null only, no undefined,
//       functions, symbols, or circular refs). Anything else throws
//       AssetError('unstable-input'). Key order is NOT significant: two
//       deep-equal manifests built with different insertion order must still
//       yield identical packId/hash (canonicalization sorts keys).
//   A2: hashAsset accepts empty Buffers (hex digest of zero bytes); non-Buffer
//       input throws AssetError (fail-closed input validation, spec silent).
//   A3: Thrown errors expose err.code ('unstable-input'); the helper also
//       accepts the code inside err.message.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./determinism.js');
}

function expectAssetError(fn, code) {
  let thrown = null;
  try {
    fn();
  } catch (err) {
    thrown = err;
  }
  assert.ok(thrown, `expected AssetError('${code}') to be thrown`);
  const blob = `${(thrown && thrown.code) || ''} ${(thrown && thrown.message) || ''}`;
  assert.ok(
    (thrown && thrown.code) === code || blob.includes(code),
    `expected error code '${code}', got code=${JSON.stringify(thrown && thrown.code)} message=${JSON.stringify(thrown && thrown.message)}`
  );
}

describe('T4 — hashAsset stable hex digest (AC-BUILD-065)', () => {
  it('returns a non-empty lowercase hex string', () => {
    const { hashAsset } = loadImpl();
    const digest = hashAsset(Buffer.from('asset-bytes-fixture', 'utf8'));
    assert.equal(typeof digest, 'string');
    assert.ok(digest.length > 0, 'digest must be non-empty');
    assert.match(digest, /^[0-9a-f]+$/, 'digest must be a stable hex digest');
  });

  it('is deterministic: same bytes hash identically across calls', () => {
    const { hashAsset } = loadImpl();
    const bytes = Buffer.from('deterministic-content', 'utf8');
    assert.equal(hashAsset(bytes), hashAsset(Buffer.from(bytes)), 'same content must hash identically');
  });

  it('distinguishes different content (content-hashed)', () => {
    const { hashAsset } = loadImpl();
    const a = hashAsset(Buffer.from('asset-a', 'utf8'));
    const b = hashAsset(Buffer.from('asset-b', 'utf8'));
    assert.notEqual(a, b, 'different content must yield different digests');
  });

  it('hashes empty buffers to a stable digest', () => {
    // ASSUMPTION A2: empty input is valid; incremental builds hash unchanged
    // (possibly empty) assets without throwing.
    const { hashAsset } = loadImpl();
    const a = hashAsset(Buffer.alloc(0));
    const b = hashAsset(Buffer.alloc(0));
    assert.match(a, /^[0-9a-f]+$/);
    assert.equal(a, b);
  });

  it('rejects non-Buffer input with an AssetError (input validation)', () => {
    const { hashAsset } = loadImpl();
    let thrown = null;
    try {
      hashAsset('not-a-buffer');
    } catch (err) {
      thrown = err;
    }
    assert.ok(thrown, 'expected an AssetError for non-Buffer input');
    assert.ok((thrown && (thrown.code || thrown.message)), 'error must carry a code or message');
  });
});

describe('T4 — buildAssetPack determinism (AC-BUILD-065 incremental stability)', () => {
  it('returns { packId: string, hash: string } with non-empty values', () => {
    const { buildAssetPack } = loadImpl();
    const out = buildAssetPack({ assets: [{ name: 'icon.avif', hash: 'aa' }] });
    assert.equal(typeof out.packId, 'string');
    assert.equal(typeof out.hash, 'string');
    assert.ok(out.packId.length > 0, 'packId must be non-empty');
    assert.match(out.hash, /^[0-9a-f]+$/, 'hash must be a hex digest');
  });

  it('same manifest bytes produce byte-identical packId/hash across runs', () => {
    const { buildAssetPack } = loadImpl();
    const manifestA = { name: 'pack-main', assets: [{ name: 'a.avif', hash: '11' }] };
    const manifestB = { name: 'pack-main', assets: [{ name: 'a.avif', hash: '11' }] };
    const first = buildAssetPack(manifestA);
    const second = buildAssetPack(manifestB);
    assert.deepEqual(second, first, 'identical manifests must yield identical packId/hash');
  });

  it('is key-order insensitive (canonical encoding sorts keys)', () => {
    // ASSUMPTION A1: insertion order is not part of "manifest bytes" — the
    // canonical encoding sorts keys, so these two are byte-identical inputs.
    const { buildAssetPack } = loadImpl();
    const first = buildAssetPack({ b: 2, a: 1 });
    const second = buildAssetPack({ a: 1, b: 2 });
    assert.deepEqual(second, first, 'key order must not change packId/hash');
  });

  it('incremental stability: an unchanged asset keeps its hash across pack rebuilds', () => {
    // Batch-1 outputs are consumed as already-built inputs (no production
    // code here): simulate a T1 raster output whose bytes never change.
    const { hashAsset, buildAssetPack } = loadImpl();
    const stableBytes = Buffer.from('raster-output-fixture', 'utf8');
    const h1 = hashAsset(stableBytes);
    const pack1 = buildAssetPack({ assets: [{ name: 'icon.avif', hash: h1 }] });
    const h2 = hashAsset(Buffer.from('raster-output-fixture', 'utf8'));
    assert.equal(h2, h1, 'unchanged asset bytes must keep the same content hash');
    const pack2 = buildAssetPack({ assets: [{ name: 'icon.avif', hash: h2 }] });
    assert.deepEqual(pack2, pack1, 'rebuilt pack over unchanged assets must be identical');
  });

  it('changed content changes the pack hash', () => {
    const { buildAssetPack } = loadImpl();
    const before = buildAssetPack({ assets: [{ name: 'a.avif', hash: '11' }] });
    const after = buildAssetPack({ assets: [{ name: 'a.avif', hash: '22' }] });
    assert.notDeepEqual(after, before, 'changed manifest content must change packId/hash');
  });
});

describe("T4 — non-canonical manifest encoding throws AssetError('unstable-input')", () => {
  it("throws 'unstable-input' for undefined values", () => {
    const { buildAssetPack } = loadImpl();
    expectAssetError(() => buildAssetPack({ a: undefined }), 'unstable-input');
  });

  it("throws 'unstable-input' for function values", () => {
    const { buildAssetPack } = loadImpl();
    expectAssetError(() => buildAssetPack({ run: () => 1 }), 'unstable-input');
  });

  it("throws 'unstable-input' for circular manifests", () => {
    const { buildAssetPack } = loadImpl();
    const circular = { name: 'loop' };
    circular.self = circular;
    expectAssetError(() => buildAssetPack(circular), 'unstable-input');
  });

  it("throws 'unstable-input' for non-object manifests (null / string)", () => {
    const { buildAssetPack } = loadImpl();
    expectAssetError(() => buildAssetPack(null), 'unstable-input');
    expectAssetError(() => buildAssetPack('{"a":1}'), 'unstable-input');
  });
});
