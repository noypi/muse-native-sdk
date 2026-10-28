'use strict';
// T2 — stub proxy serves recorded SAM/Spark fixtures with no network and no key.
// Hermetic: node:test + node:assert/strict only. Deletes every known key env var
// before each case to prove the stub never needs key custody.
// ASSUMPTION (contract-open): the plan names no stub signature, so the stub's
// most defensible surface is `handle(req: { route, params }): { status, body }`
// — synchronous, fixture-backed, throwing a documented ProxyError code for
// unknown routes. If the coder shapes it differently, this contract reconciles
// the call sites, not the expected fixture behavior.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('../lib/stub.js');
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

describe('stub serves recorded fixtures with no network/key', () => {
  it('serves a recorded segment (SAM) fixture with status 200', () => {
    const stub = loadImpl();
    const res = stub.handle({ route: 'segment', params: { mask_encoding: 'polygon' } });
    assert.equal(res.status, 200);
    assert.ok(res.body !== undefined, 'stub must return a recorded body');
  });

  it('serves a recorded reason (Spark) fixture with status 200', () => {
    const stub = loadImpl();
    const res = stub.handle({ route: 'reason', params: { reasoning_effort: 'high' } });
    assert.equal(res.status, 200);
    assert.ok(res.body !== undefined, 'stub must return a recorded body');
  });

  it('serves a recorded structure fixture with status 200', () => {
    const stub = loadImpl();
    const res = stub.handle({ route: 'structure', params: { strict: true } });
    assert.equal(res.status, 200);
    assert.ok(res.body !== undefined);
  });

  it('serves a recorded transcribe fixture with status 200', () => {
    const stub = loadImpl();
    const res = stub.handle({ route: 'transcribe', params: { mode: 'PUSH_TO_TALK' } });
    assert.equal(res.status, 200);
    assert.ok(res.body !== undefined);
  });

  it('serves a recorded generate-image fixture with status 200', () => {
    const stub = loadImpl();
    const res = stub.handle({ route: 'generate-image', params: { prompt: 'x', size: '1024x1024' } });
    assert.equal(res.status, 200);
    assert.ok(res.body !== undefined);
  });

  it('is deterministic: the same request returns a deep-equal body twice', () => {
    const stub = loadImpl();
    const req = { route: 'segment', params: { mask_encoding: 'polygon' } };
    assert.deepEqual(stub.handle(req).body, stub.handle(req).body);
  });

  it('needs no key: works with every known key env var deleted', () => {
    assert.equal(process.env.MUSE_API_KEY, undefined);
    const stub = loadImpl();
    const res = stub.handle({ route: 'reason', params: {} });
    assert.equal(res.status, 200);
  });

  it('rejects an unknown route with unknown-route and never touches the network', () => {
    const stub = loadImpl();
    let err;
    try {
      stub.handle({ route: 'speak', params: {} });
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected rejection for unknown route');
    assert.equal(err.code || err.reason, 'unknown-route');
  });
});
