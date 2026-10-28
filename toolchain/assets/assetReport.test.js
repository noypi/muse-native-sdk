'use strict';
// T1 — Raster asset conversion gate (NFR-BUILD-017 / AC-BUILD-062, TC-BUILD-062).
// Hermetic: node:test + node:assert/strict only. Fixture buffers are inline;
// no FS/network beyond reading the sibling implementation source for the
// quality-setting documentation check; no secrets, no live services.
// Red state: ./assetPipeline.js does not exist yet, so every impl-dependent
// case fails with MODULE_NOT_FOUND until T1 lands.
//
// Interface under test (plan verbatim):
//   convertRaster(input: Buffer, opts: { format: 'avif' | 'webp', quality: number }): { bytes: Buffer, format: string, bytesSaved: number }
//     throws AssetError('unsupported-format' | 'invalid-quality')
//   summarizeAssets(results: Array<{format, bytesSaved}>): { lines: string[], totalSaved: number }
//     never throws on empty input.
//
// ASSUMPTIONS:
//   A1: quality is an integer 1..100 inclusive; 0, negatives, >100, NaN and
//       non-numbers are 'invalid-quality'. The spec pins "a documented quality
//       setting" without a range; 1..100 is the most defensible reading.
//   A2: bytesSaved is input.length - bytes.length (can be 0 for tiny fixtures,
//       never negative for a real conversion). Tests assert >= 0.
//   A3: Thrown errors expose the code as err.code (an AssetError export is
//       optional); the helper also accepts the code inside err.message so a
//       coder-exported AssetError class or a coded Error both satisfy it.
//   A4: Non-Buffer / missing input is rejected with AssetError (code
//       'invalid-quality' for null opts is not assumed; any AssetError counts).
//       Spec is silent; failing closed on malformed input is the defensible default.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const IMPL_FILE = path.join(__dirname, 'assetPipeline.js');

function loadImpl() {
  return require('./assetPipeline.js');
}

// Tiny synthetic "raster": marker bytes only, never a real image decode.
function fixtureRaster(bytes = 1024) {
  const buf = Buffer.alloc(bytes, 0x89);
  buf.write('RASTER', 0, 'utf8');
  return buf;
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

describe('T1 — convertRaster contract shape', () => {
  it('returns { bytes: Buffer, format: string, bytesSaved: number } for avif', () => {
    const { convertRaster } = loadImpl();
    const input = fixtureRaster();
    const out = convertRaster(input, { format: 'avif', quality: 80 });
    assert.ok(Buffer.isBuffer(out.bytes), 'bytes must be a Buffer');
    assert.equal(out.format, 'avif');
    assert.equal(typeof out.bytesSaved, 'number');
    assert.ok(out.bytesSaved >= 0, 'bytesSaved must not be negative');
  });

  it('returns the webp format tag for a webp conversion', () => {
    const { convertRaster } = loadImpl();
    const out = convertRaster(fixtureRaster(), { format: 'webp', quality: 75 });
    assert.ok(Buffer.isBuffer(out.bytes));
    assert.equal(out.format, 'webp');
    assert.equal(typeof out.bytesSaved, 'number');
  });

  it('documents the quality setting in code and report output', () => {
    // AC-BUILD-062 requires "a documented quality setting".
    const src = fs.readFileSync(IMPL_FILE, 'utf8');
    assert.match(src, /quality/i, 'implementation must document the quality setting');
    const { convertRaster, summarizeAssets } = loadImpl();
    const out = convertRaster(fixtureRaster(), { format: 'avif', quality: 80 });
    const summary = summarizeAssets([{ format: out.format, bytesSaved: out.bytesSaved }]);
    assert.ok(summary.lines.length > 0);
    assert.match(summary.lines.join('\n'), /avif/i);
  });
});

describe('T1 — convertRaster error paths', () => {
  it("throws AssetError('unsupported-format') for an unknown format", () => {
    const { convertRaster } = loadImpl();
    expectAssetError(
      () => convertRaster(fixtureRaster(), { format: 'jpeg', quality: 80 }),
      'unsupported-format'
    );
  });

  it("throws AssetError('unsupported-format') when format is missing", () => {
    const { convertRaster } = loadImpl();
    expectAssetError(() => convertRaster(fixtureRaster(), { quality: 80 }), 'unsupported-format');
  });

  it("throws AssetError('invalid-quality') for quality 0", () => {
    const { convertRaster } = loadImpl();
    expectAssetError(() => convertRaster(fixtureRaster(), { format: 'avif', quality: 0 }), 'invalid-quality');
  });

  it("throws AssetError('invalid-quality') for quality above 100", () => {
    const { convertRaster } = loadImpl();
    expectAssetError(
      () => convertRaster(fixtureRaster(), { format: 'webp', quality: 101 }),
      'invalid-quality'
    );
  });

  it("throws AssetError('invalid-quality') for a non-numeric quality", () => {
    const { convertRaster } = loadImpl();
    expectAssetError(
      () => convertRaster(fixtureRaster(), { format: 'avif', quality: 'high' }),
      'invalid-quality'
    );
  });

  it('rejects non-Buffer input with an AssetError (input validation)', () => {
    // ASSUMPTION A4: failing closed on malformed input.
    const { convertRaster } = loadImpl();
    let thrown = null;
    try {
      convertRaster('not-a-buffer', { format: 'avif', quality: 80 });
    } catch (err) {
      thrown = err;
    }
    assert.ok(thrown, 'expected an AssetError for non-Buffer input');
    assert.ok(thrown.code || thrown.message, 'error must carry a code or message');
  });
});

describe('T1 — summarizeAssets report (TC-BUILD-062 size deltas)', () => {
  it('never throws on empty input and reports zero savings', () => {
    const { summarizeAssets } = loadImpl();
    const out = summarizeAssets([]);
    assert.ok(Array.isArray(out.lines));
    assert.equal(out.totalSaved, 0);
  });

  it('sums per-asset savings and emits one line per asset', () => {
    const { summarizeAssets } = loadImpl();
    const out = summarizeAssets([
      { format: 'avif', bytesSaved: 400 },
      { format: 'webp', bytesSaved: 250 },
    ]);
    assert.equal(out.totalSaved, 650);
    assert.equal(out.lines.length, 2);
    assert.match(out.lines.join('\n'), /avif/i);
    assert.match(out.lines.join('\n'), /webp/i);
  });
});
