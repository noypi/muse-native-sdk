'use strict';
// NAV-006 regression — JS `replace`/`go`/`back` bypass the C++ graph, forking router state.
// Support: workflow.local/runs/nav-006/support.md (BUG-NAV-006).
// Specs: FR-NAV-014, AC-NAV-040 (replace/back/go semantics + denials, never blank),
//   AC-NAV-042 (every facade method funnels to RouterBridge.invoke; C++ graph is
//   single source of truth). TC-NAV-040/042.
// Hermetic: node:test + node:assert/strict only. Fake transports injected
// in-process (one records calls, one simulates the C++ graph depth); no network,
// no secrets, no app config, no live services.
// GREEN state: production `../src/index.js` funnels replace via method
// 'replace' and go()/back() via transport 'go' with the integer guard, and the
// C++ simulator fake below carries matching Replace/Go arms — the funnel/
// parity/guard cases below pass.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createRoutes, createRouter } = require('../src/index.js');

function fixtures(transport) {
  const routes = createRoutes([
    { id: 'home', link: 'muse-demo://tab/home', params: {} },
    { id: 'lens', link: 'muse-demo://tab/lens', params: {} },
    { id: 'field', link: 'muse-demo://tab/field', params: {} },
  ]);
  const router = createRouter({ routes, home: 'home', transport });
  return { routes, router };
}

function recordingTransport() {
  const calls = [];
  return {
    calls,
    invoke: async (method, payload) => {
      calls.push({ method, payload });
      return { ok: true };
    },
  };
}

// Simulates the C++ graph with the landed production vocabulary
// (navigate|push -> Push, pop|back -> Pop, replace -> Replace swaps top,
// go -> GoTo pops |delta| clamped at home, fail-closed on bad deltas).
function cppSimulatingTransport() {
  const calls = [];
  const cppStack = ['home'];
  return {
    calls,
    cppStack,
    invoke: async (method, payload) => {
      calls.push({ method, payload });
      if (method === 'navigate' || method === 'push') {
        cppStack.push(payload.route);
        return { ok: true };
      }
      if (method === 'pop' || method === 'back') {
        if (cppStack.length > 1) cppStack.pop();
        return { ok: true };
      }
      if (method === 'replace') {
        // Replace swaps top (mirrors production navigate(isReplace) swap).
        cppStack[cppStack.length - 1] = payload.route;
        return { ok: true };
      }
      if (method === 'go') {
        // GoTo pops |delta| clamped at home; fail-closed on bad deltas
        // (mirrors production go(): bad-params for non-integer/0/NaN,
        // no-forward for positive delta — stack left untouched).
        const n = Number(payload && payload.delta);
        if (!Number.isInteger(n) || n === 0 || Number.isNaN(n)) {
          return { ok: false, code: 'bad-params' };
        }
        if (n > 0) {
          return { ok: false, code: 'no-forward' };
        }
        const pops = Math.min(cppStack.length - 1, Math.abs(n));
        for (let i = 0; i < pops; i++) cppStack.pop();
        return { ok: true };
      }
      return { ok: true };
    },
  };
}

describe('NAV-006 AC-NAV-042: replace swaps via its own funnel method (no push)', () => {
  it("replace invokes transport with method 'replace', not 'navigate'", async () => {
    const transport = recordingTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    transport.calls.length = 0;
    const res = await router.replace('home');
    assert.deepEqual(res, { ok: true });
    assert.equal(transport.calls.length, 1);
    assert.equal(transport.calls[0].method, 'replace');
    assert.equal(transport.calls[0].payload.route, 'home');
  });

  it('replace after push keeps JS depth == C++ depth (swap, not push)', async () => {
    const transport = cppSimulatingTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    const callsBefore = transport.calls.length;
    void callsBefore;
    await router.replace('home');
    // JS depth after swap: home,lens -> home,home = 2; a push would make 3.
    // C++ depth must agree once the funnel carries replace-as-swap.
    assert.equal(transport.cppStack.length, 2);
    assert.equal(router.current, 'home');
    assert.equal(transport.cppStack[transport.cppStack.length - 1], router.current);
  });
});

describe('NAV-006 AC-NAV-042: go/back route through transport, fail-closed', () => {
  it('back() emits a transport call (pop/back)', async () => {
    const transport = recordingTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    transport.calls.length = 0;
    const res = await router.back();
    assert.deepEqual(res, { ok: true });
    assert.equal(transport.calls.length, 1);
    assert.ok(['pop', 'back', 'go'].includes(transport.calls[0].method));
  });

  it('go(-1) emits a transport call', async () => {
    const transport = recordingTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    await router.push('field');
    transport.calls.length = 0;
    const res = await router.go(-1);
    assert.deepEqual(res, { ok: true });
    assert.equal(transport.calls.length, 1);
  });

  it('transport denial on back/go fails closed: page unchanged', async () => {
    const calls = [];
    const transport = {
      calls,
      invoke: async (method, payload) => {
        calls.push({ method, payload });
        if (method === 'pop' || method === 'back' || method === 'go') {
          return { ok: false, code: 'guard-denied' };
        }
        return { ok: true };
      },
    };
    const { router } = fixtures(transport);
    await router.push('lens');
    const res = await router.back();
    assert.deepEqual(res, { ok: false, code: 'guard-denied' });
    assert.equal(router.current, 'lens');
  });

  it('push/replace/back sequence keeps JS depth in agreement with C++ depth', async () => {
    const transport = cppSimulatingTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    await router.replace('field');
    await router.back();
    assert.equal(router.current, transport.cppStack[transport.cppStack.length - 1]);
    assert.equal(router.current, 'home');
  });
});

describe('NAV-006 AC-NAV-040: integer guard on go(), never blank', () => {
  it('go(-1.5) denies bad-params, page unchanged and non-blank', async () => {
    const transport = recordingTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    const res = await router.go(-1.5);
    assert.deepEqual(res, { ok: false, code: 'bad-params' });
    assert.equal(router.current, 'lens');
    assert.ok(router.current, 'current page is never blank');
  });

  it('go(1.5) denies bad-params (integer guard, not forward route)', async () => {
    const transport = recordingTransport();
    const { router } = fixtures(transport);
    await router.push('lens');
    const res = await router.go(1.5);
    assert.deepEqual(res, { ok: false, code: 'bad-params' });
    assert.equal(router.current, 'lens');
  });
});
