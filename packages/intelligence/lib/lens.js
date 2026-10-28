'use strict';
// MuseLens tap→segment polygon pipeline (AC-MUSE-008, RULE-MUSE-007).
// Routes through proxy/lib/dispatch.js (FR-MUSE-002); the unit seam seeds
// that module via the require cache, so this require path is load-bearing.

const path = require('node:path');
const { createMuseState, MuseError } = require('./contract.js');

// Resolved path of the FR-MUSE-002 dispatch module. The hermetic seam seeds
// this exact path in require.cache; packages/proxy may not exist on disk,
// and Node resolves filenames before consulting the cache, so check the
// cache first and only fall back to a real require when it lands on disk.
const DISPATCH_PATH = path.resolve(__dirname, '../../../proxy/lib/dispatch.js');

const PROXY_TAXONOMY = ['auth', 'upstream', 'timeout', 'schema'];

function dispatchFn() {
  const cached = require.cache[DISPATCH_PATH];
  const mod = cached ? cached.exports : require(DISPATCH_PATH);
  const fn = mod.dispatch || mod;
  if (typeof fn !== 'function') throw new MuseError('upstream', 'segment dispatch unavailable');
  return fn;
}

function schemaError(detail) {
  const err = new MuseError('schema', detail);
  err.reason = detail;
  return err;
}

function parsePolygon(match) {
  const mask = match && match.mask;
  if (!mask || mask.encoding !== 'polygon' || !Array.isArray(mask.points) || mask.points.length === 0) {
    throw schemaError('unparseable-mask');
  }
  for (const pt of mask.points) {
    if (
      !Array.isArray(pt) ||
      pt.length !== 2 ||
      !pt.every((n) => typeof n === 'number' && Number.isFinite(n))
    ) {
      throw schemaError('unparseable-mask');
    }
  }
  return mask.points;
}

function toTaxonomyError(err) {
  if (err && PROXY_TAXONOMY.includes(err.code)) return err;
  const detail = err && err.message ? err.message : String(err);
  const wrapped = new MuseError('upstream', detail);
  wrapped.reason = detail;
  return wrapped;
}

// segmentAt({ imageUrl, x, y, prompt, preset? }) — tap point → segment route,
// polygon mask parse, preset-name-only forwarding of allow-listed params.
async function segmentAt(args) {
  const { imageUrl, x, y, prompt, preset } = args || {};
  const state = createMuseState();
  state.transition('loading');

  // Allow-listed params only: caller extras (model ids, endpoints, raw x/y)
  // are never spread into the dispatch request.
  const req = {
    route: 'segment',
    imageUrl,
    point: [x, y],
    mask_encoding: 'polygon',
  };
  if (prompt !== undefined) req.prompt = prompt;
  if (preset !== undefined && typeof preset === 'string') req.preset = preset;

  const startedAt = Date.now();
  let res;
  try {
    res = await dispatchFn()(req);
  } catch (err) {
    try { state.transition('error'); } catch (_) { /* state already terminal */ }
    throw toTaxonomyError(err);
  }

  if (!res || (res.status !== undefined && res.status !== 200)) {
    try { state.transition('error'); } catch (_) { /* state already terminal */ }
    throw toTaxonomyError(new MuseError('upstream', `segment upstream status: ${res && res.status}`));
  }

  const body = res.body !== undefined ? res.body : res;
  const matches = body && body.matches;
  if (!Array.isArray(matches) || matches.length === 0) {
    try { state.transition('error'); } catch (_) { /* state already terminal */ }
    throw schemaError('unparseable-mask');
  }

  let polygon;
  try {
    polygon = parsePolygon(matches[0]);
  } catch (err) {
    try { state.transition('error'); } catch (_) { /* state already terminal */ }
    throw err;
  }

  const first = matches[0];
  state.transition('thinking');
  state.transition('ready');
  return {
    name: first.name !== undefined ? first.name : '',
    insight: first.insight !== undefined ? first.insight : '',
    polygon,
    confidence: typeof first.confidence === 'number' ? first.confidence : 0,
    route: 'segment',
    latencyMs: Date.now() - startedAt,
  };
}

const REASONING_EFFORTS = ['minimal', 'low', 'medium', 'high', 'xhigh', 'max'];

// reasonAboutMask({ polygon, imageUrl, prompt, reasoningEffort?, preset?, onThinking }) —
// crop-to-mask → reason route with include_reasoning:true, stream
// reasoning_content deltas to onThinking, resolve final content.
async function reasonAboutMask(args) {
  const { polygon, imageUrl, prompt, reasoningEffort, preset, onThinking } = args || {};
  if (
    !Array.isArray(polygon) ||
    polygon.length === 0 ||
    !polygon.every(
      (pt) => Array.isArray(pt) && pt.length === 2 && pt.every((n) => typeof n === 'number' && Number.isFinite(n)),
    )
  ) {
    throw schemaError('unparseable-mask');
  }
  if (reasoningEffort !== undefined && !REASONING_EFFORTS.includes(reasoningEffort)) {
    throw schemaError('invalid-reasoning-effort');
  }

  const state = createMuseState();
  state.transition('loading');

  // Allow-listed params only: preset-name-only, declared effort; caller
  // extras (model ids, endpoints) are never spread into the request.
  const req = {
    route: 'reason',
    imageUrl,
    mask: { encoding: 'polygon', points: polygon },
    mask_encoding: 'polygon',
    include_reasoning: true,
  };
  if (prompt !== undefined) req.prompt = prompt;
  if (preset !== undefined && typeof preset === 'string') req.preset = preset;
  if (reasoningEffort !== undefined) {
    req.reasoningEffort = reasoningEffort;
    req.params = { ...(req.params || {}), reasoning_effort: reasoningEffort };
  }

  const onDelta = (delta) => {
    const text = delta && delta.reasoning_content !== undefined ? delta.reasoning_content : delta;
    if (typeof text !== 'string') return;
    if (state.status === 'loading') state.transition('thinking');
    if (typeof onThinking === 'function') onThinking(text);
  };

  const startedAt = Date.now();
  let res;
  try {
    res = await dispatchFn()(req, { onDelta });
  } catch (err) {
    try { state.transition('error'); } catch (_) { /* state already terminal */ }
    throw toTaxonomyError(err);
  }

  if (!res || (res.status !== undefined && res.status !== 200)) {
    try { state.transition('error'); } catch (_) { /* state already terminal */ }
    throw toTaxonomyError(new MuseError('upstream', `reason upstream status: ${res && res.status}`));
  }

  const body = res.body !== undefined ? res.body : res;
  const content = body && body.content;
  if (typeof content !== 'string') {
    try { state.transition('error'); } catch (_) { /* state already terminal */ }
    throw schemaError('unparseable-content');
  }

  if (state.status === 'loading') state.transition('thinking');
  state.transition('ready');
  return {
    content,
    route: 'reason',
    latencyMs: Date.now() - startedAt,
  };
}

module.exports = { segmentAt, reasonAboutMask };
