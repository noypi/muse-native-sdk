'use strict';
// FR-LAYOUT-009 / AC-LAYOUT-022 — Monet palette extraction with seed fallback.
// Platform input is injected as RGBA `wallpaperPixels` (no device read), so the
// module stays hermetic. Non-empty pixels yield a deterministic `platform`
// palette; absent/empty pixels yield a deterministic `seed` palette derived
// from `seed` (or the built-in default). Never throws on lookup input and never
// returns an empty palette — except wrong-type pixel buffers, which throw
// TypeError fail-fast so callers cannot silently misread memory.

const DEFAULT_SEED = 'monet-default';

const ROLES = ['primary', 'secondary', 'tertiary', 'neutral'];

function fnv1a32(bytes) {
  let h = 0x811c9dc5;
  for (let i = 0; i < bytes.length; i += 1) {
    h ^= bytes[i];
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}

function hashString(text) {
  let h = 0x811c9dc5;
  for (let i = 0; i < text.length; i += 1) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}

function mulberry32(seed) {
  let a = seed >>> 0;
  return function next() {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function hslToHex(h, s, l) {
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = l - c / 2;
  let r = 0;
  let g = 0;
  let b = 0;
  if (h < 60) {
    r = c;
    g = x;
  } else if (h < 120) {
    r = x;
    g = c;
  } else if (h < 180) {
    g = c;
    b = x;
  } else if (h < 240) {
    g = x;
    b = c;
  } else if (h < 300) {
    r = x;
    b = c;
  } else {
    r = c;
    b = x;
  }
  const toByte = (v) => Math.round((v + m) * 255);
  const hex = (v) => toByte(v).toString(16).padStart(2, '0').toUpperCase();
  return `#${hex(r)}${hex(g)}${hex(b)}`;
}

function paletteFromHash(hash) {
  const rand = mulberry32(hash);
  const palette = {};
  for (const role of ROLES) {
    const h = Math.floor(rand() * 360);
    const s = 0.4 + rand() * 0.4;
    const l = 0.4 + rand() * 0.3;
    palette[role] = hslToHex(h, s, l);
  }
  return palette;
}

function extractPalette(source) {
  const input = source && typeof source === 'object' ? source : {};
  const { wallpaperPixels, seed } = input;
  if (wallpaperPixels !== undefined) {
    if (!(wallpaperPixels instanceof Uint8Array)) {
      throw new TypeError('extractPalette(source): wallpaperPixels must be a Uint8Array');
    }
    if (wallpaperPixels.length > 0) {
      return { palette: paletteFromHash(fnv1a32(wallpaperPixels)), source: 'platform' };
    }
    // Empty pixels count as absent — fall through to the seed path.
  }
  const seedText = typeof seed === 'string' && seed.length > 0 ? seed : DEFAULT_SEED;
  return { palette: paletteFromHash(hashString(seedText)), source: 'seed' };
}

module.exports = { extractPalette };
