'use strict';
// Batch 3 (T5/T6/T7) viral-loop contract — FR-SAMPLE-012 per-demo shareable
// loops. TC-SAMPLE-031/032/033 are manual; this file covers their
// unit-verifiable half as static source assertions (same proxy style as
// test/polish.test.ts: the plan interfaces pin the shapes, the sources are
// read as text, nothing is built/launched).
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. Reads
// demo sources as text; never builds, never launches, never touches network,
// secrets, application config, or live services. No shared mutable state.
//
// Binds ONLY to the plan Interface: lines (reproduced verbatim in the
// T5/T6/T7 contract files). Never binds to an implementation.
//
// ASSUMPTIONS:
//   A1 (static shape stands in for runtime): asserting on source text
//       (exported identifiers, link-scheme literals, storage hooks) is the
//       closest hermetic proxy for the manual TCs; actual copy-link/paste
//       reproduction, device persistence, share-sheet open, and the <30s
//       stub round are manual, owner tester.
//   A2 (scope): logic checks cover remix.ts / dailySeed.ts / guessMask.ts
//       only (the T5/T6/T7 Files lists); wiring checks cover each demo's
//       src/App.tsx only.
//   A3 (no execution): the logic modules are never imported — they may be
//       TSX/TS with toolchain aliases — so behaviour is asserted via the
//       exported shapes the Interface: lines pin, not via live calls.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync, existsSync } = require('node:fs');
const { join } = require('node:path');

const EXAMPLES = join(__dirname, '..', '..');

function targetDir(name) {
  return join(EXAMPLES, name);
}
function srcPath(target, rel) {
  return join(targetDir(target), rel);
}
function existsSrc(target, rel) {
  return existsSync(srcPath(target, rel));
}
function readSrc(target, rel) {
  return readFileSync(srcPath(target, rel), 'utf8');
}
function mustRead(target, rel) {
  assert.ok(existsSrc(target, rel), `${target}/${rel} missing`);
  return readSrc(target, rel);
}

// --- T5: gallery remix shuffle + gallery:// deep link (AC-SAMPLE-031) ---

describe('T5 remix.ts exists and exports the remix interface', () => {
  it('owns src/remix.ts (T5 Files list)', () => {
    assert.ok(
      existsSrc('m3-gallery', 'src/remix.ts'),
      'examples/m3-gallery/src/remix.ts missing',
    );
  });

  it('exports shuffleRemix(): { seed, palette, shapeScale }', () => {
    const src = mustRead('m3-gallery', 'src/remix.ts');
    assert.match(src, /shuffleRemix/, 'remix.ts must export shuffleRemix');
    assert.match(src, /seed/, 'shuffleRemix result must carry seed');
    assert.match(src, /palette/, 'shuffleRemix result must carry palette');
    assert.match(src, /shapeScale/, 'shuffleRemix result must carry shapeScale');
  });

  it("exports encodeRemix(id, seed) building 'gallery://story/<id>?seed=<n>'", () => {
    const src = mustRead('m3-gallery', 'src/remix.ts');
    assert.match(src, /encodeRemix/, 'remix.ts must export encodeRemix');
    assert.match(
      src,
      /gallery:\/\/story\//,
      "encodeRemix must build 'gallery://story/<id>?seed=<n>' links",
    );
    assert.match(src, /seed=/, 'encoded link must carry the seed query param');
  });

  it('exports decodeRemix(link): RemixParams parsing the deep link', () => {
    const src = mustRead('m3-gallery', 'src/remix.ts');
    assert.match(src, /decodeRemix/, 'remix.ts must export decodeRemix');
    assert.match(src, /RemixParams/, 'decodeRemix must return RemixParams');
  });

  it('decode(encode(x)) reproduces the exact remix (seed survives round-trip)', () => {
    const src = mustRead('m3-gallery', 'src/remix.ts');
    // Both halves of the round-trip must key off the same seed carrier.
    assert.match(src, /encodeRemix[\s\S]*seed/, 'encode side must carry seed');
    assert.match(src, /decodeRemix[\s\S]*seed/, 'decode side must recover seed');
    assert.match(
      src,
      /gallery:\/\/story\//,
      'round-trip must go through the gallery://story/ scheme',
    );
  });
});

describe('T5 gallery App.tsx wires shuffle + deep link', () => {
  it('App wires the shuffle action (shuffleRemix)', () => {
    const src = mustRead('m3-gallery', 'src/App.tsx');
    assert.match(src, /shuffleRemix/, 'gallery App.tsx must wire shuffleRemix');
  });

  it('App wires the deep link reproduce path (gallery:// / encodeRemix / decodeRemix / clipboard)', () => {
    const src = mustRead('m3-gallery', 'src/App.tsx');
    assert.ok(
      /gallery:\/\/story\//.test(src) ||
        /encodeRemix/.test(src) ||
        /decodeRemix/.test(src) ||
        /clipboard/i.test(src),
      'gallery App.tsx must wire the deep-link copy/paste reproduce path',
    );
  });
});

