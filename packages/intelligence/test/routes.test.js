'use strict';
// T2 — Proxy route availability check for transcribe/reason/generate-image (Batch 1).
// Plan Interface (verbatim):
// `assertMuseRoutes(fetchImpl): Promise<{ ok: boolean, missing: string[] }>`
// — checks `transcribe`, `reason`, `generate-image` route descriptors +
// `muse-native.yaml:muse.presets` key-union validation (unknown preset key →
// `{ code: 'unknown-preset' }` before any upstream call); rejects raw model
// IDs/params in props.
// Spec refs: FR-MUSE-002 (required dependency: AC-MUSE-005/006/034/035/036).
// Consumes the FR-MUSE-002 contract only — hermetic stub fetchImpl, no network,
// no keys. fetchImpl is the only I/O; call counts prove pre-call rejection.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { assertMuseRoutes } = require('../lib/routes.js');

// --- Hermetic stub fetchImpl -------------------------------------------
// Serves route descriptors + the presets doc. Records every call so tests can
// prove an unknown route/preset was rejected BEFORE any upstream call.
function stubFetch({ routes, presets, routeDefaults } = {}) {
  const calls = [];
  const db = {
    routes: routes ?? {
      transcribe: { route: 'transcribe', model: 'muse-voice-transcribe-1.0', stream: 'sse' },
      reason: { route: 'reason', model: 'muse-spark-1.3', stream: 'sse' },
      'generate-image': { route: 'generate-image', model: 'muse-image', stream: 'progressive' },
    },
    presets: presets ?? { 'reason-default': { route: 'reason', params: {} } },
    routeDefaults: routeDefaults ?? { transcribe: {}, reason: {}, 'generate-image': {} },
  };
  const fetchImpl = async (url, opts) => {
    calls.push({ url, opts });
    if (url === 'routes') return { ok: true, json: async () => db.routes };
    if (url === 'presets') return { ok: true, json: async () => db.presets };
    if (url === 'defaults') return { ok: true, json: async () => db.routeDefaults };
    if (String(url).startsWith('upstream:')) {
      throw new Error('upstream must never be called by the availability check');
    }
    throw new Error(`unexpected fetch: ${url}`);
  };
  return { fetchImpl, calls, db };
}

describe('T2 — all three routes present (AC-MUSE-005/006)', () => {
  it('resolves { ok: true, missing: [] } when transcribe/reason/generate-image exist', async () => {
    const { fetchImpl } = stubFetch();
    assert.deepEqual(await assertMuseRoutes(fetchImpl), { ok: true, missing: [] });
  });

  it('never calls an upstream during the availability check', async () => {
    const { fetchImpl, calls } = stubFetch();
    await assertMuseRoutes(fetchImpl);
    assert.ok(calls.every((c) => !String(c.url).startsWith('upstream:')));
  });
});

describe('T2 — unknown route rejected pre-call (AC-MUSE-006)', () => {
  it('reports the missing route by name and ok:false', async () => {
    const { fetchImpl } = stubFetch({
      routes: {
        transcribe: { route: 'transcribe' },
        reason: { route: 'reason' },
        // 'generate-image' absent
      },
    });
    const result = await assertMuseRoutes(fetchImpl);
    assert.equal(result.ok, false);
    assert.ok(result.missing.includes('generate-image'));
  });

  it('rejects before any upstream call when a route is missing', async () => {
    const { fetchImpl, calls } = stubFetch({ routes: { transcribe: { route: 'transcribe' } } });
    await assertMuseRoutes(fetchImpl);
    assert.ok(calls.every((c) => !String(c.url).startsWith('upstream:')));
  });
});

describe('T2 — preset key validation (AC-MUSE-034/035/036)', () => {
  it("unknown preset key rejects with { code: 'unknown-preset' } before any upstream call", async () => {
    // ASSUMPTION (see unit-tests/T2.md): preset selection is passed as an
    // options second arg `assertMuseRoutes(fetchImpl, { preset })`; the
    // Interface line names the validation behavior, not the parameter shape.
    const { fetchImpl, calls } = stubFetch();
    await assert.rejects(
      assertMuseRoutes(fetchImpl, { preset: 'no-such-preset' }),
      (err) => err && err.code === 'unknown-preset'
    );
    assert.ok(calls.every((c) => !String(c.url).startsWith('upstream:')));
  });

  it('absent preset resolves to the route default (ok:true)', async () => {
    const { fetchImpl } = stubFetch();
    const result = await assertMuseRoutes(fetchImpl);
    assert.deepEqual(result, { ok: true, missing: [] });
  });

  it('known preset key passes validation', async () => {
    const { fetchImpl } = stubFetch();
    const result = await assertMuseRoutes(fetchImpl, { preset: 'reason-default' });
    assert.equal(result.ok, true);
  });

  it('raw model IDs in props are rejected pre-call (AC-MUSE-035)', async () => {
    // ASSUMPTION: props under test ride the same options arg `{ props }`; a
    // raw model id / endpoint / param value rejects with a named error.
    const { fetchImpl, calls } = stubFetch();
    await assert.rejects(
      assertMuseRoutes(fetchImpl, { props: { model: 'muse-spark-1.3' } }),
      (err) => err && typeof err.code === 'string'
    );
    assert.ok(calls.every((c) => !String(c.url).startsWith('upstream:')));
  });

  it('raw endpoint in props is rejected pre-call', async () => {
    const { fetchImpl } = stubFetch();
    await assert.rejects(
      assertMuseRoutes(fetchImpl, { props: { endpoint: 'https://example.test/v1/x' } }),
      (err) => err && typeof err.code === 'string'
    );
  });
});
