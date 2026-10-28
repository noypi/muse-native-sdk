'use strict';
// T7 — MuseImage fallbackOf→generate-image with seed/cacheKey memoization.
// `fallbackOf` is tried first via the injected MEDIA-001 cache/network double
// (`fetchFallback(url)` → `{ status, body }`); the `generate-image` proxy
// route (`generateImage(req)`) fires exactly once on 404/empty (retry 0).
// Results memoize by `cacheKey` (default `prompt+style+size+seed`), scoped per
// injected route double so hermetic recorders stay isolated across tests while
// handles sharing one recorder share entries. Unknown style/preset reject
// pre-call with zero fetch/route contact.
//
// T8 — safety-block + named errors (extends T7, every T7 surface intact):
// a `generate-image` rejection with `{ code:'moderated' }` resolves to the
// named `moderated` value
// `{ status:'moderated', illustration:'m3-blocked', message, editAffordance:true }`
// (never a url, never memoized); `retryWithEdit(editedPrompt)` re-generates
// into `ready`. Schema/param violations (`schema`/`upstream`/`timeout`/`auth`)
// reject `{ code, route:'generate-image' }` — never a partial image, never
// memoized. The handle exposes observable `status`
// (`idle` → `generating` → `ready`/`moderated`/`error`) plus `lastError`.

// T2 route seam (consumed, never reimplemented): availability/preset shape
// lives in lib/routes.js; the unit doubles inject the route itself.
const { assertMuseRoutes } = require('./routes.js');
void assertMuseRoutes;

const STYLES = new Set(['avatar', 'illustration', 'banner', 'sticker', 'poster']);

// Module-level memo: WeakMap<generateImage fn, Map<cacheKey, asset>>.
// Keyed on the injected route double so fresh recorders never see stale
// entries, while handles sharing one recorder share memoized assets.
const memoByRoute = new WeakMap();
const fallbackMemo = new Map();

function memoFor(generateImage) {
  if (typeof generateImage !== 'function') return fallbackMemo;
  let m = memoByRoute.get(generateImage);
  if (!m) {
    m = new Map();
    memoByRoute.set(generateImage, m);
  }
  return m;
}

function defaultKey(props) {
  return `${props.prompt}+${props.style}+${props.size}+${props.seed}`;
}

// T9 — `size` grammar: `"<width>x<height>"`, square `"512"` = 512x512.
function parseSizeToken(size) {
  const m = /^(\d+)(?:x(\d+))?$/.exec(String(size));
  if (!m) return undefined;
  const width = Number(m[1]);
  const height = m[2] === undefined ? width : Number(m[2]);
  return { width, height };
}

function assertPreset(deps, preset) {
  if (preset === undefined || preset === null) return; // route default
  const doc = deps ? deps.presets : undefined;
  if (doc != null) {
    const known = doc instanceof Set ? [...doc] : Array.isArray(doc) ? doc : Object.keys(doc);
    if (known.includes(preset)) return;
  }
  // No registry injected (or key absent): fail closed, same shape as
  // assertMuseRoutes — an unknown preset never reaches a route call.
  throw { code: 'unknown-preset', preset };
}

