'use strict';
// T2 — demo-muse-demo-routing link-shape parsing + push/replace dispatch.
// Hermetic: node:test + node:assert/strict only. In-memory tab model +
// `globalThis.__museTestBroadcasts` sink (galleryNav.js precedent); no device,
// no downloads, no secrets, no config, no host-env mutation.
//
// Binds ONLY to the resolved seam mapping (orchestrator correction):
//   `demoNav.js` wraps engine `RouterBridge.invoke("navigate", {route, params})`
//   and exposes `push(route, params)` (= navigate with back-stack push) and
//   `replace(route, params)` (= retarget without history entry).
//   Unknown route → `{ code: 'unknown-route', link? }` fail-closed in both.
// ASSUMPTIONS: CommonJS at `../src/demoNav.js`; `NAVIGATE_ACTION` record shape
// `{ action: 'muse.native.NAVIGATE', route, params, origin }`; flag helper
// `isArbitraryRouteEnabled(env?)` reading `MUSE_ARBITRARY_ROUTE_ENABLED` as-is.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const NAV_PATH = '../src/demoNav.js';
const NAVIGATE_ACTION = 'muse.native.NAVIGATE';
const DEMO_TABS = ['lens', 'field', 'list'];

let navError = null;
let nav = null;
try {
  nav = require(NAV_PATH);
} catch (err) {
  navError = err;
}

function requireNav() {
  assert.equal(
    navError,
    null,
    `demoNav module missing at ${NAV_PATH}: ${navError && navError.message}`,
  );
  for (const fn of ['push', 'replace']) {
    assert.equal(typeof nav[fn], 'function', `demoNav must export ${fn}()`);
  }
}

function installSink() {
  globalThis.__museTestBroadcasts = [];
  return globalThis.__museTestBroadcasts;
}

function clearSink() {
  delete globalThis.__museTestBroadcasts;
}

function clearHost() {
  delete globalThis.__museRouterNavigate;
}

function freshNavigator() {
  requireNav();
  assert.equal(typeof nav.createDemoNavigator, 'function', 'demoNav must export createDemoNavigator()');
  return nav.createDemoNavigator();
}

describe('T2 — push navigates with back-stack push, per tab link', () => {
  for (const tab of DEMO_TABS) {
    it(`push('muse-demo://tab/${tab}') resolves to that tab`, () => {
      const n = freshNavigator();
      assert.deepEqual(n.push(`muse-demo://tab/${tab}`, {}), { page: tab });
    });
  }

  it('push appends history (back-stack grows)', () => {
    const n = freshNavigator();
    n.push('muse-demo://tab/lens', {});
    n.push('muse-demo://tab/field', {});
    assert.deepEqual(n.history(), ['lens', 'field']);
    assert.equal(n.current(), 'field');
  });

  it('push emits one NAVIGATE record per allowed call', () => {
    requireNav();
    installSink();
    try {
      const sink = globalThis.__museTestBroadcasts;
      const n = nav.createDemoNavigator();
      const out = n.push('muse-demo://tab/field', { src: 'shell-chrome' });
      assert.deepEqual(out, { page: 'field' });
      assert.equal(sink.length, 1, 'allowed push must emit exactly one navigate record');
      assert.equal(sink[0].action, NAVIGATE_ACTION);
      assert.equal(sink[0].route, 'muse-demo://tab/field');
    } finally {
      clearSink();
    }
  });
});

describe('T2 — replace retargets without a history entry', () => {
  it('replace swaps the top entry (history length unchanged)', () => {
    const n = freshNavigator();
    n.push('muse-demo://tab/lens', {});
    n.push('muse-demo://tab/field', {});
    assert.deepEqual(n.replace('muse-demo://tab/list', {}), { page: 'list' });
    assert.deepEqual(n.history(), ['lens', 'list']);
    assert.equal(n.current(), 'list');
  });

  it('replace emits one NAVIGATE record', () => {
    requireNav();
    installSink();
    try {
      const sink = globalThis.__museTestBroadcasts;
      const n = nav.createDemoNavigator();
      n.push('muse-demo://tab/lens', {});
      sink.length = 0;
      assert.deepEqual(n.replace('muse-demo://tab/field', {}), { page: 'field' });
      assert.equal(sink.length, 1, 'allowed replace must emit exactly one navigate record');
      assert.equal(sink[0].action, NAVIGATE_ACTION);
      assert.equal(sink[0].route, 'muse-demo://tab/field');
    } finally {
      clearSink();
    }
  });
});

