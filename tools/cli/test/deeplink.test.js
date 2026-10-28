'use strict';
// T4 — FR-DX-008 / AC-DX-035 / TC-DX-035 (Batch 1).
// Hermetic: node:test + node:assert/strict only. Nav resolution is opaque —
// a stub resolver is injected via an optional __setNavResolver test hook (or
// falls back to the module's bundled fake); no nav internals, no device.
// Interface under test (plan verbatim):
//   dispatchRoute(route: string, opts?: {coldStart?: boolean}) =>
//     ({status:'resolved', route: string} |
//      {status:'routeError', route: string, cause: string, action: string})
//   exported from tools/cli/lib/deeplink-tester.js; unknown route returns
//   structured symbolized route error, never a raw stack.
// ASSUMPTION: BUILD-001 facade untouched; known routes resolve identically
// warm and cold; unknown routes yield routeError naming the route + spec id
// with cause/action and no raw stack text.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'deeplink-tester.js');

function load() {
  return require(LIB);
}

function stubNav(mod) {
  if (mod && typeof mod.__setNavResolver === 'function') {
    mod.__setNavResolver((route) => {
      if (route === 'muse://home' || route === 'muse://settings') {
        return { ok: true };
      }
      return { ok: false, symbol: 'NAV_UNKNOWN_ROUTE', hint: 'check route spelling' };
    });
  }
}

describe('deeplink dispatchRoute (T4)', () => {
  beforeEach(() => {
    delete require.cache[LIB];
  });

  it('known route resolves warm', () => {
    const mod = load();
    stubNav(mod);
    const res = mod.dispatchRoute('muse://home');
    assert.equal(res.status, 'resolved');
    assert.equal(res.route, 'muse://home');
  });

  it('known route resolves cold', () => {
    const mod = load();
    stubNav(mod);
    const res = mod.dispatchRoute('muse://settings', { coldStart: true });
    assert.equal(res.status, 'resolved');
    assert.equal(res.route, 'muse://settings');
  });

  it('unknown route yields structured symbolized route error', () => {
    const mod = load();
    stubNav(mod);
    const res = mod.dispatchRoute('muse://nope-missing');
    assert.equal(res.status, 'routeError');
    assert.equal(res.route, 'muse://nope-missing');
    assert.equal(typeof res.cause, 'string');
    assert.equal(typeof res.action, 'string');
    assert.match(String(res.cause), /NAV_|unknown route|AC-DX-035/i);
  });

  it('unknown route cold also yields routeError, never throws', () => {
    const mod = load();
    stubNav(mod);
    const res = mod.dispatchRoute('muse://nope-missing', { coldStart: true });
    assert.equal(res.status, 'routeError');
  });

  it('route error never leaks a raw stack', () => {
    const mod = load();
    stubNav(mod);
    const res = mod.dispatchRoute('muse://nope-missing');
    const text = JSON.stringify(res);
    assert.ok(!/\n\s*at\s/.test(text), 'must not contain a raw stack trace');
    assert.ok(!/node_modules\/.+\.js:\d+/.test(text), 'must not contain frame paths');
  });

  it('empty route is a structured routeError, not a throw', () => {
    const mod = load();
    stubNav(mod);
    const res = mod.dispatchRoute('');
    assert.equal(res.status, 'routeError');
    assert.equal(typeof res.cause, 'string');
    assert.equal(typeof res.action, 'string');
  });
});
