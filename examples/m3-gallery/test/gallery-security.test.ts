'use strict';
// T8 — demo-m3-gallery security unit tests (security-review.md 2026-11-02).
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. No adb,
// no device, no network, no secrets, no config, no downloads. Every case
// drives an in-test pure mirror of the MainActivity.java logic named below
// over synthetic fixtures, plus static source pins that the guarded shape
// is present in `src/MainActivity.java`. No shared mutable state.
//
// Binds ONLY to existing production entry points (no new callable surface):
//   1. `routeGalleryLink(Uri)` — gallery://story/<id>[?seed=n] fail-closed
//      (AC-SAMPLE-041; AC-PACKAGING-011/012 deep-link navigation).
//   2. `handleGalleryIntent` stash + `handleTreeBytes` drain-once
//      (T6/T7 cold-start deep link; LOW stash-eviction note).
//   3. `isShellChromeAdvance` 3-arg + viewport-relative 5-arg overload
//      (FR-SAMPLE-014 tap geometry; AC-SAMPLE-039/040).
//   4. `dispatchKeyEvent` DPAD_RIGHT single-step / DPAD_LEFT consumed
//      no-op (FR-SAMPLE-014 key-injection boundary).
//   5. `storyTargetPage(id, viewportH)` parser caps (AC-SAMPLE-041).
//
// ASSUMPTIONS:
//   A1 (mirrors, not imports): MainActivity is Java and not importable from
//       node, so the behavioral mirrors below re-state the Java validation
//       order verbatim (host → id → query → target). The static source pins
//       tie each mirror to the production text; if the coder reorders the
//       Java checks, the pins fail and the contract is re-examined — the
//       mirrors are never edited to match a weakened implementation.
//   A2 (stash eviction): the requested "non-story-host never evicts" guard
//       is the security-review LOW fix and is NOT yet in the Java source
//       (stashing is scheme-only today). The `stash-host-check` pin below
//       is the RED record of that coder-owned fix; every other case asserts
//       behavior that holds today.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const MAIN_ACTIVITY = join(__dirname, '..', 'src', 'MainActivity.java');
const src = readFileSync(MAIN_ACTIVITY, 'utf8');

function pinPresent(re, label) {
  assert.ok(re.test(src), `MainActivity.java must carry the ${label} guard`);
}

// ---- 1. routeGalleryLink mirror (host → id → query → target) ----
function routeLink(link, resolveTarget) {
  if (link == null || link.host !== 'story') return { routed: false, reason: 'host' };
  const rawId = link.pathSegments.length === 0 ? '' : link.pathSegments[0];
  let id;
  try {
    id = decodeURIComponent(rawId);
  } catch (_) {
    id = rawId;
  }
  if (id == null || id === '') return { routed: false, reason: 'unknown-story' };
  const query = link.encodedQuery;
  if (query != null && query !== '') {
    if (!/^seed=-?\d+$/.test(query)) return { routed: false, reason: 'unknown-story' };
  }
  const target = resolveTarget(id);
  if (target == null) return { routed: false, reason: 'unknown-story' };
  return { routed: true, id };
}

const KNOWN = new Set(['card', 'button']);
const resolveKnown = (id) => (KNOWN.has(String(id).toLowerCase()) ? { page: 0, pages: 4 } : null);

// ---- 2. cold-start stash + drain-once mirror (current Java order) ----
function stashOnColdStart(data, engineReady, pending) {
  if (data == null || data.scheme !== 'gallery') return { next: pending, routedNow: false };
  if (!engineReady) return { next: data, routedNow: false }; // last-wins, scheme-only (see A2)
  return { next: pending, routedNow: true };
}
function drainStash(pending, route) {
  if (pending == null) return { next: null, calls: 0 };
  route(pending);
  return { next: null, calls: 1 };
}

// ---- 3. tap-geometry mirror (NEXT_X_MIN=600, NEXT_Y_MAX=200; ratios .833/.125) ----
function isShellChromeAdvance(x, y) {
  return x >= 600 && y <= 200;
}
function isShellChromeAdvanceVH(x, y, viewW, viewH) {
  if (viewW > 0 && viewH > 0) return x >= viewW * 0.833 && y <= viewH * 0.125;
  return isShellChromeAdvance(x, y);
}
const ACTION_UP = 1;
const ACTION_MOVE = 2;
function shouldPageOnUp(action, x, y, viewW, viewH, engineReady) {
  return action === ACTION_UP && engineReady && isShellChromeAdvanceVH(x, y, viewW, viewH);
}