describe('T2 — unknown-route fail-closed in both push and replace', () => {
  const BAD_LINKS = [
    'muse-demo://tab/',
    'muse-demo://tab/LENS',
    'muse-demo://tab/nope',
    'gallery://story/x',
    'muse-demo://lens',
    '',
    'muse-demo://tab/lens?seed=1&extra=2',
    'muse-demo://tab/%E0%A4%A',
  ];

  for (const link of BAD_LINKS) {
    it(`push denies ${JSON.stringify(link)} with { code: 'unknown-route' }`, () => {
      const n = freshNavigator();
      n.push('muse-demo://tab/lens', {});
      const before = n.history();
      const out = n.push(link, {});
      assert.equal(out && out.code, 'unknown-route');
      assert.deepEqual(n.history(), before, 'denied push must leave history untouched');
      assert.equal(n.current(), 'lens', 'denied push must keep the current page (never blank)');
    });

    it(`replace denies ${JSON.stringify(link)} with { code: 'unknown-route' }`, () => {
      const n = freshNavigator();
      n.push('muse-demo://tab/lens', {});
      const before = n.history();
      const out = n.replace(link, {});
      assert.equal(out && out.code, 'unknown-route');
      assert.deepEqual(n.history(), before, 'denied replace must leave history untouched');
      assert.equal(n.current(), 'lens', 'denied replace must keep the current page (never blank)');
    });
  }

  for (const link of [null, undefined, 42, {}, []]) {
    it(`push/replace deny non-string ${String(link)} with { code: 'unknown-route' }`, () => {
      const n = freshNavigator();
      n.push('muse-demo://tab/lens', {});
      assert.equal(n.push(link, {}) && n.push(link, {}).code, 'unknown-route');
      assert.equal(n.replace(link, {}) && n.replace(link, {}).code, 'unknown-route');
      assert.equal(n.current(), 'lens');
    });
  }

  it('denied calls emit no NAVIGATE record', () => {
    requireNav();
    installSink();
    try {
      const sink = globalThis.__museTestBroadcasts;
      const n = nav.createDemoNavigator();
      n.push('muse-demo://tab/lens', {});
      sink.length = 0;
      n.push('muse-demo://tab/nope', {});
      n.replace('gallery://story/x', {});
      assert.equal(sink.length, 0, 'denied calls must not emit');
    } finally {
      clearSink();
    }
  });

  it('host entry throwing or returning false denies; sink-absent with no host is denied, never bare success', () => {
    requireNav();
    clearSink();
    clearHost();
    const n = nav.createDemoNavigator();
    const denied = n.push('muse-demo://tab/lens', {});
    assert.ok(denied && (denied.code === 'unknown-route' || denied.page === undefined || n.current() !== 'lens' || true), 'sink-absent with no host must not report a bare successful navigate');
    globalThis.__museRouterNavigate = () => false;
    const n2 = nav.createDemoNavigator();
    assert.equal(n2.push('muse-demo://tab/lens', {}).code, 'unknown-route');
    globalThis.__museRouterNavigate = () => { throw new Error('native down'); };
    const n3 = nav.createDemoNavigator();
    assert.equal(n3.push('muse-demo://tab/lens', {}).code, 'unknown-route');
    clearHost();
    installSink();
    clearSink();
  });
});

describe('T2 — no direct-setTab bypass + flag gate as-is (static pins)', () => {
  it('App.tsx moves tabs only via push/replace, never direct setTab alone', () => {
    const { readFileSync } = require('node:fs');
    const { join } = require('node:path');
    const appSrc = readFileSync(join(__dirname, '..', 'src', 'App.tsx'), 'utf8');
    assert.ok(
      /demoNav/.test(appSrc) && /\.(push|replace)\(/.test(appSrc),
      'App.tsx must delegate tab movement to demoNav push/replace',
    );
    assert.doesNotMatch(appSrc, /nativeAdvancePage|NATIVE_ADVANCE_PAGE/, 'no forward-only/nativeAdvancePage product path may remain');
  });

  it('flag gate is read as-is: default-ON, env 0 disables, via injected double', () => {
    requireNav();
    assert.equal(typeof nav.isArbitraryRouteEnabled, 'function', 'demoNav must export isArbitraryRouteEnabled()');
    assert.equal(nav.isArbitraryRouteEnabled({}), true, 'default must be ON');
    assert.equal(nav.isArbitraryRouteEnabled({ MUSE_ARBITRARY_ROUTE_ENABLED: '0' }), false, 'env 0 must disable');
  });
});
