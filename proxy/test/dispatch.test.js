'use strict';
// T2 — proxy dispatch: allow-list enforcement, error taxonomy, SSE passthrough,
// turnId/targetId handling. Hermetic: node:test + node:assert/strict only.
// The route table comes from the implementation's internal allow-list (T1
// loadConfig over proxy/proxy.config.json); tests never touch the network,
// never read keys, and fake every collaborator (injectKey, upstream) in-process.
// A recording upstream fake asserts the "rejected before any upstream call" rule.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('../lib/dispatch.js');
}

let savedEnv;
beforeEach(() => {
  savedEnv = { ...process.env };
  delete process.env.MUSE_API_KEY;
  delete process.env.XAI_API_KEY;
  delete process.env.OPENAI_API_KEY;
});
afterEach(() => {
  process.env = savedEnv;
});

// Recording upstream fake: records every call, replays a scripted response.
function recordingUpstream(script) {
  const calls = [];
  const fn = async (route, body) => {
    calls.push({ route, body });
    const step = script[calls.length - 1] !== undefined ? script[calls.length - 1] : script[script.length - 1];
    if (step instanceof Error) throw step;
    return step;
  };
  fn.calls = calls;
  return fn;
}

function okUpstream(body) {
  return recordingUpstream([{ status: 200, body: body !== undefined ? body : { ok: true } }]);
}

function ctxWith(upstream, extra) {
  return { injectKey: () => 'test-key-not-real', upstream, ...(extra || {}) };
}

async function assertProxyError(promise, expectedCode, expectedRoute) {
  let err;
  try {
    await promise;
  } catch (e) {
    err = e;
  }
  assert.ok(err, `expected ProxyError(${expectedCode})`);
  assert.equal(err.code, expectedCode);
  if (expectedRoute !== undefined) assert.equal(err.route, expectedRoute);
  assert.equal(typeof err.retryable, 'boolean');
  return err;
}

const DOCUMENTED_CODES = new Set([
  'auth',
  'upstream',
  'timeout',
  'schema',
  'unknown-route',
  'unlisted-param',
  'unknown-preset',
  'non-muse-model',
]);

describe('dispatch happy path', () => {
  it('forwards an allow-listed segment call and returns { status, body, turnId }', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({ mask: 'polygon' });
    const res = await dispatch(
      { route: 'segment', params: { mask_encoding: 'polygon' }, turnId: 't-1' },
      ctxWith(upstream),
    );
    assert.equal(res.status, 200);
    assert.deepEqual(res.body, { mask: 'polygon' });
    assert.equal(res.turnId, 't-1');
    assert.equal(upstream.calls.length, 1);
    assert.equal(upstream.calls[0].route, 'segment');
  });

  it('passes turnId through opaquely without mutation', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const res = await dispatch(
      { route: 'reason', params: { reasoning_effort: 'high' }, turnId: 'opaque-xyz-123' },
      ctxWith(upstream),
    );
    assert.equal(res.turnId, 'opaque-xyz-123');
  });

  it('never invents a turnId when the request carries none', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const res = await dispatch({ route: 'segment', params: { mask_encoding: 'polygon' } }, ctxWith(upstream));
    assert.ok(!('turnId' in res) || res.turnId === undefined, 'turnId must not be invented');
  });

  it('drops a stale turnId delta per the caller-supplied latest-turn check (ASSUMPTION: ctx.latestTurnId)', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const res = await dispatch(
      { route: 'reason', params: { reasoning_effort: 'high' }, turnId: 't-stale' },
      ctxWith(upstream, { latestTurnId: 't-fresh' }),
    );
    assert.ok(!('turnId' in res) || res.turnId === undefined || res.turnId === 't-fresh');
  });
});