// ---- 4. DPAD mirror (ACTION_DOWN + repeatCount==0 + engineHandle!=0) ----
const DPAD_RIGHT = 22;
const DPAD_LEFT = 21;
function dispatchKey({ engineReady, action, repeatCount, keyCode }) {
  if (engineReady && action === 0 && repeatCount === 0) {
    if (keyCode === DPAD_RIGHT) return { consumed: true, advanced: 1 };
    if (keyCode === DPAD_LEFT) return { consumed: true, advanced: 0 };
  }
  return { consumed: false, advanced: 0 };
}

// ---- 5. storyTargetPage validation-prefix + row-cap mirror ----
function mui1Bytes(jsonText, { magic = 'MUI1', version = 1 } = {}) {
  const payload = Buffer.from(jsonText, 'utf8');
  const header = Buffer.alloc(12);
  header.write(magic, 0, 4, 'ascii');
  header.writeUInt32LE(version, 4);
  header.writeUInt32LE(payload.length, 8);
  return Buffer.concat([header, payload]);
}
function resolveTargetPrefix(id, viewportH, bytes) {
  if (id == null || id === '' || viewportH <= 140) return null;
  if (bytes == null || bytes.length < 12) return null;
  if (!(bytes[0] === 0x4d && bytes[1] === 0x55 && bytes[2] === 0x49 && bytes[3] === 0x31)) return null;
  if (bytes[4] !== 1) return null;
  const jsonLen = bytes[8] | (bytes[9] << 8) | (bytes[10] << 16) | (bytes[11] << 24);
  if (jsonLen <= 0 || jsonLen + 12 > bytes.length) return null;
  let parsed;
  try {
    parsed = JSON.parse(Buffer.from(bytes.subarray(12, 12 + jsonLen)).toString('utf8'));
  } catch (_) {
    return null;
  }
  const children = (parsed.roots && parsed.roots[0] && parsed.roots[0].children) || [];
  const compTypes = [];
  for (const child of children) {
    if (compTypes.length >= 400) break;
    if (!child || typeof child.type !== 'string') continue;
    if (child.type === '#text' || child.type === 'View' || child.type === 'Text') continue;
    compTypes.push(child.type);
  }
  const matchRow = compTypes.findIndex((t) => t.toLowerCase() === String(id).toLowerCase());
  if (matchRow < 0) return null;
  return { matchRow, rows: compTypes.length };
}
function treeWith(n) {
  const children = [];
  for (let i = 0; i < n; i++) children.push({ type: `Comp${i}`, props: {} });
  return JSON.stringify({ roots: [{ children }] });
}

describe('T8.1 deep-link fail-closed — no page change on untrusted shapes (AC-SAMPLE-041)', () => {
  it('routes a valid gallery://story/<id> link', () => {
    const r = routeLink({ host: 'story', pathSegments: ['Card'], encodedQuery: null }, resolveKnown);
    assert.equal(r.routed, true);
  });

  it('routes with a single integer seed (?seed=3, ?seed=-2)', () => {
    for (const q of ['seed=3', 'seed=-2', 'seed=0']) {
      const r = routeLink({ host: 'story', pathSegments: ['Card'], encodedQuery: q }, resolveKnown);
      assert.equal(r.routed, true, `query ${q} must route`);
    }
  });

  it('rejects unknown-id with no page change', () => {
    const r = routeLink({ host: 'story', pathSegments: ['nope-not-real'], encodedQuery: null }, resolveKnown);
    assert.equal(r.routed, false);
    assert.equal(r.reason, 'unknown-story');
  });

  it('rejects empty-id with no page change', () => {
    for (const segs of [[], ['']]) {
      const r = routeLink({ host: 'story', pathSegments: segs, encodedQuery: null }, resolveKnown);
      assert.equal(r.routed, false, `segments ${JSON.stringify(segs)} must reject`);
    }
  });

  it('rejects non-story host with no page change', () => {
    for (const host of ['other', '', null]) {
      const r = routeLink({ host, pathSegments: ['Card'], encodedQuery: null }, resolveKnown);
      assert.equal(r.routed, false, `host ${String(host)} must reject`);
      assert.equal(r.reason, 'host');
    }
  });

  it('rejects multi-query and non-seed-int query with no page change', () => {
    for (const q of ['seed=abc', 'a=1&seed=2', 'seed=1&x=2', 'seed=', 'seed=1.5', 'foo=1']) {
      const r = routeLink({ host: 'story', pathSegments: ['Card'], encodedQuery: q }, resolveKnown);
      assert.equal(r.routed, false, `query ${q} must reject`);
    }
  });

  it('source pins the fail-closed routing shape', () => {
    pinPresent(/"story"\.equals\(link\.getHost\(\)\)/, 'story-host check');
    pinPresent(/seed=-\?\\\\d\+/, 'seed-integer query gate');
    pinPresent(/deep-link rejected \(unknown-story\)/, 'unknown-story rejection');
  });
});

