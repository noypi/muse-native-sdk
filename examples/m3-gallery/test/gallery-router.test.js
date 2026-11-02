'use strict';

// T4 — gallery-router contract (TC-NAV-043 / AC-NAV-043). CONTRACT ONLY:
// `examples/m3-gallery/src/galleryNav.js` migration happens AFTER the facade.
// This file pins the migration target shape: what galleryNav must satisfy
// once migrated. All navigation assertions go through the FACADE's public
// interface (`createRoutes`/`createRouter`), never gallery internals.
// Hermetic: node:test + assert/strict only, in-memory fake transport,
// no network, no .env, no live services.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createRoutes, createRouter } = require('../../../packages/router/src/index.js');

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

// Gallery-shaped route table, built the way the migrated galleryNav must
// build it: facade `createRoutes` over manifest descriptors.
function galleryTable() {
  return createRoutes([
    { id: 'home', link: 'gallery://home', params: {} },
    { id: 'story', link: 'gallery://story/<id>', params: { seed: 'number' } },
    {
      id: 'guarded-story',
      link: 'gallery://story/<id>',
      params: {},
      beforeEnter: () => false,
    },
  ]);
}

function galleryRouter(transport) {
  return createRouter({ routes: galleryTable(), home: 'home', transport });
}

describe('gallery-router contract (facade interface, gallery shape)', () => {
  it('push({id, params}) resolves against the route table and invokes transport navigate', async () => {
    const transport = fakeTransport();
    const router = galleryRouter(transport);
    const res = await router.push({ id: 'story', params: { seed: 7 } });
    assert.deepEqual(res, { ok: true });
    assert.equal(transport.calls.length, 1);
    assert.deepEqual(transport.calls[0], {
      method: 'navigate',
      payload: { route: 'story', params: { seed: 7 } },
    });
    assert.equal(router.current, 'story');
  });

  it('replace(...) swaps the current entry and invokes transport navigate', async () => {
    const transport = fakeTransport();
    const router = galleryRouter(transport);
    await router.push('story');
    const res = await router.replace({ id: 'home', params: {} });
    assert.deepEqual(res, { ok: true });
    assert.equal(router.current, 'home');
    assert.equal(transport.calls.length, 2);
  });

  it('unknown gallery id denies unknown-route and leaves current unchanged', async () => {
    const transport = fakeTransport();
    const router = galleryRouter(transport);
    await router.push('story');
    const res = await router.push('no-such-story');
    assert.deepEqual(res, { ok: false, code: 'unknown-route' });
    assert.equal(router.current, 'story');
    assert.equal(transport.calls.length, 1);
  });

  it('ill-typed params deny bad-params and leave current unchanged', async () => {
    const transport = fakeTransport();
    const router = galleryRouter(transport);
    const res = await router.push({ id: 'story', params: { seed: 'not-a-number' } });
    assert.deepEqual(res, { ok: false, code: 'bad-params' });
    assert.equal(router.current, 'home');
    assert.equal(transport.calls.length, 0);
  });

  it('guarded gallery route denies guard-denied via beforeEnter and leaves current unchanged', async () => {
    const transport = fakeTransport();
    const router = galleryRouter(transport);
    const res = await router.push('guarded-story');
    assert.deepEqual(res, { ok: false, code: 'guard-denied' });
    assert.equal(router.current, 'home');
    assert.equal(transport.calls.length, 0);
  });

  it('global beforeEach guard denies with guard-denied before transport invoke', async () => {
    const transport = fakeTransport();
    const router = galleryRouter(transport);
    router.beforeEach(() => false);
    const res = await router.push('story');
    assert.deepEqual(res, { ok: false, code: 'guard-denied' });
    assert.equal(transport.calls.length, 0);
  });

  it('go(delta>0) is denied no-forward; back() clamps at home, never blank', async () => {
    const transport = fakeTransport();
    const router = galleryRouter(transport);
    await router.push('story');
    assert.deepEqual(await router.go(1), { ok: false, code: 'no-forward' });
    assert.equal(router.current, 'story');
    assert.deepEqual(await router.back(), { ok: true });
    assert.equal(router.current, 'home');
    assert.deepEqual(await router.back(), { ok: true });
    assert.equal(router.current, 'home');
  });
});

describe('gallery migration target (binding does not exist yet — RED)', () => {
  it('migrated galleryNav exposes a facade-backed binding (not the legacy bridge)', () => {
    // Target shape: examples/m3-gallery/src/galleryNav.js (or its
    // successor) builds its navigator from createRoutes/createRouter with
    // injected transport. Today it hand-rolls `requestShellAdvance` —
    // this binding is absent, so this fails until migration.
    const galleryNav = require('../src/galleryNav.js');
    assert.equal(
      typeof galleryNav.createGalleryRouter,
      'function',
      'galleryNav must export createGalleryRouter({ transport }) built on the facade'
    );
  });

  it('legacy requestShellAdvance is retired (removed, not wrapped)', () => {
    const galleryNav = require('../src/galleryNav.js');
    assert.equal(
      galleryNav.requestShellAdvance,
      undefined,
      'requestShellAdvance must be absent once migrated (AC-NAV-043)'
    );
  });
});