describe('dispatch validation rejects before any upstream call', () => {
  it('rejects an unknown route with unknown-route and zero upstream calls', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const err = await assertProxyError(
      dispatch({ route: 'speak', params: {} }, ctxWith(upstream)),
      'unknown-route',
      'speak',
    );
    assert.equal(err.retryable, false);
    assert.equal(upstream.calls.length, 0);
  });

  it('rejects an unlisted param with unlisted-param and zero upstream calls', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const err = await assertProxyError(
      dispatch({ route: 'segment', params: { mask_encoding: 'polygon', evil: 1 } }, ctxWith(upstream)),
      'unlisted-param',
      'segment',
    );
    assert.equal(err.retryable, false);
    assert.equal(upstream.calls.length, 0);
  });

  it('rejects style_preset as unlisted-param (Q-MUSE-001 assumed, never allow-listed)', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await assertProxyError(
      dispatch({ route: 'generate-image', params: { prompt: 'x', size: '1024x1024', style_preset: 'foo' } }, ctxWith(upstream)),
      'unlisted-param',
      'generate-image',
    );
    assert.equal(upstream.calls.length, 0);
  });

  it('rejects an unknown preset key with unknown-preset and zero upstream calls', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const err = await assertProxyError(
      dispatch({ route: 'reason', params: {}, preset: 'no-such-preset-xyz' }, ctxWith(upstream)),
      'unknown-preset',
      'reason',
    );
    assert.equal(err.retryable, false);
    assert.equal(upstream.calls.length, 0);
  });

  it('rejects a preset shaped as a raw non-Muse model id with non-muse-model and zero upstream calls (ASSUMPTION)', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const err = await assertProxyError(
      dispatch({ route: 'reason', params: {}, preset: 'gpt-4' }, ctxWith(upstream)),
      'non-muse-model',
      'reason',
    );
    assert.equal(err.retryable, false);
    assert.equal(upstream.calls.length, 0);
  });

  it('rejects a non-object params bag with schema and zero upstream calls', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await assertProxyError(
      dispatch({ route: 'segment', params: null }, ctxWith(upstream)),
      'schema',
      'segment',
    );
    assert.equal(upstream.calls.length, 0);
  });

  it('surfaces only documented error codes', async () => {
    const { dispatch } = loadImpl();
    const cases = [
      dispatch({ route: 'nope', params: {} }, ctxWith(okUpstream({}))).catch((e) => e),
      dispatch({ route: 'segment', params: { bogus: 1 } }, ctxWith(okUpstream({}))).catch((e) => e),
      dispatch({ route: 'reason', params: {}, preset: 'no-such-preset-xyz' }, ctxWith(okUpstream({}))).catch((e) => e),
    ];
    for (const p of cases) {
      const err = await p;
      assert.ok(err && typeof err.code === 'string', 'rejection must carry a code');
      assert.ok(DOCUMENTED_CODES.has(err.code), `code must be documented, got '${err.code}'`);
    }
  });
});

describe('dispatch key custody and upstream errors', () => {
  it('maps an injectKey failure to auth without calling upstream', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const err = await assertProxyError(
      dispatch(
        { route: 'segment', params: { mask_encoding: 'polygon' } },
        { injectKey: () => { throw new Error('missing key'); }, upstream },
      ),
      'auth',
      'segment',
    );
    assert.equal(err.retryable, false);
    assert.equal(upstream.calls.length, 0);
  });

  it('retries a retryable upstream failure on reason and surfaces upstream when exhausted', async () => {
    const { dispatch } = loadImpl();
    const boom = new Error('upstream 503');
    boom.retryable = true;
    const upstream = recordingUpstream([boom, boom]);
    const err = await assertProxyError(
      dispatch({ route: 'reason', params: { reasoning_effort: 'high' } }, ctxWith(upstream)),
      'upstream',
      'reason',
    );
    assert.equal(err.retryable, true);
    assert.equal(upstream.calls.length, 2, 'reason retry policy is 1 retry (2 attempts)');
  });

  it('never retries generate-image (retry 0: non-idempotent)', async () => {
    const { dispatch } = loadImpl();
    const boom = new Error('upstream 500');
    boom.retryable = true;
    const upstream = recordingUpstream([boom]);
    await assertProxyError(
      dispatch({ route: 'generate-image', params: { prompt: 'x', size: '1024x1024' } }, ctxWith(upstream)),
      'upstream',
      'generate-image',
    );
    assert.equal(upstream.calls.length, 1, 'generate-image must attempt exactly once');
  });

  it('maps an upstream timeout to timeout with retryable true', async () => {
    const { dispatch } = loadImpl();
    const te = new Error('timed out');
    te.code = 'timeout';
    const upstream = recordingUpstream([te, te]);
    const err = await assertProxyError(
      dispatch({ route: 'transcribe', params: { mode: 'PUSH_TO_TALK' } }, ctxWith(upstream)),
      'timeout',
      'transcribe',
    );
    assert.equal(err.retryable, true);
  });
});

