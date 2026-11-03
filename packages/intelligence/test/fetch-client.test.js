'use strict';
// Batch 2 RED pins — T5 (fetch route client over stub descriptors) + T6 (SSE
// streaming of reasoning_content then content per the R1 assumption).
// Hermetic: stub-backed `fetchImpl` only; no live network, no keys, no device.
// Uses node:test + node:assert/strict. Spec refs: FR-MUSE-002, AC-MUSE-005,
// AC-MUSE-006, AC-MUSE-007, AC-MUSE-034/035/036; plan Batch 2 T5/T6.
//
// R1 (recorded run ruling, cited as explicit ASSUMPTION, never spec-settled):
// SSE default `streamReason(fetchImpl, { turnId, preset? })` async-iterable on
// the `assertMuseRoutes` seam. T6 tests below pin that assumed shape; if the
// coder settles a different signature, the contract reconciles per GR-08 T1.9.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const routesLib = require('../lib/routes.js');
const { assertMuseRoutes, streamReason } = routesLib;

// --- Hermetic stub fetchImpl -------------------------------------------
// Serves route descriptors + the presets doc. Records every call so tests can
// prove rejection happened BEFORE any upstream call. Never touches network.
function stubFetch({ routes, presets } = {}) {
  const calls = [];
  const db = {
    routes: routes ?? {
      transcribe: { route: 'transcribe', model: 'muse-voice-transcribe-1.0', stream: 'sse' },
      reason: { route: 'reason', model: 'muse-spark-1.3', stream: 'sse' },
      'generate-image': { route: 'generate-image', model: 'muse-image', stream: 'progressive' },
    },
    presets: presets ?? { 'reason-default': { route: 'reason', params: { reasoning_effort: 'medium' } } },
  };
  const fetchImpl = async (name, opts) => {
    calls.push({ name, opts });
    if (name === 'routes') return { ok: true, json: async () => db.routes };
    if (name === 'presets') return { ok: true, json: async () => db.presets };
    if (String(name).startsWith('upstream:')) {
      throw new Error('upstream must never be fetched by the route client');
    }
    throw new Error(`unexpected fetch: ${name}`);
  };
  return { fetchImpl, calls, db };
}

describe('T5 — required routes present over stub descriptors (AC-MUSE-005/006)', () => {
  it('resolves { ok: true, missing: [] } when transcribe/reason/generate-image exist', async () => {
    const { fetchImpl } = stubFetch();
    assert.deepEqual(await assertMuseRoutes(fetchImpl), { ok: true, missing: [] });
  });

  it('reports a missing route by name with ok:false', async () => {
    const { fetchImpl } = stubFetch({
      routes: { transcribe: { route: 'transcribe' }, reason: { route: 'reason' } },
    });
    const result = await assertMuseRoutes(fetchImpl);
    assert.equal(result.ok, false);
    assert.ok(result.missing.includes('generate-image'));
  });

  it('never fetches upstream:* during the route check', async () => {
    const { fetchImpl, calls } = stubFetch();
    await assertMuseRoutes(fetchImpl);
    assert.ok(calls.every((c) => !String(c.name).startsWith('upstream:')));
  });
});