describe('T8.2 cold-start stash evict/drain (T6/T7 deep link)', () => {
  it('stashes a valid story link pre-ready and drains it exactly once post-ready', () => {
    const link = { scheme: 'gallery', host: 'story', pathSegments: ['Card'], encodedQuery: null };
    const stashed = stashOnColdStart(link, false, null);
    assert.equal(stashed.next, link);
    assert.equal(stashed.routedNow, false);
    let calls = 0;
    const drained = drainStash(stashed.next, () => { calls++; });
    assert.equal(calls, 1);
    assert.equal(drained.next, null);
    const again = drainStash(drained.next, () => { calls++; });
    assert.equal(again.calls, 0);
    assert.equal(calls, 1);
  });

  it('warm links route immediately without touching the stash', () => {
    const link = { scheme: 'gallery', host: 'story', pathSegments: ['Card'], encodedQuery: null };
    const r = stashOnColdStart(link, true, null);
    assert.equal(r.routedNow, true);
    assert.equal(r.next, null);
  });

  it('a drained non-story link still rejects with no page change', () => {
    const seen = [];
    const drained = drainStash(
      { scheme: 'gallery', host: 'other', pathSegments: ['x'], encodedQuery: null },
      (link) => { seen.push(routeLink(link, resolveKnown)); },
    );
    assert.equal(drained.calls, 1);
    assert.equal(drained.next, null);
    assert.equal(seen.length, 1);
    assert.equal(seen[0].routed, false);
  });

  it('RED (LOW fix, coder-owned): stash site checks story-host before overwriting', () => {
    pinPresent(/"story"\.equals\(data\.getHost\(\)\)/, 'stash-time story-host check');
  });

  it('source pins drain-once + deferred routing', () => {
    pinPresent(/pendingDeepLink = null;\s*\n\s*routeGalleryLink\(link\)/, 'drain-once (null then route)');
  });
});

describe('T8.3 tap geometry — Next-region advances once; Prev/surface/drag never page (FR-SAMPLE-014)', () => {
  it('Next-region ACTION_UP on 720x1600 pages (evidence anchor 660,150)', () => {
    assert.equal(shouldPageOnUp(ACTION_UP, 660, 150, 720, 1600, true), true);
  });

  it('claimed is forensics-only: same verdict for claimed -1 and 7', () => {
    assert.equal(isShellChromeAdvanceVH(660, 105, 720, 1600), true);
    assert.equal(isShellChromeAdvance(660, 105), isShellChromeAdvanceVH(660, 105, 720, 1600));
  });

  it('Prev-region and story-surface samples never page', () => {
    assert.equal(shouldPageOnUp(ACTION_UP, 60, 150, 720, 1600, true), false);
    assert.equal(shouldPageOnUp(ACTION_UP, 360, 800, 720, 1600, true), false);
  });

  it('drag-release (MOVE in Next region) never pages', () => {
    assert.equal(shouldPageOnUp(ACTION_MOVE, 660, 150, 720, 1600, true), false);
  });

  it('pre-ready taps never page', () => {
    assert.equal(shouldPageOnUp(ACTION_UP, 660, 150, 720, 1600, false), false);
  });

  it('non-positive viewport falls back to reference constants', () => {
    assert.equal(isShellChromeAdvanceVH(660, 150, 0, 0), isShellChromeAdvance(660, 150));
    assert.equal(isShellChromeAdvanceVH(660, 150, -1, 1600), isShellChromeAdvance(660, 150));
  });

  it('source pins the geometry-only rule', () => {
    pinPresent(/NEXT_X_MIN/, 'Next-region bound');
    pinPresent(/0\.833f/, 'viewport-relative x ratio');
    pinPresent(/0\.125f/, 'viewport-relative y ratio');
  });
});

