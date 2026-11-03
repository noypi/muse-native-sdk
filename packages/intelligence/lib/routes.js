'use strict';
// T2 — Proxy route availability check for transcribe/reason/generate-image.
// Consumes the FR-MUSE-002 contract only: verifies route descriptors plus
// `muse-native.yaml:muse.presets` key-union validation. Hermetic: `fetchImpl`
// is the only I/O and an `upstream:*` fetch must never fire.

const REQUIRED_ROUTES = ['transcribe', 'reason', 'generate-image'];

function err(code, extra) {
  return Object.assign({ code }, extra);
}

function isUpstreamUrl(url) {
  return String(url).startsWith('upstream:');
}

async function loadDoc(fetchImpl, name, turnId) {
  const fwd = turnId === undefined ? undefined : { turnId };
  const res = await fetchImpl(name, fwd);
  if (res && typeof res.json === 'function') return res.json();
  return res;
}

function validateProps(props) {
  if (props == null) return;
  if (typeof props !== 'object') throw err('raw-param', { route: 'transcribe' });
  const forbiddenKeys = new Set(['model', 'modelid', 'endpoint', 'url', 'baseurl', 'apikey']);
  for (const [key, value] of Object.entries(props)) {
    const flat = String(key).toLowerCase().replace(/[_-]/g, '');
    if (forbiddenKeys.has(flat)) {
      if (flat === 'model' || flat === 'modelid') throw err('raw-model', { key });
      if (flat === 'endpoint' || flat === 'url' || flat === 'baseurl') {
        throw err('raw-endpoint', { key });
      }
      throw err('raw-param', { key });
    }
    if (typeof value === 'string') {
      if (/^muse-[a-z0-9-]+/i.test(value.trim())) throw err('raw-model', { key });
      if (/^https?:\/\//i.test(value.trim())) throw err('raw-endpoint', { key });
    }
  }
}

async function assertMuseRoutes(fetchImpl, options) {
  const opts = options ?? {};
  const { preset, props, resolveParams, turnId } = opts;

  // Reject raw model IDs / endpoints / params before any descriptor fetch,
  // hence certainly before any upstream call (which never happens here).
  validateProps(props);

  const routes = await loadDoc(fetchImpl, 'routes', turnId);
  const presets = await loadDoc(fetchImpl, 'presets', turnId);

  if (preset !== undefined && preset !== null) {
    const known = presets && typeof presets === 'object' ? Object.keys(presets) : [];
    if (!known.includes(preset)) {
      const bad = err('unknown-preset', { preset });
      throw bad;
    }
  }

  const missing = REQUIRED_ROUTES.filter((name) => !(routes && routes[name]));
  const result = { ok: missing.length === 0, missing };
  // T5 — echo the calling turnId opaquely; the proxy passes it through and
  // never invents one, so invent none when the caller supplies none.
  if (turnId !== undefined) result.turnId = turnId;
  // T4 — thread the preset's params object verbatim when requested (A3: no
  // second resolution path — no model resolution, no merging; that lives
  // solely in dispatch). Falsy/absent resolveParams keeps today's shape.
  if (resolveParams && preset !== undefined && preset !== null) {
    const entry = presets && typeof presets === 'object' ? presets[preset] : undefined;
    if (entry && typeof entry === 'object' && 'params' in entry) {
      result.params = { ...entry.params };
    }
  }
  return result;
}

module.exports = { assertMuseRoutes, streamReason };

// T6 — Hermetic SSE streaming of `reason` per the R1 ASSUMPTION (plan Blocking
// Q1; cited as assumed-shape, never spec-settled):
// `streamReason(fetchImpl, { turnId, preset? })` async-iterable yielding
// `{ type: 'reasoning_content'|'content', turnId, delta }` in SSE order.
// The stub `fetchImpl` serves recorded `reason-stream` chunks in-process; no
// socket, no network, no key. Stale-turn deltas are dropped: when the caller
// passes `committedTurnId` (latest committed turn) and it differs from the
// calling `turnId`, nothing is yielded.
async function* streamReason(fetchImpl, options) {
  const opts = options ?? {};
  const { turnId, preset, route, committedTurnId } = opts;

  // Stale turn: the caller was superseded by a later committed turn.
  if (committedTurnId !== undefined && turnId !== committedTurnId) return;

  const routes = await loadDoc(fetchImpl, 'routes', turnId);
  const presets = await loadDoc(fetchImpl, 'presets', turnId);

  if (preset !== undefined && preset !== null) {
    const known = presets && typeof presets === 'object' ? Object.keys(presets) : [];
    if (!known.includes(preset)) throw err('unknown-preset', { preset });
  }

  const want = route !== undefined && route !== null ? route : (preset != null && presets && presets[preset] && presets[preset].route) || 'reason';
  if (!routes || !routes[want]) throw err('unknown-route', { route: want });

  const fwd = turnId === undefined ? undefined : { turnId };
  const res = await fetchImpl('reason-stream', fwd);
  let chunks = res && Array.isArray(res.chunks) ? res.chunks : undefined;
  if (chunks === undefined) {
    const doc = res && typeof res.json === 'function' ? await res.json() : res;
    if (doc && Array.isArray(doc.chunks)) chunks = doc.chunks;
    else if (doc && doc.streams && Array.isArray(doc.streams.masked)) chunks = doc.streams.masked;
    else chunks = [];
  }

  for (const chunk of chunks) {
    for (const line of String(chunk).split('\n')) {
      const trimmed = line.trim();
      if (!trimmed.startsWith('data:')) continue;
      const payload = trimmed.slice('data:'.length).trim();
      if (payload === '[DONE]') continue;
      let obj;
      try {
        obj = JSON.parse(payload);
      } catch (e) {
        continue;
      }
      if (obj && typeof obj.reasoning_content === 'string') {
        yield { type: 'reasoning_content', turnId, delta: obj.reasoning_content };
      } else if (obj && typeof obj.content === 'string') {
        yield { type: 'content', turnId, delta: obj.content };
      }
    }
  }
}