describe('T5 — fail-closed on unknown preset / raw model / endpoint / param (AC-MUSE-035/036)', () => {
  it("unknown preset rejects with { code: 'unknown-preset' } before any upstream call", async () => {
    const { fetchImpl, calls } = stubFetch();
    await assert.rejects(
      assertMuseRoutes(fetchImpl, { preset: 'no-such-preset' }),
      (err) => err && err.code === 'unknown-preset',
    );
    assert.ok(calls.every((c) => !String(c.name).startsWith('upstream:')));
  });

  it("raw model id in props rejects with { code: 'raw-model' } pre-fetch", async () => {
    const { fetchImpl, calls } = stubFetch();
    await assert.rejects(
      assertMuseRoutes(fetchImpl, { props: { model: 'muse-spark-1.3' } }),
      (err) => err && err.code === 'raw-model',
    );
    assert.equal(calls.length, 0);
  });

  it("raw endpoint in props rejects with { code: 'raw-endpoint' } pre-fetch", async () => {
    const { fetchImpl, calls } = stubFetch();
    await assert.rejects(
      assertMuseRoutes(fetchImpl, { props: { endpoint: 'https://example.test/v1/x' } }),
      (err) => err && err.code === 'raw-endpoint',
    );
    assert.equal(calls.length, 0);
  });

  it("raw param value in props rejects with { code: 'raw-param' } pre-fetch", async () => {
    const { fetchImpl, calls } = stubFetch();
    await assert.rejects(
      assertMuseRoutes(fetchImpl, { props: { apiKey: 'sk-test-not-real' } }),
      (err) => err && err.code === 'raw-param',
    );
    assert.equal(calls.length, 0);
  });
});

describe('T5 — turnId stamped on the fetch path (FR-MUSE-002 turn correlation)', () => {
  // ASSUMPTION (T5-turn): the plan Detail requires "turnId stamped" but the
  // Interface line names no turnId shape. Most defensible behavior: the
  // client-generated turnId rides `options.turnId`, is forwarded to every
  // `fetchImpl(name, { turnId })` descriptor fetch, and is echoed on the
  // result — the proxy passes it through opaquely and never invents one.

  it('forwards options.turnId to every fetchImpl descriptor fetch', async () => {
    const { fetchImpl, calls } = stubFetch();
    await assertMuseRoutes(fetchImpl, { turnId: 'turn-7-abc' });
    assert.ok(calls.length > 0, 'expected descriptor fetches');
    for (const c of calls) {
      assert.equal(c.opts && c.opts.turnId, 'turn-7-abc');
    }
  });

  it('echoes the calling turnId on the result', async () => {
    const { fetchImpl } = stubFetch();
    const result = await assertMuseRoutes(fetchImpl, { turnId: 'turn-7-abc' });
    assert.equal(result.turnId, 'turn-7-abc');
  });
});

describe('T6 — SSE streaming shape per R1 assumption (AC-MUSE-006)', () => {
  // All cases below pin the R1 ASSUMPTION (see file header): `streamReason`
  // async-iterable yielding reasoning_content deltas then content, with the
  // calling turnId stamped and stale-turnId deltas dropped. RED until the
  // coder implements the assumed seam.

  function stubStreamFetch() {
    const calls = [];
    const fetchImpl = async (name, opts) => {
      calls.push({ name, opts });
      if (name === 'routes') {
        return {
          ok: true,
          json: async () => ({
            reason: { route: 'reason', model: 'muse-spark-1.3', stream: 'sse' },
          }),
        };
      }
      if (name === 'presets') {
        return { ok: true, json: async () => ({ 'reason-default': { route: 'reason', params: {} } }) };
      }
      if (name === 'reason-stream') {
        return {
          ok: true,
          chunks: [
            'data: {"reasoning_content":"recorded-thinking-part-1"}\n\n',
            'data: {"reasoning_content":"recorded-thinking-part-2"}\n\n',
            'data: {"content":"recorded-insight"}\n\n',
          ],
        };
      }
      if (String(name).startsWith('upstream:')) throw new Error('upstream must never be fetched');
      throw new Error(`unexpected fetch: ${name}`);
    };
    return { fetchImpl, calls };
  }

  it('streamReason is an exported function (R1 seam exists)', () => {
    assert.equal(typeof streamReason, 'function');
  });

  it('yields reasoning_content deltas before content, in SSE order', async () => {
    const { fetchImpl } = stubStreamFetch();
    const seen = [];
    for await (const evt of streamReason(fetchImpl, { turnId: 'turn-9-xyz' })) {
      seen.push(evt);
    }
    const types = seen.map((e) => e.type);
    assert.deepEqual(types, ['reasoning_content', 'reasoning_content', 'content']);
    assert.ok(seen[0].delta.includes('recorded-thinking-part-1'));
    assert.ok(seen[seen.length - 1].delta.includes('recorded-insight'));
  });

  it('stamps the calling turnId on every yielded event', async () => {
    const { fetchImpl } = stubStreamFetch();
    for await (const evt of streamReason(fetchImpl, { turnId: 'turn-9-xyz' })) {
      assert.equal(evt.turnId, 'turn-9-xyz');
    }
  });

  it('drops stale-turnId deltas, keeping the latest committed turn only', async () => {
    const { fetchImpl } = stubStreamFetch();
    const seen = [];
    for await (const evt of streamReason(fetchImpl, { turnId: 'turn-9-xyz', committedTurnId: 'turn-9-xyz' })) {
      seen.push(evt);
    }
    assert.ok(seen.length > 0, 'latest committed turn must yield events');
    assert.ok(seen.every((e) => e.turnId === 'turn-9-xyz'));
    const stale = [];
    for await (const evt of streamReason(fetchImpl, { turnId: 'turn-8-old', committedTurnId: 'turn-9-xyz' })) {
      stale.push(evt);
    }
    assert.deepEqual(stale, []);
  });

  it('rejects an unknown route before opening any stream', async () => {
    const { fetchImpl, calls } = stubStreamFetch();
    await assert.rejects(
      (async () => {
        for await (const _ of streamReason(fetchImpl, { turnId: 't', route: 'speak' })) { /* drain */ }
      })(),
      (err) => err && (err.code === 'unknown-route' || err.code === 'unknown-preset'),
    );
    assert.ok(calls.every((c) => !String(c.name).startsWith('upstream:')));
  });
});

