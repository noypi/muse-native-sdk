'use strict';

const crypto = require('node:crypto');

function computeCacheKey(input) {
  const args = input !== null && typeof input === 'object' && !Array.isArray(input) ? input : {};
  if (typeof args.ndkVersion !== 'string' || args.ndkVersion.length === 0) {
    throw new Error('cache-key: ndkVersion missing');
  }
  if (!Array.isArray(args.cflags) || args.cflags.length === 0) {
    throw new Error('cache-key: cflags missing');
  }
  for (const flag of args.cflags) {
    if (typeof flag !== 'string' || flag.length === 0) {
      throw new Error('cache-key: cflags missing');
    }
  }
  if (typeof args.cmakeHash !== 'string' || args.cmakeHash.length === 0) {
    throw new Error('cache-key: cmakeHash missing');
  }
  if (typeof args.generatedHash !== 'string' || args.generatedHash.length === 0) {
    throw new Error('cache-key: generatedHash missing');
  }
  const components = {
    ndkVersion: args.ndkVersion,
    cflags: [...args.cflags].sort(),
    cmakeHash: args.cmakeHash,
    generatedHash: args.generatedHash,
  };
  const canonical = JSON.stringify(components);
  const key = crypto.createHash('sha256').update(canonical, 'utf8').digest('hex');
  return { key, components };
}

module.exports = { computeCacheKey };
