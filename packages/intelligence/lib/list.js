'use strict';
// MuseList summary→groups→items pipeline (AC-MUSE-014, RULE-MUSE-007).
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
  if (typeof fn !== 'function') throw new MuseError('upstream', 'structure dispatch unavailable');
  return fn;
}

function schemaError(detail) {
  const err = new MuseError('schema', detail);
  err.reason = detail;
  return err;
}

function toTaxonomyError(err) {
  if (err && PROXY_TAXONOMY.includes(err.code)) return err;
  const detail = err && err.message ? err.message : String(err);
  const wrapped = new MuseError('upstream', detail);
  wrapped.reason = detail;
  return wrapped;
}

function validateHierarchy(body) {
  if (!body || typeof body.summary !== 'string' || !Array.isArray(body.groups)) {
    throw schemaError('schema-violation: invalid summary/groups');
  }
  for (const g of body.groups) {
    if (!g || typeof g.label !== 'string' || typeof g.sentiment !== 'string' || !Array.isArray(g.items)) {
      throw schemaError('schema-violation: invalid group');
    }
    for (const item of g.items) {
      if (!item || typeof item.id !== 'string' || typeof item.text !== 'string') {
        throw schemaError('schema-violation: invalid item');
      }
    }
  }
  return {
    summary: body.summary,
    groups: body.groups.map((g) => ({
      label: g.label,
      sentiment: g.sentiment,
      items: g.items.map((item) => ({ id: item.id, text: item.text })),
    })),
  };
}

// structureData({ data, renderMode, prompt, preset? }) — structure route with
// strict schema selected by renderMode; prompt steers the fill;
// preset-name-only forwarding of allow-listed params.
async function structureData(args) {
  const { data, renderMode, prompt, preset } = args || {};
  const state = createMuseState();
  state.transition('loading');

  // Allow-listed params only: caller extras (model ids, endpoints) are
  // never spread into the dispatch request.
  const req = {
    route: 'structure',
    renderMode,
    schema: renderMode,
    strict: true,
  };
  if (data !== undefined) req.data = data;
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
    throw toTaxonomyError(new MuseError('upstream', `structure upstream status: ${res && res.status}`));
  }

  const body = res.body !== undefined ? res.body : res;
  let rendered;
  try {
    rendered = validateHierarchy(body);
  } catch (err) {
    try { state.transition('error'); } catch (_) { /* state already terminal */ }
    throw err;
  }

  state.transition('thinking');
  state.transition('ready');
  return {
    ...rendered,
    route: 'structure',
    latencyMs: Date.now() - startedAt,
  };
}

// remode({ current:{scrollOffset,expandedIds}, hierarchy, renderMode, ... }) —
// re-requests structure with the new mode on the same data+host (no remount);
// returns the preserved offset + intersection of retained expanded ids
// (AC-MUSE-015). Retained membership is item `id` presence in the fresh
// hierarchy's groups; expanded-id order follows current.expandedIds.
async function remode(args) {
  const { current, renderMode, data } = args || {};
  const scrollOffset = current && current.scrollOffset !== undefined ? current.scrollOffset : 0;
  const expandedIds = current && Array.isArray(current.expandedIds) ? current.expandedIds : [];

  // Same data, new mode, same host object (caller-held host is never touched,
  // so identity `===` is preserved — no remount). Host is intentionally not
  // forwarded into the dispatch request; structureData carries data+mode.
  const req = { renderMode };
  if (data !== undefined) req.data = data;
  const fresh = await structureData(req);

  const retained = new Set();
  for (const g of (fresh && fresh.groups) || []) {
    for (const item of (g && g.items) || []) {
      if (item && typeof item.id === 'string') retained.add(item.id);
    }
  }
  return {
    scrollOffset,
    expandedIds: expandedIds.filter((id) => retained.has(id)),
  };
}

module.exports = { structureData, remode };