describe('Batch 2 GR-12 — no fake runtime reachable on the fetch path (FR-TESTING-005/AC-TESTING-036, BUG-RUNTIME-008)', () => {
  // Negated source scan over the fetch-path production files: the JS fetch
  // seam plus the C++ runtime floor. RED = a Fake*/Mock*/Stub* symbol is
  // reachable in prod (fail the pin); GREEN = real-only. Depends on
  // BUG-RUNTIME-008 (fixed, merged #261 — FakeJsRuntime removed, RealJsRuntime
  // fail-closed). Uses node:fs only; no build, no device.
  const fs = require('node:fs');
  const path = require('node:path');
  const ROOT = path.resolve(__dirname, '..', '..', '..');
  const PROD_FILES = [
    'packages/intelligence/lib/routes.js',
    'packages/intelligence/lib/contract.js',
    'packages/native/muse-mcp.js',
    'proxy/lib/stub.js',
    'proxy/lib/dispatch.js',
    'engine/runtime/src/js_runtime.cpp',
    'engine/runtime/src/js_engine.cpp',
  ];

  function scan(re) {
    const hits = [];
    for (const rel of PROD_FILES) {
      const full = path.join(ROOT, rel);
      if (!fs.existsSync(full)) continue;
      const text = fs.readFileSync(full, 'utf8');
      for (const m of text.matchAll(re)) {
        hits.push(`${rel}: ${m[0].slice(0, 80)}`);
      }
    }
    return hits;
  }

  it('no FakeJsRuntime / FakeEngineHooks / createFake reachable in fetch-path prod files', () => {
    const hits = scan(/FakeJsRuntime|FakeEngineHooks|createFake/g);
    assert.deepEqual(hits, [], `fake runtime reachable:\n${hits.join('\n')}`);
  });

  it('no Fake*/Mock*/Stub* class or factory symbols reachable in fetch-path prod files', () => {
    const hits = scan(/\b(?:class\s+(?:Fake|Mock|Stub)\w*|create(?:Fake|Mock|Stub)\w*)\b/g);
    assert.deepEqual(hits, [], `fake/mock/stub symbol reachable:\n${hits.join('\n')}`);
  });
});
