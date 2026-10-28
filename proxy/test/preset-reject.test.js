'use strict';
// T4 — preset-name-only component contract (type + proxy rejection).
// Hermetic: node:test + node:assert/strict (+ node:fs/os/path for the .d.ts
// static half), all local files, no network, no keys, no live services.
// Static half reads packages/jsx/muse-preset.d.ts as text (production type
// claim — NOT a test file). Behavioral half drives proxy/lib/dispatch.js
// with a recording upstream fake and asserts pre-upstream rejection.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const DTS_PATH = path.join(__dirname, '..', '..', 'packages', 'jsx', 'muse-preset.d.ts');

function strippedDts() {
  const raw = fs.readFileSync(DTS_PATH, 'utf8');
  return raw.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
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

// ---------------------------------------------------------------------------
// Static half — muse-preset.d.ts declares the shared MusePresetProps mixin.
// ---------------------------------------------------------------------------
describe('T4 static — muse-preset.d.ts preset-name-only mixin (AC-MUSE-035)', () => {
  it('declares a PresetKey type alias (string until codegen lands)', () => {
    const dts = strippedDts();
    assert.ok(
      /\btype\s+PresetKey\s*=\s*string\b/.test(dts),
      'muse-preset.d.ts must declare `type PresetKey = string`',
    );
  });

  it('declares MusePresetProps with an optional `preset` prop', () => {
    const dts = strippedDts();
    const m = dts.match(/MusePresetProps\s*\{([^}]*)\}/);
    assert.ok(m, 'muse-preset.d.ts must declare a MusePresetProps mixin');
    assert.ok(/\bpreset\?\s*:/.test(m[1]), 'MusePresetProps must declare optional `preset?`');
    assert.ok(/PresetKey|string/.test(m[1]), '`preset` must be typed PresetKey (string until codegen)');
  });

  it('declares no raw model/endpoint/parameter props (tsc TS2322-class surface)', () => {
    const dts = strippedDts();
    const m = dts.match(/MusePresetProps\s*\{([^}]*)\}/);
    assert.ok(m, 'precondition: MusePresetProps mixin must exist');
    for (const prop of ['model', 'endpoint', 'reasoning_effort', 'mask_encoding']) {
      assert.ok(
        !new RegExp(`\\b${prop}\\s*[?:]`).test(m[1]),
        `MusePresetProps must not declare a raw \`${prop}\` prop`,
      );
    }
  });
});

// ---------------------------------------------------------------------------
// Behavioral half — dispatch rejects bad presets pre-upstream (AC-MUSE-036).
// ---------------------------------------------------------------------------
describe('T4 behavioral — dispatch preset rejection pre-upstream (AC-MUSE-036)', () => {
  it('rejects an unknown preset key with unknown-preset and zero upstream calls', async () => {
    const { dispatch } = require('../lib/dispatch.js');
    const upstream = recordingUpstream([{ status: 200, body: { ok: true } }]);
    await assertProxyError(
      dispatch(
        { route: 'reason', params: {}, preset: 'no-such-preset' },
        { injectKey: () => 'test-key-not-real', upstream },
      ),
      'unknown-preset',
      'reason',
    );
    assert.equal(upstream.calls.length, 0, 'rejection must happen before any upstream call');
  });

  it('rejects a preset naming a non-Muse model with non-muse-model pre-upstream', async () => {
    const { dispatch } = require('../lib/dispatch.js');
    const upstream = recordingUpstream([{ status: 200, body: { ok: true } }]);
    await assertProxyError(
      dispatch(
        { route: 'reason', params: {}, preset: 'gpt-4-turbo' },
        { injectKey: () => 'test-key-not-real', upstream },
      ),
      'non-muse-model',
      'reason',
    );
    assert.equal(upstream.calls.length, 0, 'rejection must happen before any upstream call');
  });

  it('rejects an empty/non-string preset with unknown-preset pre-upstream', async () => {
    const { dispatch } = require('../lib/dispatch.js');
    for (const bad of ['', 42]) {
      const upstream = recordingUpstream([{ status: 200, body: { ok: true } }]);
      await assertProxyError(
        dispatch(
          { route: 'segment', params: { mask_encoding: 'polygon' }, preset: bad },
          { injectKey: () => 'test-key-not-real', upstream },
        ),
        'unknown-preset',
        'segment',
      );
      assert.equal(upstream.calls.length, 0, `preset ${JSON.stringify(bad)} must not reach upstream`);
    }
  });

  it('absent preset resolves to the route default (forwards upstream, no preset error)', async () => {
    const { dispatch } = require('../lib/dispatch.js');
    const upstream = recordingUpstream([{ status: 200, body: { mask: 'polygon' } }]);
    const res = await dispatch(
      { route: 'segment', params: { mask_encoding: 'polygon' } },
      { injectKey: () => 'test-key-not-real', upstream },
    );
    assert.equal(res.status, 200);
    assert.equal(upstream.calls.length, 1, 'absent preset must forward to upstream');
  });

  it('preset errors carry the route and a boolean retryable flag', async () => {
    const { dispatch } = require('../lib/dispatch.js');
    const upstream = recordingUpstream([{ status: 200, body: { ok: true } }]);
    const err = await assertProxyError(
      dispatch(
        { route: 'structure', params: {}, preset: 'definitely-unknown' },
        { injectKey: () => 'test-key-not-real', upstream },
      ),
      'unknown-preset',
      'structure',
    );
    assert.equal(err.retryable, false, 'validation failures are not retryable');
  });
});
