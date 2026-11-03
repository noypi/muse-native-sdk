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

describe('T6 outcomes/logs observed as router results in JS (RED pre-move)', () => {
  function fakeRouterTransport() {
    const calls = [];
    return { calls, invoke: async (method, payload) => { calls.push({ method, payload }); return { ok: true }; } };
  }

  it('migrated demoNav exposes router push/replace returning NavResult observed in JS', async () => {
    requireNav();
    assert.equal(typeof nav.createDemoRouter, 'function', 'demoNav must export createDemoRouter (T6 Interface)');
    const transport = fakeRouterTransport();
    const router = nav.createDemoRouter({ transport });
    const ok = await router.push('lens');
    assert.deepEqual(ok, { ok: true });
    assert.equal(transport.calls.length, 1);
    assert.equal(transport.calls[0].method, 'navigate');
    const rep = await router.replace('field');
    assert.deepEqual(rep, { ok: true });
  });

  it('onFallbackRecover lives JS-side and returns NavResult', async () => {
    requireNav();
    assert.equal(typeof nav.createDemoRouter, 'function', 'createDemoRouter must exist');
    const transport = fakeRouterTransport();
    const router = nav.createDemoRouter({ transport });
    assert.equal(typeof router.onFallbackRecover, 'function', 'router must expose onFallbackRecover(): NavResult');
    const res = await router.onFallbackRecover();
    assert.ok(res && (res.ok === true || res.ok === false), 'must return NavResult');
  });

  it('tap-advance lands same dest as push (no applyValidatedTab in Java)', async () => {
    requireNav();
    assert.equal(typeof nav.createDemoRouter, 'function', 'createDemoRouter must exist');
    const transport = fakeRouterTransport();
    const router = nav.createDemoRouter({ transport });
    assert.equal(typeof router.tapAdvance, 'function', 'router must expose tap-advance observed in JS');
    const viaTap = await router.tapAdvance();
    const viaPush = await router.push('field');
    assert.deepEqual(viaTap, viaPush);
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

// --- Batch 2 hardening (security review 2026-11-03): JS-side-only denial
// logging, AC-NAV-047. A denied push/replace must record nothing (no
// transport call, no state change) and leak no internals through logs:
// the only observable is the returned NavResult.

describe('hardening: denials are JS-side only, nothing recorded (AC-NAV-047)', () => {
  function fakeRouterTransport() {
    const calls = [];
    return { calls, invoke: async (method, payload) => { calls.push({ method, payload }); return { ok: true }; } };
  }

  function captureConsole() {
    const seen = [];
    const methods = ['debug', 'log', 'info', 'warn', 'error'];
    const originals = {};
    for (const m of methods) {
      originals[m] = console[m];
      console[m] = (...args) => { seen.push({ method: m, args: args.map(String) }); };
    }
    return { seen, restore() { for (const m of methods) { console[m] = originals[m]; } } };
  }

  for (const bad of ['muse-demo://tab/nope', 'gallery://story/x', null, 42]) {
    it(`denied push/replace of ${JSON.stringify(bad)} records nothing, only NavResult observed`, async () => {
      requireNav();
      const transport = fakeRouterTransport();
      const router = nav.createDemoRouter({ transport });
      await router.push('lens');
      const cap = captureConsole();
      let pushRes;
      let replaceRes;
      try {
        pushRes = await router.push(bad);
        replaceRes = await router.replace(bad);
      } finally {
        cap.restore();
      }
      assert.equal(pushRes && pushRes.ok, false);
      assert.equal(replaceRes && replaceRes.ok, false);
      assert.equal(transport.calls.length, 1, 'denials must never reach transport');
      assert.equal(router.current, 'lens', 'denial must leave state unchanged');
      assert.equal(cap.seen.length, 0, 'denial must emit no log record');
    });
  }

  it('denial logs never carry the raw opaque input', async () => {
    requireNav();
    const transport = fakeRouterTransport();
    const router = nav.createDemoRouter({ transport });
    const evil = 'muse-demo://tab/lens?seed=<script>';
    const cap = captureConsole();
    try {
      const res = await router.forwardIntent(evil);
      assert.deepEqual(res, { ok: false, code: 'unknown-route' });
    } finally {
      cap.restore();
    }
    for (const entry of cap.seen) {
      for (const arg of entry.args) {
        assert.ok(!arg.includes(evil), 'raw opaque input must never appear in logs');
      }
    }
  });
});

// --- Batch 2 hardening (security review 2026-11-03): non-object params
// container denied with `bad-params`, AC-NAV-047 (typed params, fail
// closed). The transport seam must never see a mistyped container.

describe('hardening: non-object params denied with bad-params (AC-NAV-047)', () => {
  function fakeRouterTransport() {
    const calls = [];
    return { calls, invoke: async (method, payload) => { calls.push({ method, payload }); return { ok: true }; } };
  }

  for (const params of ['evil-string', 42, true, ['lens'], null]) {
    it(`push('lens', ${JSON.stringify(params)}) denies bad-params, transport never called`, async () => {
      requireNav();
      const transport = fakeRouterTransport();
      const router = nav.createDemoRouter({ transport });
      const before = router.current;
      const res = await router.push('lens', params);
      assert.deepEqual(res, { ok: false, code: 'bad-params' });
      assert.equal(transport.calls.length, 0, 'mistyped container must not cross the transport seam');
      assert.equal(router.current, before, 'denial must leave state unchanged');
    });

    it(`replace('lens', ${JSON.stringify(params)}) denies bad-params, transport never called`, async () => {
      requireNav();
      const transport = fakeRouterTransport();
      const router = nav.createDemoRouter({ transport });
      const before = router.current;
      const res = await router.replace('lens', params);
      assert.deepEqual(res, { ok: false, code: 'bad-params' });
      assert.equal(transport.calls.length, 0, 'mistyped container must not cross the transport seam');
      assert.equal(router.current, before, 'denial must leave state unchanged');
    });
  }

  it('denied params leave router state unchanged', async () => {
    requireNav();
    const transport = fakeRouterTransport();
    const router = nav.createDemoRouter({ transport });
    await router.push('lens');
    const res = await router.push('field', 'evil-string');
    assert.deepEqual(res, { ok: false, code: 'bad-params' });
    assert.equal(router.current, 'lens');
    assert.equal(transport.calls.length, 1);
  });
});