describe('T8.4 DPAD — RIGHT single-step forward-only; LEFT consumed no-op (FR-SAMPLE-014)', () => {
  it('DPAD_RIGHT ACTION_DOWN first-press advances exactly one page', () => {
    const r = dispatchKey({ engineReady: true, action: 0, repeatCount: 0, keyCode: DPAD_RIGHT });
    assert.equal(r.consumed, true);
    assert.equal(r.advanced, 1);
  });

  it('DPAD_LEFT is consumed with zero advance (Prev stays disabled)', () => {
    const r = dispatchKey({ engineReady: true, action: 0, repeatCount: 0, keyCode: DPAD_LEFT });
    assert.equal(r.consumed, true);
    assert.equal(r.advanced, 0);
  });

  it('repeat-count guarded: held RIGHT falls through', () => {
    const r = dispatchKey({ engineReady: true, action: 0, repeatCount: 1, keyCode: DPAD_RIGHT });
    assert.deepEqual(r, { consumed: false, advanced: 0 });
  });

  it('key-up and engine-not-ready fall through', () => {
    assert.deepEqual(
      dispatchKey({ engineReady: true, action: 1, repeatCount: 0, keyCode: DPAD_RIGHT }),
      { consumed: false, advanced: 0 },
    );
    assert.deepEqual(
      dispatchKey({ engineReady: false, action: 0, repeatCount: 0, keyCode: DPAD_RIGHT }),
      { consumed: false, advanced: 0 },
    );
  });

  it('source pins the DPAD guard shape', () => {
    pinPresent(/KEYCODE_DPAD_RIGHT/, 'DPAD_RIGHT entry');
    pinPresent(/KEYCODE_DPAD_LEFT/, 'DPAD_LEFT entry');
    pinPresent(/getRepeatCount\(\) == 0/, 'repeat-count guard');
    pinPresent(/key Prev disabled/, 'LEFT consumed no-op');
  });
});

describe('T8.5 storyTargetPage caps — parser DoS / malformed-asset boundary (AC-SAMPLE-041)', () => {
  const good = (n, extra) => mui1Bytes(extra || treeWith(n));

  it('resolves a known id on a valid tree with a live viewport', () => {
    const kids = [{ type: 'Card', props: {} }, { type: '#text', value: 'hi' }];
    const t = resolveTargetPrefix('Card', 800, mui1Bytes(JSON.stringify({ roots: [{ children: kids }] })));
    assert.ok(t !== null);
    assert.equal(t.matchRow, 0);
  });

  it('rejects viewport<=140 (boundary 140 null, 141 live)', () => {
    assert.equal(resolveTargetPrefix('Card', 140, good(1)), null);
    assert.equal(resolveTargetPrefix('Card', 0, good(1)), null);
    assert.ok(resolveTargetPrefix('Comp0', 141, good(1)) !== null);
  });

  it('caps component rows at 400', () => {
    const t = resolveTargetPrefix('Comp399', 2000, good(450));
    assert.ok(t !== null);
    assert.equal(t.rows, 400);
    assert.equal(resolveTargetPrefix('Comp400', 2000, good(450)), null);
  });

  it('rejects malformed/short trees without throwing', () => {
    const ir = treeWith(2);
    assert.equal(resolveTargetPrefix('Comp0', 800, mui1Bytes(ir, { magic: 'BAD!' })), null);
    assert.equal(resolveTargetPrefix('Comp0', 800, mui1Bytes(ir, { version: 2 })), null);
    assert.equal(resolveTargetPrefix('Comp0', 800, Buffer.from('short', 'utf8')), null);
    const trunc = good(2).subarray(0, 20);
    assert.equal(resolveTargetPrefix('Comp0', 800, trunc), null);
    assert.equal(resolveTargetPrefix('Comp0', 800, mui1Bytes('{not-json', {})), null);
    assert.equal(resolveTargetPrefix(null, 800, good(1)), null);
    assert.equal(resolveTargetPrefix('', 800, good(1)), null);
  });

  it('rejects unknown id without throwing', () => {
    assert.equal(resolveTargetPrefix('nope-not-real', 800, good(3)), null);
  });

  it('source pins the parser caps', () => {
    pinPresent(/viewportH <= 140/, 'viewport floor');
    pinPresent(/compTypes\.size\(\) >= 400/, '400-row cap');
    pinPresent(/catch \(Throwable ignored\)/, 'never-throws guard');
  });
});
