'use strict';
// T7 — MuseImage fallbackOf→generate-image with seed/cacheKey memoization (Batch 3).
// Plan Interface (verbatim):
// `MuseImage({ prompt:string, style:'avatar'|'illustration'|'banner'|'sticker'|'poster', size:string, seed:string, fallbackOf?:string, cacheKey?:string, preset?:string })` — unknown style rejected pre-call with `{ code:'unknown-style' }`; `resolveImage(): Promise<{ url, seed, width, height }>` tries `fallbackOf` via MEDIA-001 cache/network first (injected fetch double; FR-MEDIA-004 consumed, not reimplemented), calls `generate-image` only on 404/empty; memoizes by `cacheKey` (default `prompt+style+size+seed`); retry 0. Spec: FR-MUSE-010 AC-MUSE-029 + FR-MUSE-002 (required) + FR-MEDIA-004 (required).
// Spec refs: FR-MUSE-010 (AC-MUSE-029); FR-MUSE-002 generate-image route (consumed); FR-MEDIA-004 cache/progressive fetch (consumed).
// Hermetic: node:test + node:assert/strict only; no network, no secrets, no
// config, no live services. MEDIA-001 cache/network (`fetchFallback`) and the
// `generate-image` route (`generateImage`) are injected doubles; call counts
// prove fallback-first + memoize + retry-0 behavior.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { MuseImage } = require('../lib/image.js');

const STYLES = ['avatar', 'illustration', 'banner', 'sticker', 'poster'];

function baseProps(overrides = {}) {
  return {
    prompt: 'Avatar for Ada, engineer. Loves robots.',
    style: 'avatar',
    size: '512x512',
    seed: 'seed-abc-123',
    ...overrides,
  };
}

// Injected MEDIA-001 double: serves fallbackOf cache/network hits.
// `entries`: Map url -> { status, body }. Missing url = 404.
function stubFallback(entries = {}) {
  const calls = [];
  const fetchFallback = async (url) => {
    calls.push(url);
    if (Object.hasOwn(entries, url)) return entries[url];
    return { status: 404, body: null };
  };
  return { fetchFallback, calls };
}

// Injected generate-image route double. Records every call; retry-0 is proven
// by the coder making exactly one call per resolveImage (asserted below).
function stubGenerate(handler) {
  const calls = [];
  const generateImage = async (req) => {
    calls.push(req);
    if (handler) return handler(req, calls.length);
    return { url: `generated:${req.seed}`, seed: req.seed, width: 512, height: 512 };
  };
  return { generateImage, calls };
}

function deps(overrides = {}) {
  const fb = stubFallback(overrides.fallbackEntries);
  const gen = stubGenerate(overrides.generate);
  return {
    fetchFallback: fb.fetchFallback,
    generateImage: gen.generateImage,
    presets: overrides.presets,
    _fbCalls: fb.calls,
    _genCalls: gen.calls,
  };
}

describe('T7 — unknown style rejected pre-call (AC-MUSE-029)', () => {
  it('rejects an unknown style with { code: "unknown-style" } before any fetch', () => {
    const d = deps();
    assert.throws(
      () => MuseImage(baseProps({ style: 'oil-painting' }), d),
      (err) => err && err.code === 'unknown-style'
    );
    assert.equal(d._fbCalls.length, 0);
    assert.equal(d._genCalls.length, 0);
  });

  it('accepts every allow-listed style token', () => {
    for (const style of STYLES) {
      const d = deps();
      const handle = MuseImage(baseProps({ style }), d);
      assert.equal(handle.props.style, style);
      assert.equal(d._fbCalls.length, 0);
      assert.equal(d._genCalls.length, 0);
    }
  });
});

describe('T7 — fallbackOf hit fires zero generate-image calls (AC-MUSE-029)', () => {
  it('returns the cached asset without calling generate-image', async () => {
    const cached = { url: 'cache:avatar-1', seed: 'seed-abc-123', width: 512, height: 512 };
    const d = deps({ fallbackEntries: { 'https://cdn.test/ada.png': { status: 200, body: cached } } });
    const handle = MuseImage(baseProps({ fallbackOf: 'https://cdn.test/ada.png' }), d);
    const result = await handle.resolveImage();
    assert.deepEqual(result, cached);
    assert.equal(d._fbCalls.length, 1);
    assert.equal(d._genCalls.length, 0);
  });
});

