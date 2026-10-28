'use strict';
// T1 — versioned proxy config allow-list loader. Fails closed.
// Pure local validation: reads + parses one JSON file, checks it against the
// Q-MUSE-001 verified allow-list, returns the route table. No network surface,
// no key access (never reads env or secret stores).

const fs = require('node:fs');

// Q-MUSE-001 verified model IDs only (decided 2026-10-27). Anything else —
// e.g. gpt-4, meta-seamless, meta-tts — is rejected as non-muse-model.
// style_preset/seed stay assumed/unverified and MUST NOT appear here.
const ALLOW_MODELS = new Set([
  'sam-3.1',
  'muse-spark-1.3',
  'muse-voice-transcribe-1.0',
  'muse-image',
]);

// Closed per-route param allow-lists (Q-MUSE-001 verified only).
const ALLOW_PARAMS = {
  segment: new Set(['mask_encoding']),
  reason: new Set(['reasoning_effort', 'include_reasoning', 'strict']),
  structure: new Set(['reasoning_effort', 'include_reasoning', 'strict']),
  transcribe: new Set(['mode']),
  'generate-image': new Set(['prompt', 'size']),
};

const ALLOW_STREAMS = new Set(['none', 'sse', 'progressive']);

class ConfigError extends Error {
  // name: the offending key (route, model id, param, or path/shape pointer).
  // reason: unknown-route | non-muse-model | unlisted-param | bad-shape.
  constructor(name, reason) {
    super(`${reason}: ${name}`);
    this.name = String(name);
    this.reason = reason;
  }
}

function fail(name, reason) {
  throw new ConfigError(name, reason);
}

function isRecord(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function loadConfig(configPath) {
  let raw;
  try {
    raw = fs.readFileSync(configPath, 'utf8');
  } catch {
    // Missing/unreadable file fails closed without leaking ENOENT details.
    fail(String(configPath), 'bad-shape');
  }

  let doc;
  try {
    doc = JSON.parse(raw);
  } catch {
    fail(String(configPath), 'bad-shape');
  }

  if (!isRecord(doc) || typeof doc.version !== 'number' || !isRecord(doc.routes)) {
    fail(!isRecord(doc) || typeof doc.version !== 'number' ? 'version' : 'routes', 'bad-shape');
  }

  const routes = {};
  for (const routeName of Object.keys(doc.routes)) {
    if (!Object.prototype.hasOwnProperty.call(ALLOW_PARAMS, routeName)) {
      fail(routeName, 'unknown-route');
    }
    const entry = doc.routes[routeName];
    if (!isRecord(entry)) {
      fail(routeName, 'bad-shape');
    }
    // Closed model allow-list: checked before any network or key access.
    if (typeof entry.model !== 'string' || !ALLOW_MODELS.has(entry.model)) {
      fail(typeof entry.model === 'string' ? entry.model : routeName, 'non-muse-model');
    }
    // Closed param allow-list per route.
    if (!isRecord(entry.params)) {
      fail(`${routeName}.params`, 'bad-shape');
    }
    for (const param of Object.keys(entry.params)) {
      if (!ALLOW_PARAMS[routeName].has(param)) {
        fail(param, 'unlisted-param');
      }
    }
    if (typeof entry.stream !== 'string' || !ALLOW_STREAMS.has(entry.stream)) {
      fail(`${routeName}.stream`, 'bad-shape');
    }
    if (typeof entry.timeoutMs !== 'number' || !Number.isFinite(entry.timeoutMs)) {
      fail(`${routeName}.timeoutMs`, 'bad-shape');
    }
    if (typeof entry.retry !== 'number' || !Number.isFinite(entry.retry)) {
      fail(`${routeName}.retry`, 'bad-shape');
    }
    routes[routeName] = {
      model: entry.model,
      params: { ...entry.params },
      stream: entry.stream,
      timeoutMs: entry.timeoutMs,
      retry: entry.retry,
    };
  }

  return { version: doc.version, routes };
}

module.exports = { loadConfig, ConfigError };
