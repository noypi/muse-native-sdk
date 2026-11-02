'use strict';
// Batch 2 (T2) — @muse/router transport injection (AC-NAV-041, TC-NAV-041).
// Specs: FR-NAV-014, AC-NAV-041. Plan interface (reproduced verbatim):
//   `Transport = { invoke(method: "navigate", payload: {route: string,
//   params?: object}): Promise<NavResult> }; createRouter({transport})` —
//   platform passes `RouterBridge.invoke` bound once at boot; no
//   `globalThis.__muse*` reads in `packages/router/src`.
// Hermetic: node:test + node:assert/strict (+ node:fs/path for the
// no-globals source assertion) only. Fake transports injected in-process;
// no network, no secrets, no app config, no live services.
// RED state: `../src/transport.js` does not exist yet (coder owns it), so
// this file fails at import until T2 is implemented.
// ASSUMPTION (A-T2-1): `../src/transport.js` exports `createBridgeTransport`
//   — the plan names the file and the Transport shape but not the export
//   name. The coder may reconcile the name; expected behavior (bind-once,
//   funnel shape) is what the tests pin.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { createRoutes, createRouter } = require('../src/index.js');
// eslint-disable-next-line import/no-unresolved
const { createBridgeTransport } = require('../src/transport.js'); // RED: missing (A-T2-1)

// --- Fakes (in-test only) -------------------------------------------------

function recordingBridge() {
  const calls = [];
  const bridge = {
    calls,
    invoke: async (method, payload) => {
      calls.push({ method, payload });
      return { ok: true };
    },
  };
  return bridge;
}

function fixtures(transport) {
  const routes = createRoutes([
    { id: 'home', link: 'muse-demo://tab/home', params: {} },
    { id: 'lens', link: 'muse-demo://tab/lens', params: { id: 'string' } },
  ]);
  const router = createRouter({ routes, home: 'home', transport });
  return { routes, router };
}

function srcFiles() {
  const dir = path.join(__dirname, '..', 'src');
  return fs
    .readdirSync(dir)
    .filter((f) => f.endsWith('.js'))
    .map((f) => ({ file: f, text: fs.readFileSync(path.join(dir, f), 'utf8') }));
}

// --- AC-NAV-041: transport injection ----------------------------------------

describe('bridge adapter binds once (AC-NAV-041)', () => {
  it('createBridgeTransport exposes a Transport with an invoke function', () => {
    const bridge = recordingBridge();
    const transport = createBridgeTransport(bridge);
    assert.equal(typeof transport.invoke, 'function');
  });

  it('platform binds RouterBridge.invoke once: one factory call serves many navigations', async () => {
    const bridge = recordingBridge();
    const transport = createBridgeTransport(bridge); // bound once at boot
    const { router } = fixtures(transport);
    await router.push('lens');
    await router.replace('home');
    assert.equal(bridge.calls.length, 2);
  });

  it('push funnels to bridge.invoke("navigate", {route, params})', async () => {
    const bridge = recordingBridge();
    const { router } = fixtures(createBridgeTransport(bridge));
    const res = await router.push({ id: 'lens', params: { id: 'p1' } });
    assert.deepEqual(res, { ok: true });
    assert.equal(bridge.calls.length, 1);
    assert.equal(bridge.calls[0].method, 'navigate');
    assert.deepEqual(bridge.calls[0].payload, { route: 'lens', params: { id: 'p1' } });
  });

  it('replace funnels through the same transport with the new route', async () => {
    const bridge = recordingBridge();
    const { router } = fixtures(createBridgeTransport(bridge));
    await router.push('lens');
    const res = await router.replace('home');
    assert.deepEqual(res, { ok: true });
    const last = bridge.calls[bridge.calls.length - 1];
    assert.equal(last.method, 'navigate');
    assert.equal(last.payload.route, 'home');
  });

  it('payload carries exactly route+params (no extra fields, no secrets)', async () => {
    const bridge = recordingBridge();
    const { router } = fixtures(createBridgeTransport(bridge));
    await router.push({ id: 'lens', params: { id: 'p1' } });
    assert.deepEqual(Object.keys(bridge.calls[0].payload).sort(), ['params', 'route']);
  });

  it('tests inject a fake in the same slot: no adapter required', async () => {
    const calls = [];
    const fake = {
      invoke: async (method, payload) => {
        calls.push({ method, payload });
        return { ok: true };
      },
    };
    const { router } = fixtures(fake);
    const res = await router.push('lens');
    assert.deepEqual(res, { ok: true });
    assert.equal(calls.length, 1);
    assert.equal(calls[0].method, 'navigate');
  });

  it('two routers with different fakes do not cross-talk', async () => {
    const a = recordingBridge();
    const b = recordingBridge();
    const ra = fixtures(createBridgeTransport(a)).router;
    const rb = fixtures(createBridgeTransport(b)).router;
    await ra.push('lens');
    assert.equal(a.calls.length, 1);
    assert.equal(b.calls.length, 0);
    await rb.push('lens');
    assert.equal(b.calls.length, 1);
  });

  it('denied navigation never reaches the bridge (guards run before invoke)', async () => {
    const bridge = recordingBridge();
    const { router } = fixtures(createBridgeTransport(bridge));
    router.beforeEach(() => false);
    const res = await router.push('lens');
    assert.deepEqual(res, { ok: false, code: 'guard-denied' });
    assert.equal(bridge.calls.length, 0);
  });

  it('unknown route never reaches the bridge', async () => {
    const bridge = recordingBridge();
    const { router } = fixtures(createBridgeTransport(bridge));
    const res = await router.push('nope');
    assert.deepEqual(res, { ok: false, code: 'unknown-route' });
    assert.equal(bridge.calls.length, 0);
  });
});

// --- AC-NAV-041: no globals in app code --------------------------------------

describe('no globalThis.__muse* in packages/router/src (AC-NAV-041)', () => {
  it('no file under src references globalThis.__muse', () => {
    for (const { file, text } of srcFiles()) {
      assert.ok(
        !text.includes('globalThis.__muse'),
        `${file} must not read globalThis.__muse* (inject transport instead)`,
      );
    }
  });

  it('legacy __museRouterNavigate/__museTestBroadcasts survive only behind adapters, never in src', () => {
    for (const { file, text } of srcFiles()) {
      assert.ok(
        !text.includes('__museRouterNavigate') && !text.includes('__museTestBroadcasts'),
        `${file} must not reference legacy globals`,
      );
    }
  });
});
