'use strict';
// T5 — Regression test: yaml resolution reaches the upstream call (TC-MUSE-048).
// Hermetic: node:test + node:assert/strict only. Mirrors the
// recording-upstream-fake pattern from proxy/test/dispatch.test.js.
// Fixtures carry an in-memory project `muse:` block (pinned model + preset
// params); NEVER the repo-root muse-native.yaml, never examples/* yaml
// (RULE-WORKFLOW-007). Asserts against the T1/T3 interfaces only:
//   T1 `resolveMuseSection(yaml, allowList)` (packages/cli/lib/muse-models.js)
//   T3 `dispatch(req, ctx)` with optional `req.projectMuse` (proxy/lib/dispatch.js)

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');

function loadDispatch() {
  return require('../lib/dispatch.js');
}

function loadModels() {
  return require('../../packages/cli/lib/muse-models.js');
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

// Recording upstream fake (same shape as dispatch.test.js): records every
// call, replays a scripted response.
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

// Inline allow-list fixture (mirrors proxy/proxy.config.json IDs used in
// packages/cli/test/muse-models.test.js). Inline so the test never reads the
// repo-root config as application configuration.
function allowList() {
  return {
    version: 1,
    routes: {
      segment: { model: 'sam-3.1', params: { mask_encoding: 'polygon' }, stream: 'none', timeoutMs: 8000, retry: 1 },
      reason: { model: 'muse-spark-1.3', params: { reasoning_effort: 'high', include_reasoning: true, strict: true }, stream: 'sse', timeoutMs: 30000, retry: 1 },
      structure: { model: 'muse-spark-1.3', params: { strict: true }, stream: 'sse', timeoutMs: 30000, retry: 1 },
      transcribe: { model: 'muse-voice-transcribe-1.0', params: { mode: 'PUSH_TO_TALK' }, stream: 'sse', timeoutMs: 30000, retry: 1 },
      'generate-image': { model: 'muse-image', params: { prompt: 'x', size: '1024x1024' }, stream: 'progressive', timeoutMs: 60000, retry: 0 },
    },
  };
}

// Hermetic project `muse:` block fixture: a pinned allow-listed non-default
// model for `reason` ('sam-3.1', whose proxy default is 'muse-spark-1.3') plus
// a preset carrying allow-listed params only.
function projectMuseFixture() {
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

describe('T5 yaml resolution reaches the upstream call (TC-MUSE-048)', () => {
  it('pinned model from the project block reaches the upstream call', async () => {
    const { dispatch } = loadDispatch();
    const upstream = okUpstream({});
    await dispatch(
      { route: 'reason', params: { reasoning_effort: 'high' }, projectMuse: projectMuseFixture() },
      ctxWith(upstream),
    );
    assert.equal(upstream.calls.length, 1);
    assert.equal(upstream.calls[0].body.model, 'sam-3.1');
  });

  it('preset params merge caller-wins with preset as defaults (D1)', async () => {
    const { dispatch } = loadDispatch();
    const upstream = okUpstream({});
    await dispatch(
      {
        route: 'reason',
        params: { reasoning_effort: 'high' },
        preset: 'reason-default',
        projectMuse: projectMuseFixture(),
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
    const { dispatch } = loadDispatch();
    const upstream = okUpstream({});
    await dispatch(
      { route: 'reason', params: {}, preset: 'reason-default', projectMuse: projectMuseFixture() },
      ctxWith(upstream),
    );
    assert.equal(upstream.calls.length, 1);
    assert.deepEqual(upstream.calls[0].body.params, {
      reasoning_effort: 'low',
      strict: true,
    });
    assert.equal(upstream.calls[0].body.model, 'sam-3.1');
  });

  it('T1 resolveMuseSection returns the project block verbatim for the dispatch fixture shape', () => {
    const { resolveMuseSection } = loadModels();
    const yaml = {
      muse: {
        models: { reason: 'sam-3.1' },
        presets: {
          'reason-default': { route: 'reason', params: { reasoning_effort: 'low', strict: true } },
        },
      },
    };
    const resolved = resolveMuseSection(yaml, allowList());
    assert.deepEqual(resolved, projectMuseFixture());
  });

  it('absent project block keeps today\u2019s wire shape: no model key, params verbatim', async () => {
    const { dispatch } = loadDispatch();
    const upstream = okUpstream({});
    await dispatch(
      { route: 'reason', params: { reasoning_effort: 'high' } },
      ctxWith(upstream),
    );
    assert.equal(upstream.calls.length, 1);
    assert.deepEqual(upstream.calls[0].body.params, { reasoning_effort: 'high' });
    assert.ok(!('model' in upstream.calls[0].body), 'absent block must not add a model key');
  });
});

describe('T5 fail-closed: unlisted model/param never reaches upstream (TC-MUSE-048)', () => {
  it('T1 resolveMuseSection throws ConfigError on an unlisted (non-Muse) pinned model', () => {
    const { resolveMuseSection } = loadModels();
    const yaml = { muse: { models: { reason: 'gpt-4' } } };
    let err;
    try {
      resolveMuseSection(yaml, allowList());
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError for unlisted model');
    assert.equal(err.name, 'ConfigError');
  });

  it('T1 resolveMuseSection throws ConfigError on an unlisted preset param', () => {
    const { resolveMuseSection } = loadModels();
    const yaml = {
      muse: {
        presets: {
          'reason-default': { route: 'reason', params: { evil_param: 1 } },
        },
      },
    };
    let err;
    try {
      resolveMuseSection(yaml, allowList());
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError for unlisted preset param');
    assert.equal(err.name, 'ConfigError');
  });

  it('dispatch rejects an unlisted caller param pre-upstream with the project block present', async () => {
    const { dispatch } = loadDispatch();
    const upstream = okUpstream({});
    let err;
    try {
      await dispatch(
        {
          route: 'reason',
          params: { reasoning_effort: 'high', evil: 1 },
          preset: 'reason-default',
          projectMuse: projectMuseFixture(),
        },
        ctxWith(upstream),
      );
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected unlisted-param rejection');
    assert.equal(err.code, 'unlisted-param');
    assert.equal(err.retryable, false);
    assert.equal(upstream.calls.length, 0);
  });

  it('dispatch rejects an unknown preset pre-upstream with the project block present', async () => {
    const { dispatch } = loadDispatch();
    const upstream = okUpstream({});
    let err;
    try {
      await dispatch(
        { route: 'reason', params: {}, preset: 'no-such-preset-xyz', projectMuse: projectMuseFixture() },
        ctxWith(upstream),
      );
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected unknown-preset rejection');
    assert.equal(err.code, 'unknown-preset');
    assert.equal(upstream.calls.length, 0);
  });

  it('dispatch rejects a raw non-Muse model id as preset pre-upstream with the project block present', async () => {
    const { dispatch } = loadDispatch();
    const upstream = okUpstream({});
    let err;
    try {
      await dispatch(
        { route: 'reason', params: {}, preset: 'gpt-4', projectMuse: projectMuseFixture() },
        ctxWith(upstream),
      );
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected non-muse-model rejection');
    assert.equal(err.code, 'non-muse-model');
    assert.equal(upstream.calls.length, 0);
  });
});