function MuseImage(props, deps) {
  const p = props ?? {};
  if (!STYLES.has(p.style)) {
    throw { code: 'unknown-style', style: p.style };
  }
  const d = deps ?? {};
  assertPreset(d, p.preset);

  let key = p.cacheKey !== undefined && p.cacheKey !== null ? String(p.cacheKey) : defaultKey(p);
  const memo = memoFor(d.generateImage);
  const explicitKey = p.cacheKey !== undefined && p.cacheKey !== null;
  let currentPrompt = p.prompt;
  // Fallback-only memo entries are namespaced by `fallbackOf` so different
  // fallback URLs never share an asset; route memo stays keyed per injected
  // `generateImage` fn + key (shared-recorder behavior preserved).
  const scopeKey = (map, k) =>
    map === fallbackMemo ? `${String(p.fallbackOf ?? '')}\u0000${k}` : k;

  const handle = { props: { ...p }, cacheKey: key, status: 'idle', lastError: undefined };

  // T9 — prompt doubles as the a11y description; reserved layout box is fixed
  // from `size` at construction (no shift, observable before resolve).
  handle.contentDescription = p.prompt;
  const reservedBox = parseSizeToken(p.size);
  if (reservedBox === undefined) {
    throw { code: 'unknown-size', route: 'generate-image', size: p.size };
  }
  handle.reservedBox = reservedBox;
  handle.layoutShift = false;

  function toModerated(err) {
    const message =
      err && typeof err.message === 'string' && err.message.length > 0
        ? err.message
        : 'This prompt was blocked by safety review. Edit the description and try again.';
    return { status: 'moderated', illustration: 'm3-blocked', message, editAffordance: true };
  }

  function toNamedError(err) {
    const code = err && err.code !== undefined ? err.code : 'upstream';
    return { code, route: 'generate-image' };
  }

  async function generateOnce(prompt) {
    const req = { prompt, style: p.style, size: p.size, seed: p.seed };
    if (p.preset !== undefined && p.preset !== null) req.preset = p.preset;
    try {
      const asset = await d.generateImage(req);
      return asset;
    } catch (err) {
      if (err && err.code === 'moderated') {
        const value = toModerated(err);
        handle.status = 'moderated';
        handle.lastError = undefined;
        return { __moderated: value };
      }
      const named = toNamedError(err);
      handle.status = 'error';
      handle.lastError = named;
      throw named;
    }
  }

  async function resolveImage() {
    // T9 — offline is an explicit named error state: probe first, zero route
    // calls, never a silent empty.
    if (typeof d.isOnline === 'function') {
      const online = await d.isOnline();
      if (!online) {
        const named = { code: 'offline', route: 'generate-image' };
        handle.status = 'error';
        handle.lastError = named;
        throw named;
      }
    }

    // T9 — progressive preview through the MEDIA-001 path (injected double):
    // reserved box first, preview frames forwarded, cross-fade after final.
    async function runProgressive() {
      if (typeof d.loadProgressive !== 'function') return;
      const req = { prompt: currentPrompt, style: p.style, size: p.size, seed: p.seed };
      await d.loadProgressive(req, async (frame) => {
        if (typeof d.onPreview === 'function') d.onPreview(frame);
      });
    }

    function fireCrossFade() {
      if (typeof d.onCrossFade === 'function') d.onCrossFade({ layoutShift: false });
    }

    if (memo.has(scopeKey(memo, key))) {
      handle.status = 'generating';
      const hit = memo.get(scopeKey(memo, key));
      handle.status = 'ready';
      handle.lastError = undefined;
      fireCrossFade();
      return hit;
    }

    handle.status = 'generating';

    await runProgressive();

    // Fallback-first via MEDIA-001 cache/network (injected double).
    if (p.fallbackOf !== undefined && p.fallbackOf !== null && typeof d.fetchFallback === 'function') {
      const res = await d.fetchFallback(p.fallbackOf);
      if (res && res.status === 200 && res.body !== null && res.body !== undefined) {
        memo.set(scopeKey(memo, key), res.body);
        handle.status = 'ready';
        handle.lastError = undefined;
        fireCrossFade();
        return res.body;
      }
    }

    // Single generate-image attempt (retry 0): failures reject named,
    // never cached, never retried; safety blocks resolve to moderated.
    const out = await generateOnce(currentPrompt);
    if (out && out.__moderated) return out.__moderated;
    memo.set(scopeKey(memo, key), out);
    handle.status = 'ready';
    handle.lastError = undefined;
    fireCrossFade();
    return out;
  }

  async function retryWithEdit(editedPrompt) {
    const next = typeof editedPrompt === 'string' ? editedPrompt : currentPrompt;
    currentPrompt = next;
    handle.props = { ...handle.props, prompt: next };
    handle.contentDescription = next;
    // Errors and moderated values are never memoized, so always re-fire.
    const storeKey = explicitKey ? key : defaultKey({ prompt: next, style: p.style, size: p.size, seed: p.seed });
    const store = explicitKey ? memo : memoFor(d.generateImage);
    handle.status = 'generating';
    const out = await generateOnce(next);
    if (out && out.__moderated) return out.__moderated;
    store.set(store === fallbackMemo ? scopeKey(store, storeKey) : storeKey, out);
    // A subsequent `resolveImage()` reads the edited asset (default-key case).
    if (!explicitKey) {
      key = storeKey;
      handle.cacheKey = key;
    }
    handle.status = 'ready';
    handle.lastError = undefined;
    return out;
  }

  handle.resolveImage = resolveImage;
  handle.retryWithEdit = retryWithEdit;
  return handle;
}

module.exports = { MuseImage };
