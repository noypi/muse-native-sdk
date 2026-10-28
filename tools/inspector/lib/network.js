'use strict';
// Network inspector view (FR-DX-003 / AC-DX-012 / TC-DX-012).
// Append-only in-memory request log; describeRequest applies an injected
// redactor to response bodies (default masks authorization/token fields).
// Unknown id rejects with a structured NFR-DX-002 diagnostic.

const CACHE_DECISIONS = new Set(['hit', 'miss', 'revalidate', 'bypass']);

const requests = new Map();
let nextId = 1;

function badRequestError(detail) {
  const err = new Error(`malformed network request entry in inspector view (DX-003 / AC-DX-012): ${detail}`);
  err.code = 'E_INSPECTOR_BAD_REQUEST';
  err.cause = `malformed network request entry in inspector view (DX-003 / AC-DX-012): ${detail}`;
  err.action = 'Record a well-formed entry with url, method, timings, cacheDecision (DX-003).';
  return err;
}

function unknownRequestError() {
  const err = new Error('unknown network request in inspector view (DX-003 / AC-DX-012)');
  err.code = 'E_INSPECTOR_UNKNOWN_REQUEST';
  err.cause = 'unknown network request id in inspector view (DX-003 / AC-DX-012)';
  err.action = 'Describe a recorded request id from the inspector network log (DX-003).';
  return err;
}

function isPlainObject(value) {
  if (value === null || typeof value !== 'object') return false;
  const proto = Object.getPrototypeOf(value);
  return proto === Object.prototype || proto === null;
}

function isValidUrl(url) {
  if (typeof url !== 'string' || url.length === 0) return false;
  try {
    const parsed = new URL(url);
    return parsed.protocol === 'http:' || parsed.protocol === 'https:';
  } catch (e) {
    void e;
    return false;
  }
}

function clone(value) {
  if (Array.isArray(value)) return value.map(clone);
  if (isPlainObject(value)) {
    const out = {};
    for (const key of Object.keys(value)) out[key] = clone(value[key]);
    return out;
  }
  return value;
}

function maskValue(key) {
  const lower = String(key).toLowerCase();
  return lower === 'authorization' || lower === 'token';
}

function defaultRedact(body) {
  if (Array.isArray(body)) return body.map(defaultRedact);
  if (isPlainObject(body)) {
    const out = {};
    for (const key of Object.keys(body)) {
      out[key] = maskValue(key) ? '[REDACTED]' : defaultRedact(body[key]);
    }
    return out;
  }
  return body;
}

function recordRequest(entry) {
  if (!isPlainObject(entry)) throw badRequestError('entry must be an object');
  if (!isValidUrl(entry.url)) throw badRequestError('url must be an http(s) URL');
  if (typeof entry.method !== 'string' || entry.method.length === 0) {
    throw badRequestError('method must be a non-empty string');
  }
  if (!isPlainObject(entry.timings)) throw badRequestError('timings must be an object');
  if (!CACHE_DECISIONS.has(entry.cacheDecision)) {
    throw badRequestError('cacheDecision must be hit|miss|revalidate|bypass');
  }
  const id = `req-${nextId}`;
  nextId += 1;
  requests.set(id, {
    id,
    url: entry.url,
    method: entry.method,
    timings: clone(entry.timings),
    cacheDecision: entry.cacheDecision,
    body: clone(entry.body),
  });
  return { id };
}

function describeRequest(id, redact) {
  if (typeof id !== 'string' || !requests.has(id)) throw unknownRequestError();
  const stored = requests.get(id);
  const bodyCopy = clone(stored.body);
  let body;
  if (redact === undefined) {
    body = defaultRedact(bodyCopy);
  } else {
    if (typeof redact !== 'function') throw badRequestError('redact must be a function');
    body = redact(bodyCopy);
  }
  return {
    id: stored.id,
    url: stored.url,
    method: stored.method,
    timings: clone(stored.timings),
    cacheDecision: stored.cacheDecision,
    body,
  };
}

function clearRequests() {
  const removed = requests.size;
  requests.clear();
  nextId = 1;
  return { removed };
}

module.exports = { recordRequest, describeRequest, clearRequests };
