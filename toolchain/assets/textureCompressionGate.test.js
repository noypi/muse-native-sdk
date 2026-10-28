'use strict';
// T3 — Texture compression gate (NFR-BUILD-017 / AC-BUILD-064, TC-BUILD-064).
// Hermetic: node:test + node:assert/strict only. Textures are synthetic pixel
// descriptors (no GPU, no device, no downloads); no secrets, no services.
// Red state: ./textureCompression.js does not exist yet, so every
// impl-dependent case fails with MODULE_NOT_FOUND until T3 lands.
//
// Interface under test (plan verbatim):
//   compressTexture(tex: { width: number, height: number, pixels: Buffer }, opts: { tier: 'astc' | 'etc2' }): { bytes: Buffer, tier: string, pack: string }
//     throws AssetError('unsupported-tier')
//   pack names the BUILD-006 delivery shape.
//
// ASSUMPTIONS:
//   A1: Tier tags mirror GFX-TEX by reference (ASTC primary, ETC2 baseline);
//       tests accept lowercase 'astc'/'etc2' only. Block sizes (4x4..12x12)
//       are not part of the unit contract.
//   A2: `pack` names the BUILD-006 delivery shape the output targets. Tests
//       require a non-empty string naming an asset pack or feature module
//       (matches /pack|module|bundle|split/i). The exact taxonomy string is
//       owned by the coder; the regex pins the BUILD-006 reference without
//       redefining its values here.
//   A3: pixels must be a Buffer of width*height*4 bytes (RGBA8 descriptor).
//       Mismatched/degenerate descriptors are rejected with an AssetError
//       (any code); the spec is silent, failing closed is the defensible default.
//   A4: Thrown errors expose err.code; the helper also accepts the code
//       inside err.message.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./textureCompression.js');
}

function fixtureTex(width = 4, height = 4) {
  return { width, height, pixels: Buffer.alloc(width * height * 4, 0x7f) };
}

function expectAssetError(fn, code) {
  let thrown = null;
  try {
    fn();
  } catch (err) {
    thrown = err;
  }
  assert.ok(thrown, `expected AssetError('${code}') to be thrown`);
  const blob = `${thrown.code || ''} ${thrown.message || ''}`;
  assert.ok(
    thrown.code === code || blob.includes(code),
    `expected error code '${code}', got code=${JSON.stringify(thrown.code)} message=${JSON.stringify(thrown.message)}`
  );
}

describe('T3 — compressTexture tiers (GFX-TEX by reference)', () => {
  it("compresses to the astc tier and names the delivery pack", () => {
    const { compressTexture } = loadImpl();
    const out = compressTexture(fixtureTex(), { tier: 'astc' });
    assert.ok(Buffer.isBuffer(out.bytes), 'bytes must be a Buffer');
    assert.equal(out.tier, 'astc');
    assert.equal(typeof out.pack, 'string');
    assert.match(out.pack, /pack|module|bundle|split/i, 'pack must name the BUILD-006 delivery shape');
  });

  it("compresses to the etc2 fallback tier and names the delivery pack", () => {
    const { compressTexture } = loadImpl();
    const out = compressTexture(fixtureTex(), { tier: 'etc2' });
    assert.ok(Buffer.isBuffer(out.bytes));
    assert.equal(out.tier, 'etc2');
    assert.match(out.pack, /pack|module|bundle|split/i, 'pack must name the BUILD-006 delivery shape');
  });

  it('produces non-empty output for a non-degenerate texture', () => {
    const { compressTexture } = loadImpl();
    const out = compressTexture(fixtureTex(8, 8), { tier: 'astc' });
    assert.ok(out.bytes.length > 0, 'compressed bytes must be non-empty');
  });
});

describe('T3 — compressTexture error paths', () => {
  it("throws AssetError('unsupported-tier') for an unknown tier", () => {
    const { compressTexture } = loadImpl();
    expectAssetError(() => compressTexture(fixtureTex(), { tier: 'bc7' }), 'unsupported-tier');
  });

  it("throws AssetError('unsupported-tier') when tier is missing", () => {
    const { compressTexture } = loadImpl();
    expectAssetError(() => compressTexture(fixtureTex(), {}), 'unsupported-tier');
  });

  it('rejects a degenerate texture descriptor with an AssetError (input validation)', () => {
    // ASSUMPTION A3: failing closed on malformed descriptors.
    const { compressTexture } = loadImpl();
    let thrown = null;
    try {
      compressTexture({ width: 0, height: 4, pixels: Buffer.alloc(0) }, { tier: 'astc' });
    } catch (err) {
      thrown = err;
    }
    assert.ok(thrown, 'expected an AssetError for a zero-width texture');
    assert.ok(thrown.code || thrown.message, 'error must carry a code or message');
  });

  it('rejects a pixel buffer whose size mismatches width*height*4', () => {
    const { compressTexture } = loadImpl();
    let thrown = null;
    try {
      compressTexture({ width: 4, height: 4, pixels: Buffer.alloc(7) }, { tier: 'etc2' });
    } catch (err) {
      thrown = err;
    }
    assert.ok(thrown, 'expected an AssetError for a mismatched pixel buffer');
    assert.ok(thrown.code || thrown.message, 'error must carry a code or message');
  });
});
