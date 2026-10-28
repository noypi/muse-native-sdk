'use strict';
// MuseField privacy-auto routing (AC-MUSE-011, RULE-MUSE-007) + fieldError (AC-MUSE-013).
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
  if (typeof fn !== 'function') throw new MuseError('upstream', 'field dispatch unavailable');
  return fn;
}

function toTaxonomyError(err) {
  if (err && PROXY_TAXONOMY.includes(err.code)) return err;
  const detail = err && err.message ? err.message : String(err);
  const wrapped = new MuseError('upstream', detail);
  wrapped.reason = detail;
  return wrapped;
}

// classifyAndRoute({ text, context, privacy:"auto", prompt, preset? }) —
// every input class routes to the Muse reason route; prompt is the task
// instruction, context drives routing only (the two never mix),
// preset-name-only forwarding of allow-listed params.
async function classifyAndRoute(args) {
  const { text, context, prompt, preset } = args || {};
  const state = createMuseState();
  state.transition('loading');

  // Allow-listed params only: caller extras (model ids, endpoints) are
  // never spread into the dispatch request.
  const req = { route: 'reason' };
  if (text !== undefined) req.text = text;
  if (context !== undefined) req.context = context;
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
    throw toTaxonomyError(new MuseError('upstream', `field upstream status: ${res && res.status}`));
  }

  const body = res.body !== undefined ? res.body : res;
  const answer = body && body.answer;
  const classification = body && body.classification;
  const reason = body && body.reason;
  if (typeof answer !== 'string' || typeof classification !== 'string' || typeof reason !== 'string') {
    try { state.transition('error'); } catch (_) { /* state already terminal */ }
    const err = new MuseError('schema', 'unparseable-field');
    err.reason = 'unparseable-field';
    throw err;
  }

  // Expose the classification reason from the dispatch call, qualified by
  // the routing context when the proxy reason does not already carry it —
  // this keeps every input class distinguishable by its routing reason.
  let reasonOut = reason;
  if (typeof context === 'string' && context.length > 0 && !reason.includes(context)) {
    reasonOut = `${context}→${reason}`;
  }

  state.transition('thinking');
  state.transition('ready');
  return {
    route: 'reason',
    reason: reasonOut,
    classification,
    latencyMs: Date.now() - startedAt,
    answer,
  };
}

const RETRYABLE = { auth: false, upstream: true, timeout: true, schema: false };

// fieldError(code, detail) — taxonomy-only named error states; never
// partial text / raw upstream (no answer/text payload); keeps existing text
// by carrying no text payload on the error state.
function fieldError(code, detail) {
  if (!PROXY_TAXONOMY.includes(code)) {
    throw new MuseError('schema', `unknown field error code: ${String(code)}`);
  }
  const message = detail !== undefined && detail !== null && String(detail).length > 0
    ? `${code}: ${String(detail)}`
    : `${code} error`;
  return {
    state: 'error',
    code,
    message,
    retryable: RETRYABLE[code] === true,
  };
}

// bindField({ value, selection:{start,end}, focused, verdict:{route,reason,latencyMs} }) —
// pure rebind (AC-MUSE-012): identical text/selection/focus in and out with
// the new route badge attached; never drops/mutates text on route change
// (muse-core write-through semantics, caret preserved); inputs never mutated,
// no aliased objects escape.
function bindField(args) {
  const { value, selection, focused, verdict } = args || {};
  const selectionOut = selection !== undefined && selection !== null
    ? { start: selection.start, end: selection.end }
    : selection;
  const v = verdict || {};
  return {
    value,
    selection: selectionOut,
    focused,
    badge: { route: v.route, reason: v.reason, latencyMs: v.latencyMs },
  };
}

module.exports = { classifyAndRoute, fieldError, bindField };
