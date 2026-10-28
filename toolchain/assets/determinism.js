'use strict';
// T4 — Asset pipeline determinism + content hashing gate (NFR-BUILD-017 / AC-BUILD-065, TC-BUILD-065).
// Canonical JSON encoding + SHA-256 over transform outputs from T1–T3.
// Same manifest bytes produce byte-identical packId/hash across runs.

const crypto = require('node:crypto');

class AssetError extends Error {
  constructor(code, message) {
    super(message || code);
    this.name = 'AssetError';
    this.code = code;
  }
}

function sha256Hex(bytes) {
  return crypto.createHash('sha256').update(bytes).digest('hex');
}

function hashAsset(bytes) {
  if (!Buffer.isBuffer(bytes)) {
    throw new AssetError('invalid-input', 'invalid-input: hashAsset input must be a Buffer');
  }
  return sha256Hex(bytes);
}

function isPlainObject(value) {
  if (value === null || typeof value !== 'object') {
    return false;
  }
  const proto = Object.getPrototypeOf(value);
  return proto === Object.prototype || proto === null;
}

function assertCanonical(value, seen) {
  if (value === undefined) {
    throw new AssetError('unstable-input', 'unstable-input: undefined is not canonical');
  }
  const kind = typeof value;
  if (kind === 'function' || kind === 'symbol' || kind === 'bigint') {
    throw new AssetError('unstable-input', `unstable-input: '${kind}' is not canonical`);
  }
  if (kind === 'number') {
    if (!Number.isFinite(value)) {
      throw new AssetError('unstable-input', 'unstable-input: non-finite number is not canonical');
    }
    return;
  }
  if (kind === 'string' || kind === 'boolean' || value === null) {
    return;
  }
  // Objects and arrays only from here.
  if (seen.has(value)) {
    throw new AssetError('unstable-input', 'unstable-input: circular reference is not canonical');
  }
  seen.add(value);
  try {
    if (Array.isArray(value)) {
      for (const item of value) {
        assertCanonical(item, seen);
      }
      return;
    }
    if (isPlainObject(value)) {
      for (const key of Object.keys(value)) {
        assertCanonical(value[key], seen);
      }
      return;
    }
    throw new AssetError('unstable-input', 'unstable-input: only plain JSON values are canonical');
  } finally {
    seen.delete(value);
  }
}

function canonicalStringify(value) {
  if (value === null || typeof value !== 'object') {
    return JSON.stringify(value);
  }
  if (Array.isArray(value)) {
    return `[${value.map((item) => canonicalStringify(item)).join(',')}]`;
  }
  const keys = Object.keys(value).sort();
  const parts = keys.map((key) => `${JSON.stringify(key)}:${canonicalStringify(value[key])}`);
  return `{${parts.join(',')}}`;
}

function buildAssetPack(manifest) {
  if (manifest === null || typeof manifest !== 'object' || Array.isArray(manifest) || Buffer.isBuffer(manifest)) {
    throw new AssetError('unstable-input', 'unstable-input: manifest must be a plain object');
  }
  if (!isPlainObject(manifest)) {
    throw new AssetError('unstable-input', 'unstable-input: manifest must be a plain object');
  }
  assertCanonical(manifest, new Set());
  const canonical = canonicalStringify(manifest);
  const hash = sha256Hex(Buffer.from(canonical, 'utf8'));
  const packId = `pack-${hash.slice(0, 16)}`;
  return { packId, hash };
}

module.exports = { AssetError, hashAsset, buildAssetPack };