describe('dispatch security-review coverage (Stage 4)', () => {
  it('auth error carries no key material even when injectKey throws a distinctive sentinel', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const sentinel = 'SENTINEL-NO-KEY-MATERIAL-not-real-7f3a';
    const err = await assertProxyError(
      dispatch(
        { route: 'segment', params: { mask_encoding: 'polygon' } },
        { injectKey: () => { throw new Error(sentinel); }, upstream },
      ),
      'auth',
      'segment',
    );
    assert.equal(err.retryable, false);
    assert.equal(upstream.calls.length, 0);
    const serialized = JSON.stringify({ name: err.name, code: err.code, route: err.route, message: err.message, retryable: err.retryable });
    assert.ok(!serialized.includes(sentinel), 'auth error must not echo the injectKey failure text');
    assert.ok(!serialized.includes('test-key-not-real'), 'auth error must not carry key material');
    assert.ok(!/sk-[A-Za-z0-9]/.test(serialized), 'auth error must not carry a key-shaped substring');
  });

  it('rejects a __proto__ param key pre-upstream with zero upstream calls and no prototype pollution', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const params = JSON.parse('{"__proto__":{"polluted":"yes"},"mask_encoding":"polygon"}');
    const err = await assertProxyError(
      dispatch({ route: 'segment', params }, ctxWith(upstream)),
      'unlisted-param',
      'segment',
    );
    assert.equal(err.retryable, false);
    assert.equal(upstream.calls.length, 0);
    assert.equal({}.polluted, undefined, 'Object.prototype must not be polluted');
  });

  it('rejects a constructor param key pre-upstream with zero upstream calls', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const params = JSON.parse('{"constructor":"evil","mask_encoding":"polygon"}');
    const err = await assertProxyError(
      dispatch({ route: 'segment', params }, ctxWith(upstream)),
      'unlisted-param',
      'segment',
    );
    assert.equal(err.retryable, false);
    assert.equal(upstream.calls.length, 0);
  });

  it('rejects a __proto__ route pre-upstream with zero upstream calls', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const err = await assertProxyError(
      dispatch({ route: '__proto__', params: {} }, ctxWith(upstream)),
      'unknown-route',
      '__proto__',
    );
    assert.equal(err.retryable, false);
    assert.equal(upstream.calls.length, 0);
  });

  it('never echoes targetId in the response while forwarding it upstream (commit-time binding only)', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({ ok: true });
    const res = await dispatch(
      { route: 'reason', params: { reasoning_effort: 'high' }, turnId: 't-1', targetId: 'target-abc-123' },
      ctxWith(upstream),
    );
    assert.equal(res.status, 200);
    assert.ok(!('targetId' in res), 'response must never carry targetId');
    assert.equal(res.turnId, 't-1', 'turnId passthrough is unaffected');
    assert.equal(upstream.calls.length, 1);
    assert.equal(upstream.calls[0].body.targetId, 'target-abc-123', 'targetId is forwarded upstream opaquely');
  });

  it('never echoes targetId when the request carries no turnId', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({ ok: true });
    const res = await dispatch(
      { route: 'segment', params: { mask_encoding: 'polygon' }, targetId: 'target-xyz-789' },
      ctxWith(upstream),
    );
    assert.ok(!('targetId' in res), 'response must never carry targetId');
    assert.equal(upstream.calls[0].body.targetId, 'target-xyz-789');
  });
});

