'use strict';
// T2 — proxy dispatch: allow-list enforcement, key custody, per-route
// timeout+retry, SSE/chunked passthrough, turnId/targetId semantics.
// Validates against the T1 allow-list (loaded via load-config) before any
// upstream call; only documented ProxyError codes surface.

const path = require('node:path');
const { ProxyError } = require('./errors');
const { loadConfig } = require('./load-config');

// Closed per-route param allow-lists (Q-MUSE-001 verified only; mirrors T1).
const ALLOW_PARAMS = {
  segment: new Set(['mask_encoding']),
  reason: new Set(['reasoning_effort', 'include_reasoning', 'strict']),
  structure: new Set(['reasoning_effort', 'include_reasoning', 'strict']),
  transcribe: new Set(['mode']),
  'generate-image': new Set(['prompt', 'size']),
};

// Presets shaped as raw non-Muse model ids map to non-muse-model;
// any other unknown key maps to unknown-preset.
const NON_MUSE_PRESET_RE = /^(gpt[-\d]|o1[-\.]|openai|meta-(seamless|tts))/i;
const KNOWN_PRESETS = new Set([]);

let cachedConfig;
function getConfig() {
  if (cachedConfig) return cachedConfig;
  cachedConfig = loadConfig(path.join(__dirname, '..', 'proxy.config.json'));
  return cachedConfig;
}

function isRecord(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isTimeoutError(err) {
  if (!err || typeof err !== 'object') return false;
  if (err.code === 'timeout') return true;
  const msg = typeof err.message === 'string' ? err.message : '';
  return /timed?\s*out/i.test(msg);
}

function fail(code, route, retryable) {
  throw new ProxyError(code, route, retryable);
}

async function dispatch(req, ctx) {
  const route = req && typeof req === 'object' ? req.route : undefined;
  const cfg = getConfig();

  // Unknown route pre-upstream.
  if (typeof route !== 'string' || !Object.prototype.hasOwnProperty.call(cfg.routes, route)) {
    fail('unknown-route', String(route), false);
  }
  const allowed = ALLOW_PARAMS[route];
  if (!allowed) fail('unknown-route', String(route), false);

  // Params must be an object bag when present; null/non-object is a schema error.
  // Undefined is treated as an empty bag.
  let params = req.params;
  if (params === undefined) {
    params = {};
  }
  if (!isRecord(params)) {
    fail('schema', route, false);
  }

  // Unlisted params pre-upstream.
  for (const key of Object.keys(params)) {
    if (!allowed.has(key)) {
      fail('unlisted-param', route, false);
    }
  }

  // Preset validation pre-upstream.
  if (req.preset !== undefined) {
    if (typeof req.preset !== 'string' || req.preset.length === 0) {
      fail('unknown-preset', route, false);
    } else if (KNOWN_PRESETS.has(req.preset)) {
      // known preset: passes validation
    } else if (NON_MUSE_PRESET_RE.test(req.preset)) {
      fail('non-muse-model', route, false);
    } else {
      fail('unknown-preset', route, false);
    }
  }

  // Key custody: injectKey failure maps to auth with zero upstream calls.
  // Never echoes key material into the error.
  let key;
  try {
    key = ctx.injectKey();
  } catch {
    fail('auth', route, false);
  }

  const entry = cfg.routes[route];
  const maxRetries = typeof entry.retry === 'number' ? entry.retry : 1;
  const maxAttempts = maxRetries + 1;

  const body = { params, key };
  if (req.preset !== undefined) body.preset = req.preset;
  if (req.turnId !== undefined) body.turnId = req.turnId;
  // targetId is bound at commit only: forwarded upstream, never echoed back.
  if (req.targetId !== undefined) body.targetId = req.targetId;

  let lastError;
  for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
    try {
      const res = await ctx.upstream(route, body);
      const out = { status: res.status, body: res.body };
      // turnId opaque passthrough, never invented; stale turnId dropped
      // per the caller-supplied latest-turn check.
      if (req.turnId !== undefined) {
        if (ctx.latestTurnId !== undefined && req.turnId !== ctx.latestTurnId) {
          // stale: drop, do not echo
        } else {
          out.turnId = req.turnId;
        }
      }
      return out;
    } catch (err) {
      lastError = err;
      // Already-taxonomized errors pass through (only documented codes).
      if (err instanceof ProxyError) {
        if (attempt < maxAttempts && err.retryable === true) continue;
        throw err;
      }
      const timeout = isTimeoutError(err);
      const retryable = err && err.retryable === false ? false : true;
      if (attempt < maxAttempts && retryable) continue;
      if (timeout) {
        throw new ProxyError('timeout', route, true);
      }
      throw new ProxyError('upstream', route, retryable);
    }
  }
  // Exhausted without a result: map the last failure.
  if (isTimeoutError(lastError)) throw new ProxyError('timeout', route, true);
  const retryable = lastError && lastError.retryable === false ? false : true;
  throw new ProxyError('upstream', route, retryable);
}

module.exports = { dispatch };
