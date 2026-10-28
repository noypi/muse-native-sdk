'use strict';
// T3 — Texture compression gate (NFR-BUILD-017 / AC-BUILD-064, TC-BUILD-064).
// Hermetic descriptor-level transform over synthetic RGBA8 fixtures; no GPU,
// no device, no real codec invocation.
// Format tiers mirror GFX-TEX by reference (ASTC primary, ETC2/EAC baseline);
// output is packed into the BUILD-006 delivery shape (asset pack).

class AssetError extends Error {
  constructor(code, message) {
    super(message || code);
    this.name = 'AssetError';
    this.code = code;
  }
}

const SUPPORTED_TIERS = new Set(['astc', 'etc2']);

// BUILD-006 delivery shape targeted by compressed texture output.
const TEXTURE_ASSET_PACK = 'texture-asset-pack';

function compressTexture(tex, opts) {
  const tier = opts ? opts.tier : undefined;
  if (tier !== 'astc' && tier !== 'etc2') {
    throw new AssetError('unsupported-tier', `unsupported-tier: '${String(tier)}'`);
  }
  const width = tex ? tex.width : undefined;
  const height = tex ? tex.height : undefined;
  const pixels = tex ? tex.pixels : undefined;
  if (!Number.isInteger(width) || width <= 0 || !Number.isInteger(height) || height <= 0) {
    throw new AssetError('invalid-descriptor', `invalid-descriptor: width=${String(width)} height=${String(height)}`);
  }
  if (!Buffer.isBuffer(pixels) || pixels.length !== width * height * 4) {
    throw new AssetError(
      'invalid-descriptor',
      `invalid-descriptor: expected ${width * height * 4} RGBA8 bytes, got ${Buffer.isBuffer(pixels) ? pixels.length : typeof pixels}`
    );
  }
  // Deterministic hermetic stand-in: tier-tagged header + sampled payload.
  // ASTC primary compresses more aggressively than the ETC2 baseline.
  const ratio = tier === 'astc' ? 0.25 : 0.5;
  const payloadLen = Math.max(1, Math.floor(pixels.length * ratio));
  const header = Buffer.from(`:${tier}:`, 'utf8');
  const payload = Buffer.from(pixels.subarray(0, Math.min(payloadLen, pixels.length)));
  const bytes = Buffer.concat([header, payload]);
  return { bytes, tier, pack: TEXTURE_ASSET_PACK };
}

module.exports = { AssetError, compressTexture };