describe('BUG-MUSE-003 T3 — projectMuse resolution (AC-MUSE-046, FR-MUSE-002)', () => {
  // Hermetic project-block fixture (recording-upstream-fake pattern: no
  // repo-root reads, no network, no keys). Shape mirrors the T1
  // `resolveMuseSection` value object `{ models, presets }`. The pinned
  // model is an allow-listed non-default for the route ('sam-3.1' on
  // 'reason', whose proxy default is 'muse-spark-1.3'); preset params use
  // allow-listed keys only (reason: reasoning_effort/include_reasoning/strict).
  function projectMuse() {
    return {
      models: { reason: 'sam-3.1' },
      presets: {
        'reason-default': {
          route: 'reason',
          params: { reasoning_effort: 'low', strict: true },
        },
      },
    };
  }

  it('absent projectMuse keeps today\u2019s wire shape: params verbatim, no model key', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await dispatch(
      { route: 'reason', params: { reasoning_effort: 'high' } },
      ctxWith(upstream),
    );
    assert.equal(upstream.calls.length, 1);
    assert.deepEqual(upstream.calls[0].body.params, { reasoning_effort: 'high' });
    assert.ok(!('model' in upstream.calls[0].body), 'absent projectMuse must not add a model key');
  });

  it('pinned allow-listed model reaches the upstream call', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await dispatch(
      { route: 'reason', params: { reasoning_effort: 'high' }, projectMuse: projectMuse() },
      ctxWith(upstream),
    );
    assert.equal(upstream.calls.length, 1);
    // ASSUMPTION (see unit-tests/T3.md): the resolved model rides
    // `body.model`; the plan Interface pins resolution, not the wire key.
    assert.equal(upstream.calls[0].body.model, 'sam-3.1');
  });

  it('merges preset params under caller params with caller-wins on conflict (D1)', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await dispatch(
      {
        route: 'reason',
        params: { reasoning_effort: 'high' },
        preset: 'reason-default',
        projectMuse: projectMuse(),
      },
      ctxWith(upstream),
    );
    assert.equal(upstream.calls.length, 1);
    assert.deepEqual(upstream.calls[0].body.params, {
      reasoning_effort: 'high',
      strict: true,
    });
  });

  it('preset supplies defaults the caller omitted', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await dispatch(
      { route: 'reason', params: {}, preset: 'reason-default', projectMuse: projectMuse() },
      ctxWith(upstream),
    );
    assert.equal(upstream.calls.length, 1);
    assert.deepEqual(upstream.calls[0].body.params, {
      reasoning_effort: 'low',
      strict: true,
    });
  });

  it('pinned model resolves without a preset while params pass through verbatim', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await dispatch(
      { route: 'reason', params: { reasoning_effort: 'high' }, projectMuse: projectMuse() },
      ctxWith(upstream),
    );
    assert.equal(upstream.calls.length, 1);
    assert.equal(upstream.calls[0].body.model, 'sam-3.1');
    assert.deepEqual(upstream.calls[0].body.params, { reasoning_effort: 'high' });
  });

  it('empty projectMuse maps behave like an absent block (ASSUMPTION)', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await dispatch(
      { route: 'reason', params: { reasoning_effort: 'high' }, projectMuse: { models: {}, presets: {} } },
      ctxWith(upstream),
    );
    assert.equal(upstream.calls.length, 1);
    assert.deepEqual(upstream.calls[0].body.params, { reasoning_effort: 'high' });
    assert.ok(!('model' in upstream.calls[0].body), 'unpinned route must not add a model key');
  });

  it('still rejects an unknown route with projectMuse present and zero upstream calls', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await assertProxyError(
      dispatch({ route: 'speak', params: {}, projectMuse: projectMuse() }, ctxWith(upstream)),
      'unknown-route',
      'speak',
    );
    assert.equal(upstream.calls.length, 0);
  });

  it('still rejects an unlisted caller param with projectMuse present and zero upstream calls', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await assertProxyError(
      dispatch(
        { route: 'reason', params: { reasoning_effort: 'high', evil: 1 }, projectMuse: projectMuse() },
        ctxWith(upstream),
      ),
      'unlisted-param',
      'reason',
    );
    assert.equal(upstream.calls.length, 0);
  });

  it('still rejects an unknown preset with projectMuse present and zero upstream calls', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await assertProxyError(
      dispatch(
        { route: 'reason', params: {}, preset: 'no-such-preset-xyz', projectMuse: projectMuse() },
        ctxWith(upstream),
      ),
      'unknown-preset',
      'reason',
    );
    assert.equal(upstream.calls.length, 0);
  });

  it('still rejects a preset shaped as a raw non-Muse model id with projectMuse present', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    await assertProxyError(
      dispatch(
        { route: 'reason', params: {}, preset: 'gpt-4', projectMuse: projectMuse() },
        ctxWith(upstream),
      ),
      'non-muse-model',
      'reason',
    );
    assert.equal(upstream.calls.length, 0);
  });

  it('merging preset params never pollutes Object.prototype (ASSUMPTION: trusted artefact, spread merge)', async () => {
    const { dispatch } = loadImpl();
    const upstream = okUpstream({});
    const poisoned = projectMuse();
    poisoned.presets['reason-default'].params = JSON.parse(
      '{"reasoning_effort":"low","__proto__":{"polluted":"yes"}}',
    );
    await dispatch(
      { route: 'reason', params: {}, preset: 'reason-default', projectMuse: poisoned },
      ctxWith(upstream),
    );
    assert.equal(upstream.calls.length, 1);
    assert.equal({}.polluted, undefined, 'Object.prototype must not be polluted');
  });
});

describe('dispatch streaming passthrough', () => {
  it('passes reason SSE chunks through opaquely in order (ASSUMPTION: chunks array passthrough)', async () => {
    const { dispatch } = loadImpl();
    const chunks = ['{"t":"he"}', '{"t":"llo"}'];
    const upstream = recordingUpstream([{ status: 200, body: { chunks } }]);
    const res = await dispatch(
      { route: 'reason', params: { reasoning_effort: 'high' } },
      ctxWith(upstream),
    );
    assert.equal(res.status, 200);
    assert.deepEqual(res.body.chunks, chunks);
  });

  it('passes transcribe chunks through without merging or reordering', async () => {
    const { dispatch } = loadImpl();
    const chunks = ['a', 'b', 'c'];
    const upstream = recordingUpstream([{ status: 200, body: { chunks } }]);
    const res = await dispatch(
      { route: 'transcribe', params: { mode: 'PUSH_TO_TALK' } },
      ctxWith(upstream),
    );
    assert.deepEqual(res.body.chunks, chunks);
  });
});