describe('T7 — fallbackOf 404/empty generates from prompt+style+size+seed (AC-MUSE-029)', () => {
  it('calls generate-image once on 404 with the full request identity', async () => {
    const d = deps();
    const handle = MuseImage(baseProps({ fallbackOf: 'https://cdn.test/missing.png' }), d);
    const result = await handle.resolveImage();
    assert.equal(d._fbCalls.length, 1);
    assert.equal(d._genCalls.length, 1);
    const req = d._genCalls[0];
    assert.equal(req.prompt, baseProps().prompt);
    assert.equal(req.style, 'avatar');
    assert.equal(req.size, '512x512');
    assert.equal(req.seed, 'seed-abc-123');
    assert.equal(result.seed, 'seed-abc-123');
    assert.ok(typeof result.url === 'string' && result.url.length > 0);
  });

  it('treats an empty (status-200 null body) fallback as a miss and generates', async () => {
    const d = deps({ fallbackEntries: { 'https://cdn.test/empty.png': { status: 200, body: null } } });
    const handle = MuseImage(baseProps({ fallbackOf: 'https://cdn.test/empty.png' }), d);
    await handle.resolveImage();
    assert.equal(d._genCalls.length, 1);
  });

  it('generates without fallbackOf (no cache attempt, one route call)', async () => {
    const d = deps();
    const handle = MuseImage(baseProps(), d);
    await handle.resolveImage();
    assert.equal(d._fbCalls.length, 0);
    assert.equal(d._genCalls.length, 1);
  });
});

describe('T7 — memoization by cacheKey, default prompt+style+size+seed (AC-MUSE-029)', () => {
  it('second resolveImage with the same key fires zero new calls', async () => {
    const d = deps();
    const handle = MuseImage(baseProps({ cacheKey: 'avatar:ada' }), d);
    const first = await handle.resolveImage();
    const second = await handle.resolveImage();
    assert.deepEqual(second, first);
    assert.equal(d._genCalls.length, 1);
  });

  it('explicit cacheKey isolates entries: different keys generate independently', async () => {
    const d = deps();
    const a = MuseImage(baseProps({ cacheKey: 'avatar:ada' }), d);
    const b = MuseImage(baseProps({ cacheKey: 'avatar:grace' }), d);
    await a.resolveImage();
    await b.resolveImage();
    assert.equal(d._genCalls.length, 2);
  });

  it('same seed reproduces the same asset (identity-stable avatars)', async () => {
    const d = deps();
    const a = MuseImage(baseProps({ seed: 'stable-hash-1' }), d);
    const first = await a.resolveImage();
    const second = await a.resolveImage();
    assert.equal(first.url, second.url);
    assert.equal(first.seed, 'stable-hash-1');
  });
});

describe('T7 — retry 0: generation is non-idempotent (FR-MUSE-002)', () => {
  it('a failing generate-image call is attempted exactly once and rejects named', async () => {
    let attempts = 0;
    const d = deps({
      generate: () => {
        attempts += 1;
        throw Object.assign({ code: 'upstream', route: 'generate-image' });
      },
    });
    const handle = MuseImage(baseProps(), d);
    await assert.rejects(handle.resolveImage(), (err) => err && err.code === 'upstream');
    assert.equal(attempts, 1);
    assert.equal(d._genCalls.length, 1);
  });
});

describe('T7 — preset runtime validation (Q-MUSE-003 settled)', () => {
  it('unknown preset key rejects pre-call with { code: "unknown-preset" }', async () => {
    // ASSUMPTION: preset allow-list arrives via the deps second arg
    // (`presets` registry double); the Interface line pins the rejection
    // shape, not the parameter plumbing.
    const d = deps({ presets: { 'avatar-default': { style: 'avatar' } } });
    assert.throws(
      () => MuseImage(baseProps({ preset: 'no-such-preset' }), d),
      (err) => err && err.code === 'unknown-preset'
    );
    assert.equal(d._fbCalls.length, 0);
    assert.equal(d._genCalls.length, 0);
  });

  it('known preset key passes validation and is forwarded to the route', async () => {
    const d = deps({ presets: { 'avatar-default': { style: 'avatar' } } });
    const handle = MuseImage(baseProps({ preset: 'avatar-default' }), d);
    await handle.resolveImage();
    assert.equal(d._genCalls.length, 1);
    assert.equal(d._genCalls[0].preset, 'avatar-default');
  });
});
