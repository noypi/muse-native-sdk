'use strict';
// Batch 1 (T1) — @muse/router facade core: createRoutes + createRouter
// (push/replace/back/go, guards, named denials).
// Specs: FR-NAV-014, AC-NAV-039, AC-NAV-040 (TC-NAV-039/040).
// Hermetic: node:test + node:assert/strict only. Fake transport injected
// in-process; no network, no secrets, no app config, no live services.
// RED state: `../src/index.js` does not exist yet (coder owns it), so this
// file fails at import until T1 is implemented.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createRoutes, createRouter } = require('../src/index.js');

// --- Fakes (in-test only) -------------------------------------------------

function fakeTransport() {
  const calls = [];
  return {
    calls,
    invoke: async (method, payload) => {
      calls.push({ method, payload });
      return { ok: true };
    },
  };
}

function fixtures(transport) {
  const routes = createRoutes([
    { id: 'home', link: 'muse-demo://tab/home', params: {} },
    { id: 'lens', link: 'muse-demo://tab/lens', params: { id: 'string' } },
    {
      id: 'guarded',
      link: 'muse-demo://tab/guarded',
      params: {},
      beforeEnter: () => false,
    },
  ]);
  const router = createRouter({ routes, home: 'home', transport });
  return { routes, router };
}

// --- AC-NAV-039: createRoutes / createRouter API ----------------------------

describe('createRoutes (AC-NAV-039)', () => {
  it('builds a route table preserving id/link/params-schema/beforeEnter', () => {
    const transport = fakeTransport();
    const { routes } = fixtures(transport);
    assert.ok(routes);
    for (const id of ['home', 'lens', 'guarded']) {
      assert.ok(routes[id], `route table carries '${id}'`);
    }
    assert.equal(routes.lens.link, 'muse-demo://tab/lens');
    assert.deepEqual(routes.lens.params, { id: 'string' });
    assert.equal(typeof routes.guarded.beforeEnter, 'function');
  });

  it('push(string) navigates and invokes transport with route+params', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    const res = await router.push('lens');
    assert.deepEqual(res, { ok: true });
    assert.equal(transport.calls.length, 1);
    assert.equal(transport.calls[0].method, 'navigate');
    assert.equal(transport.calls[0].payload.route, 'lens');
  });

  it('push({id, params}) carries typed params to the transport', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    const res = await router.push({ id: 'lens', params: { id: 'p1' } });
    assert.deepEqual(res, { ok: true });
    assert.deepEqual(transport.calls[0].payload.params, { id: 'p1' });
  });

  it('push(fn) resolves the destination from state', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    const res = await router.push(() => 'lens');
    assert.deepEqual(res, { ok: true });
    assert.equal(transport.calls[0].payload.route, 'lens');
  });

  it('replace swaps the current page and invokes transport', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    const res = await router.replace('home');
    assert.deepEqual(res, { ok: true });
    assert.equal(router.current, 'home');
    const last = transport.calls[transport.calls.length - 1];
    assert.equal(last.payload.route, 'home');
  });

  it('beforeEach guard runs before transport invoke', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    const seen = [];
    router.beforeEach((to, from) => {
      seen.push({ to, from });
      return true;
    });
    await router.push('lens');
    assert.equal(seen.length, 1);
    assert.equal(transport.calls.length, 1);
  });
});

// --- AC-NAV-040: navigation semantics + named denials ------------------------

describe('navigation semantics (AC-NAV-040)', () => {
  it('push unknown route denies unknown-route, page unchanged, no invoke', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    const res = await router.push('nope');
    assert.deepEqual(res, { ok: false, code: 'unknown-route' });
    assert.equal(router.current, 'lens');
    assert.equal(transport.calls.length, 1);
  });

  it('push with bad params denies bad-params, page unchanged', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    const res = await router.push({ id: 'lens', params: { id: 42 } });
    assert.deepEqual(res, { ok: false, code: 'bad-params' });
    assert.equal(router.current, 'home');
    assert.equal(transport.calls.length, 0);
  });

  it('replace unknown route denies unknown-route, page unchanged', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    const res = await router.replace('nope');
    assert.deepEqual(res, { ok: false, code: 'unknown-route' });
    assert.equal(router.current, 'lens');
  });

  it('global beforeEach denial returns guard-denied, page unchanged', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    router.beforeEach(() => false);
    const res = await router.push('lens');
    assert.deepEqual(res, { ok: false, code: 'guard-denied' });
    assert.equal(router.current, 'home');
    assert.equal(transport.calls.length, 0);
  });

  it('async beforeEach denial returns guard-denied', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    router.beforeEach(async () => false);
    const res = await router.push('lens');
    assert.deepEqual(res, { ok: false, code: 'guard-denied' });
    assert.equal(router.current, 'home');
  });

  it('per-route beforeEnter denial returns guard-denied, page unchanged', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    const res = await router.push('guarded');
    assert.deepEqual(res, { ok: false, code: 'guard-denied' });
    assert.equal(router.current, 'home');
    assert.equal(transport.calls.length, 0);
  });

  it('back() returns to the previous page (= go(-1))', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    const res = await router.back();
    assert.deepEqual(res, { ok: true });
    assert.equal(router.current, 'home');
  });

  it('go(delta>0) is denied no-forward, page unchanged', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    const res = await router.go(1);
    assert.deepEqual(res, { ok: false, code: 'no-forward' });
    assert.equal(router.current, 'lens');
  });

  it('negative go() clamps at home and never goes blank', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    const res = await router.go(-5);
    assert.deepEqual(res, { ok: true });
    assert.equal(router.current, 'home');
    assert.ok(router.current, 'current page is never blank');
  });

  it('back() at home stays at home (never blank)', async () => {
    const transport = fakeTransport();
    const { router } = fixtures(transport);
    const res = await router.back();
    assert.deepEqual(res, { ok: true });
    assert.equal(router.current, 'home');
  });
});
