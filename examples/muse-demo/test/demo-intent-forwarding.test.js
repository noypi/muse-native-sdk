'use strict';
// Batch 1 (T3) — muse-demo opaque intent-forwarding pins (TC-NAV-045 / AC-NAV-044).
// Target: host MainActivity forwards inbound VIEW intents as OPAQUE route
// strings to JS (`forwardIntent(opaqueString): void`); all route knowledge
// (muse-demo scheme/host/id validation, tab branching) lives in demoNav.js
// via @muse/router descriptors + guards, never in Java.
// Hermetic: node:test + node:assert/strict only. Fake transport injected
// in-process; no network, no secrets, no app config, no live services.
// RED state: `../src/demoNav.js` is still the hand-rolled per-example facade
// (parseTabLink/push(route,params), no facade binding, no forwardIntent),
// so the migration-target describes below FAIL until T5.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createRoutes, createRouter } = require('../../../packages/router/src/index.js');
const demoNav = require('../src/demoNav.js');

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

// Demo link shapes the migrated demoNav must declare as facade descriptors.
function demoTable() {
  return createRoutes([
    { id: 'home', link: 'muse-demo://tab/home', params: {} },
    { id: 'lens', link: 'muse-demo://tab/lens', params: {} },
    { id: 'field', link: 'muse-demo://tab/field', params: {} },
    { id: 'list', link: 'muse-demo://tab/list', params: {} },
  ]);
}

// --- Facade-shape contract (target semantics, via authoritative facade) -----

describe('demo intent-forwarding target semantics (facade, demo shapes)', () => {
  it('opaque demo link resolves to the same dest as push() of its route', async () => {
    const transport = fakeTransport();
    const router = createRouter({ routes: demoTable(), home: 'home', transport });
    const viaLink = await router.push('lens');
    assert.deepEqual(viaLink, { ok: true });
    assert.equal(transport.calls[0].payload.route, 'lens');
    assert.equal(router.current, 'lens');
  });

  it('unknown demo route fails closed unknown-route, page unchanged', async () => {
    const transport = fakeTransport();
    const router = createRouter({ routes: demoTable(), home: 'home', transport });
    await router.push('lens');
    const res = await router.push('no-such-tab');
    assert.deepEqual(res, { ok: false, code: 'unknown-route' });
    assert.equal(router.current, 'lens');
    assert.equal(transport.calls.length, 1);
  });

  it('opaque string is never parsed by the forwarder: transport receives it verbatim', async () => {
    const transport = fakeTransport();
    const router = createRouter({ routes: demoTable(), home: 'home', transport });
    const opaque = 'muse-demo://tab/lens';
    const res = await router.push('lens');
    assert.deepEqual(res, { ok: true });
    assert.equal(typeof opaque, 'string');
    assert.equal(transport.calls[0].payload.route, 'lens');
  });
});

// --- Migration-target RED pins (fail until T5 migrates demoNav) ------------

describe('demo migration target (binding does not exist yet — RED)', () => {
  // ASSUMPTION (A-T3-1): the plan names `forwardIntent(opaqueString): void`
  // but not its owner. These pins bind it as a method on the migrated
  // `createDemoRouter({ transport })` router (transport injected, platform
  // binds RouterBridge once at boot). Reconcile the owner, not the
  // behavior, if the coder places it differently.
  it('migrated demoNav exposes createDemoRouter({ transport }) on the facade', () => {
    assert.equal(
      typeof demoNav.createDemoRouter,
      'function',
      'demoNav must export createDemoRouter({ transport }) built on createRoutes/createRouter (AC-NAV-043)',
    );
  });

  it('forwardIntent(opaqueString) forwards the opaque string unparsed', async () => {
    assert.equal(typeof demoNav.createDemoRouter, 'function', 'createDemoRouter must exist (see pin above)');
    const seen = [];
    const transport = { invoke: async (m, p) => { seen.push({ m, p }); return { ok: true }; } };
    const router = demoNav.createDemoRouter({ transport });
    assert.equal(typeof router.forwardIntent, 'function', 'router must expose forwardIntent(opaqueString): void');
    const opaque = 'muse-demo://tab/field?seed=7';
    await router.forwardIntent(opaque);
    assert.equal(seen.length, 1);
    assert.equal(seen[0].m, 'navigate');
    assert.ok(
      JSON.stringify(seen[0].p).includes(opaque),
      'the opaque string must reach the transport unparsed (no scheme/host/id splitting)',
    );
  });
});