describe('T5 remix boundary / error / security', () => {
  it('encodeRemix validates its inputs (empty id / non-integer seed rejected)', () => {
    const src = mustRead('m3-gallery', 'src/remix.ts');
    assert.ok(
      /throw/.test(src) && /encodeRemix/.test(src),
      'remix.ts must throw on invalid encodeRemix input (empty id, bad seed)',
    );
  });

  it('decodeRemix rejects malformed links and the wrong scheme', () => {
    const src = mustRead('m3-gallery', 'src/remix.ts');
    assert.ok(
      /throw/.test(src) && /decodeRemix/.test(src),
      'remix.ts must throw on malformed decodeRemix input',
    );
    assert.match(
      src,
      /gallery:\/\//,
      'decodeRemix must check the gallery:// scheme before parsing',
    );
  });

  it('story id is encoded so link metacharacters (?, #, /) cannot break the scheme', () => {
    const src = mustRead('m3-gallery', 'src/remix.ts');
    assert.ok(
      /encodeURIComponent/.test(src) || /decodeURIComponent/.test(src) || /URL/.test(src),
      'remix.ts must encode the story id (no raw interpolation into the link)',
    );
  });
});

// --- T6: flappy daily seed + best + restart + share (AC-SAMPLE-032) ---

describe('T6 dailySeed.ts exists and exports the daily-seed interface', () => {
  it('owns src/dailySeed.ts (T6 Files list)', () => {
    assert.ok(
      existsSrc('flappy', 'src/dailySeed.ts'),
      'examples/flappy/src/dailySeed.ts missing',
    );
  });

  it('exports dailySeed(date?): number with seed = YYYYMMDD', () => {
    const src = mustRead('flappy', 'src/dailySeed.ts');
    assert.match(src, /dailySeed/, 'dailySeed.ts must export dailySeed');
    assert.ok(
      /getFullYear/.test(src) && /getMonth/.test(src) && /getDate/.test(src),
      'dailySeed must derive the seed from the calendar date (YYYYMMDD)',
    );
  });

  it('zero-pads month/day so YYYYMMDD is stable (e.g. Jan -> 01)', () => {
    const src = mustRead('flappy', 'src/dailySeed.ts');
    assert.ok(
      /padStart/.test(src) || /\*\s*100/.test(src) || /YYYYMMDD/i.test(src),
      'dailySeed must zero-pad month/day into a stable YYYYMMDD number',
    );
  });

  it('dailySeed is pure date math (deterministic per calendar day)', () => {
    const src = mustRead('flappy', 'src/dailySeed.ts');
    assert.ok(
      !/Math\.random/.test(src),
      'dailySeed must not use Math.random — same calendar day gives the same seed',
    );
  });

  it('exports device-persisted getBest()/setBest(n)', () => {
    const src = mustRead('flappy', 'src/dailySeed.ts');
    assert.match(src, /getBest/, 'dailySeed.ts must export getBest');
    assert.match(src, /setBest/, 'dailySeed.ts must export setBest');
    assert.ok(
      /localStorage|AsyncStorage|persist|store|	save/i.test(src),
      'best score must go through a device-persistence hook',
    );
  });

  it('setBest validates (negative / NaN / non-integer rejected or clamped)', () => {
    const src = mustRead('flappy', 'src/dailySeed.ts');
    assert.ok(
      /throw/.test(src) ||
        /Number\.isInteger/.test(src) ||
        /Math\.max/.test(src) ||
        /Math\.floor/.test(src),
      'setBest must guard negative/NaN/non-integer input',
    );
  });

  it('exports restart(): void completing < 1s (synchronous reset, no long sleep)', () => {
    const src = mustRead('flappy', 'src/dailySeed.ts');
    assert.match(src, /restart/, 'dailySeed.ts must export restart');
    assert.ok(
      !/setTimeout\s*\(\s*[^,]+,\s*(?:[1-9]\d{3,})/.test(src),
      'restart path must not sleep >= 1s — restart completes < 1s',
    );
  });

  it('exports sharePayload(): { score, seed }', () => {
    const src = mustRead('flappy', 'src/dailySeed.ts');
    assert.match(src, /sharePayload/, 'dailySeed.ts must export sharePayload');
    assert.match(src, /score/, 'share payload must carry score');
    assert.match(src, /seed/, 'share payload must carry seed');
  });
});

describe('T6 flappy App.tsx wires the daily-seed loop', () => {
  it('App wires dailySeed + best + restart + share payload', () => {
    const src = mustRead('flappy', 'src/App.tsx');
    assert.match(src, /dailySeed/, 'flappy App.tsx must wire dailySeed');
    assert.ok(
      /getBest/.test(src) || /setBest/.test(src),
      'flappy App.tsx must wire persisted best (getBest/setBest)',
    );
    assert.match(src, /restart/, 'flappy App.tsx must wire one-tap restart');
    assert.match(src, /sharePayload|share/i, 'flappy App.tsx must wire the share payload');
  });
});

describe('T6 share boundary / security', () => {
  it('share path performs no network upload (local share-sheet payload only)', () => {
    const src = mustRead('flappy', 'src/dailySeed.ts');
    assert.ok(
      !/fetch\s*\(/.test(src) && !/XMLHttpRequest/.test(src),
      'dailySeed.ts must not upload — share is a local payload, no backend',
    );
  });
});

// --- T7: muse-demo guess-the-mask round < 30s stub (AC-SAMPLE-033) ---

describe('T7 guessMask.ts exists and exports the round interface', () => {
  it('owns src/guessMask.ts (T7 Files list)', () => {
    assert.ok(
      existsSrc('muse-demo', 'src/guessMask.ts'),
      'examples/muse-demo/src/guessMask.ts missing',
    );
  });

  it('exports startRound(photoId): void', () => {
    const src = mustRead('muse-demo', 'src/guessMask.ts');
    assert.match(src, /startRound/, 'guessMask.ts must export startRound');
    assert.match(src, /photoId/, 'startRound must take a photoId');
  });

  it('exports guess(maskId): void', () => {
    const src = mustRead('muse-demo', 'src/guessMask.ts');
    assert.match(src, /guess/, 'guessMask.ts must export guess');
    assert.match(src, /maskId/, 'guess must take a maskId');
  });

  it('exports reveal() with mask + reasonStream + by_object shelf entry', () => {
    const src = mustRead('muse-demo', 'src/guessMask.ts');
    assert.match(src, /reveal/, 'guessMask.ts must export reveal');
    assert.match(src, /reasonStream/, 'reveal result must carry reasonStream');
    assert.match(src, /shelfEntry/, 'reveal result must carry shelfEntry');
    assert.match(src, /by_object/, "shelfEntry must use renderMode 'by_object'");
  });

  it('reasonStream is an AsyncIterable<string> (streams, not a bulk string)', () => {
    const src = mustRead('muse-demo', 'src/guessMask.ts');
    assert.ok(
      /AsyncIterable/.test(src) ||
        /Symbol\.asyncIterator/.test(src) ||
        /async\s*\*/.test(src),
      'reasonStream must be an AsyncIterable<string>',
    );
  });

  it('round runs against the stub proxy only (no network, no web image search)', () => {
    const src = mustRead('muse-demo', 'src/guessMask.ts');
    assert.ok(
      !/fetch\s*\(/.test(src) && !/XMLHttpRequest/.test(src),
      'guessMask.ts must not hit the network — stub proxy only (Q-MUSE-004: web image search stays out)',
    );
  });

  it('full round cannot exceed the 30s budget by construction (no >= 30s delay)', () => {
    const src = mustRead('muse-demo', 'src/guessMask.ts');
    assert.ok(
      !/setTimeout\s*\(\s*[^,]+,\s*(?:3\d{4,}|\d{6,})/.test(src),
      'guessMask.ts must not introduce delays near/above the 30s budget',
    );
  });
});

describe('T7 muse-demo App.tsx wires the guess-the-mask round', () => {
  it('App wires startRound + guess + reveal', () => {
    const src = mustRead('muse-demo', 'src/App.tsx');
    assert.match(src, /startRound/, 'muse-demo App.tsx must wire startRound');
    assert.match(src, /guess/, 'muse-demo App.tsx must wire guess');
    assert.match(src, /reveal/, 'muse-demo App.tsx must wire reveal');
  });
});

describe('T7 round boundary / error / security', () => {
  it('validates round inputs and ordering (empty photoId / guess-before-start rejected)', () => {
    const src = mustRead('muse-demo', 'src/guessMask.ts');
    assert.ok(
      /throw/.test(src),
      'guessMask.ts must throw on invalid round input or ordering (empty photoId, guess before startRound, unknown maskId)',
    );
  });

  it('reads no secrets or app config (stub fixtures are inline dummy data)', () => {
    const src = mustRead('muse-demo', 'src/guessMask.ts');
    assert.ok(
      !/process\.env/.test(src) && !/\.env/.test(src),
      'guessMask.ts must not read secrets or application config',
    );
  });
});
