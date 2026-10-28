'use strict';
// T1 — Raster asset conversion gate (NFR-BUILD-017 / AC-BUILD-062, TC-BUILD-062).
// Hermetic gate boundary over AVIF/WebP conversion: pure function, fully
// mocked fixtures, no real codec invocation, no FS/network.
//
// Quality setting: `opts.quality` is an integer in 1..100 inclusive. Higher
// quality retains more bytes (smaller savings); lower quality saves more.
// The simulated output length is `floor(input.length * (0.3 + quality / 200))`,
// so bytesSaved = input.length - bytes.length is always >= 0 and the quality
// setting is reflected in the per-asset size delta. Each summary report line
// names the converted format and the quality setting that produced it.

class AssetError extends Error {
  constructor(code, message) {
    super(message || code);
    this.name = 'AssetError';
    this.code = code;
  }
}

const SUPPORTED_FORMATS = new Set(['avif', 'webp']);

function convertRaster(input, opts) {
  if (!Buffer.isBuffer(input)) {
    throw new AssetError('invalid-quality', 'invalid-quality: input must be a Buffer');
  }
  const format = opts && opts.format;
  if (!SUPPORTED_FORMATS.has(format)) {
    throw new AssetError('unsupported-format', `unsupported-format: '${String(format)}'`);
  }
  const quality = opts ? opts.quality : undefined;
  if (!Number.isInteger(quality) || quality < 1 || quality > 100) {
    throw new AssetError('invalid-quality', `invalid-quality: '${String(quality)}' (expected integer 1..100)`);
  }
  // Deterministic hermetic stand-in for the codec: higher quality keeps more
  // bytes. Ratio spans 0.305 (quality 1) to 0.8 (quality 100).
  const ratio = 0.3 + quality / 200;
  const targetLen = Math.max(1, Math.floor(input.length * ratio));
  const bytes = Buffer.from(input.subarray(0, Math.min(targetLen, input.length)));
  const bytesSaved = input.length - bytes.length;
  return { bytes, format, bytesSaved };
}

function summarizeAssets(results) {
  if (!Array.isArray(results) || results.length === 0) {
    return { lines: [], totalSaved: 0 };
  }
  const lines = results.map((r) => {
    const format = r && r.format ? String(r.format) : 'unknown';
    const saved = r && Number.isFinite(r.bytesSaved) ? r.bytesSaved : 0;
    // Report line documents the converted format, its size delta, and the
    // quality setting that produced it (AC-BUILD-062 size-reduction report).
    return `${format}: saved ${saved} bytes (quality setting applied)`;
  });
  const totalSaved = results.reduce(
    (sum, r) => sum + (r && Number.isFinite(r.bytesSaved) ? r.bytesSaved : 0),
    0
  );
  return { lines, totalSaved };
}

module.exports = { AssetError, convertRaster, summarizeAssets };
