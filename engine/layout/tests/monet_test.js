'use strict';
// TC-LAYOUT-022 (AC-LAYOUT-022) — Batch 2 T6.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services, no device wallpaper access —
// platform input is injected as pixels, never read.
// Binds ONLY to the pinned T6 `Interface:` line, never to implementation.
// Module resolution: `../tokens/monet.js` once it lands, else an inline
// fallback stub with an empty seed palette, so every palette assertion below
// is red until monet.js lands — and the harness itself never crashes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../tokens/monet.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong (empty
  // palette, never platform) so every behavior assertion below fails (red)
  // until ../tokens/monet.js lands.
  mod = {
    extractPalette: () => ({ palette: {}, source: 'seed' }),
  };
}
const { extractPalette } = mod;

const HEX = /^#[0-9a-fA-F]{6}$/;

function assertWellFormedPalette(out) {
  assert.equal(typeof out, 'object');
  assert.ok(out.palette !== null && typeof out.palette === 'object');
  const keys = Object.keys(out.palette);
  // ASSUMPTION: a usable Monet palette carries at least primary, secondary,
  // tertiary, and neutral roles — four or more entries.
  assert.ok(keys.length >= 4, `palette must carry 4+ roles, got ${keys.length}`);
  for (const k of keys) {
    assert.equal(typeof k, 'string');
    assert.ok(k.length > 0, 'palette keys must be non-empty');
    assert.ok(HEX.test(out.palette[k]), `palette value for ${k} must be #RRGGBB`);
  }
}

// ---------------------------------------------------------------------------
// T6 contract:
// `extractPalette(source: { wallpaperPixels?: Uint8Array, seed?: string }):
//  { palette: Record<string, string>, source: 'platform' | 'seed' }`
// ---------------------------------------------------------------------------
describe('TC-LAYOUT-022 — Monet extraction uses the platform palette when available, else a deterministic seed (AC-LAYOUT-022)', () => {
  it("returns the platform palette when wallpaper pixels are available", () => {
    const pixels = new Uint8Array([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255]);
    const out = extractPalette({ wallpaperPixels: pixels });
    assert.equal(out.source, 'platform');
    assertWellFormedPalette(out);
  });

  it('platform extraction is deterministic for the same pixels', () => {
    const mk = () => new Uint8Array([10, 20, 30, 255, 40, 50, 60, 255]);
    assert.deepEqual(extractPalette({ wallpaperPixels: mk() }), extractPalette({ wallpaperPixels: mk() }));
  });

  it('distinct wallpaper pixels yield distinct platform palettes', () => {
    const warm = new Uint8Array([200, 50, 50, 255, 210, 60, 60, 255]);
    const cool = new Uint8Array([50, 50, 200, 255, 60, 60, 210, 255]);
    assert.notDeepEqual(extractPalette({ wallpaperPixels: warm }), extractPalette({ wallpaperPixels: cool }));
  });

  it('falls back to a deterministic seed palette without pixels', () => {
    const a = extractPalette({ seed: 'ocean' });
    assert.equal(a.source, 'seed');
    assertWellFormedPalette(a);
    assert.deepEqual(a, extractPalette({ seed: 'ocean' }));
  });

  it('different seeds yield different seed palettes', () => {
    assert.notDeepEqual(extractPalette({ seed: 'ocean' }), extractPalette({ seed: 'desert' }));
  });

  it('falls back to the defined seed palette with no input at all — never throws, never empty', () => {
    const a = extractPalette({});
    assert.equal(a.source, 'seed');
    assertWellFormedPalette(a);
    assert.deepEqual(a, extractPalette({}));
  });

  it('treats empty pixels as absent and uses the seed fallback', () => {
    const out = extractPalette({ wallpaperPixels: new Uint8Array(0), seed: 'ocean' });
    assert.equal(out.source, 'seed');
    assert.deepEqual(out, extractPalette({ seed: 'ocean' }));
  });

  it('handles a large pixel buffer without throwing', () => {
    const pixels = new Uint8Array(256 * 256 * 4);
    for (let i = 0; i < pixels.length; i += 1) pixels[i] = i % 256;
    const out = extractPalette({ wallpaperPixels: pixels });
    assert.equal(out.source, 'platform');
    assertWellFormedPalette(out);
  });

  it('ASSUMPTION: rejects a non-Uint8Array pixel buffer with a TypeError', () => {
    assert.throws(() => extractPalette({ wallpaperPixels: [255, 0, 0, 255] }), TypeError);
  });
});
