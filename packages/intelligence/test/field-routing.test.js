'use strict';
// T4 — MuseField privacy-auto routing (AC-MUSE-011) + T6 unit half (AC-MUSE-013).
// Hermetic: dispatch boundary faked via require-cache seeding; no network, no keys.
// Uses node:test + node:assert.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const DISPATCH_PATH = path.resolve(__dirname, '../../../proxy/lib/dispatch.js');

function seedDispatchFake(fake) {
  require.cache[DISPATCH_PATH] = {
    id: DISPATCH_PATH,
    filename: DISPATCH_PATH,
    loaded: true,
    exports: { dispatch: fake },
  };
}

function freshField() {
  delete require.cache[path.resolve(__dirname, '../lib/field.js')];
  return require('../lib/field.js');
}

beforeEach(() => {
  delete require.cache[DISPATCH_PATH];
  delete require.cache[path.resolve(__dirname, '../lib/field.js')];
});

describe('classifyAndRoute — privacy auto routes every class to Muse reason (AC-MUSE-011)', () => {
  it('routes sensitive context (medical_symptoms) to reason with reason exposed', async () => {
    const seen = [];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: { answer: 'Summary for clinician.', classification: 'sensitive', reason: 'medical_symptoms→sensitive' } };
    });
    const { classifyAndRoute } = freshField();
    const res = await classifyAndRoute({
      text: 'fever for three days',
      context: 'medical_symptoms',
      privacy: 'auto',
      prompt: 'Summarize these symptoms for a clinician, no diagnosis.',
    });
    assert.equal(res.route, 'reason');
    assert.equal(typeof res.reason, 'string');
    assert.ok(res.reason.length > 0);
    assert.equal(typeof res.classification, 'string');
    assert.equal(typeof res.latencyMs, 'number');
    assert.equal(typeof res.answer, 'string');
    assert.equal(seen.length, 1);
    assert.equal(seen[0].route, 'reason');
  });

  it('routes a non-sensitive class to reason too, with a distinct reason', async () => {
    seedDispatchFake(async () => (
      { status: 200, body: { answer: 'Caption.', classification: 'general', reason: 'general→reason' } }
    ));
    const { classifyAndRoute } = freshField();
    const sensitive = await classifyAndRoute({
      text: 'fever', context: 'medical_symptoms', privacy: 'auto', prompt: 'Summarize.',
    });
    const general = await classifyAndRoute({
      text: 'a red kettle', context: 'shopping_query', privacy: 'auto', prompt: 'Caption this.',
    });
    assert.equal(sensitive.route, 'reason');
    assert.equal(general.route, 'reason');
    assert.notEqual(sensitive.reason, general.reason);
  });

  it('never routes to a non-Muse target', async () => {
    const seen = [];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: { answer: 'ok', classification: 'general', reason: 'general→reason' } };
    });
    const { classifyAndRoute } = freshField();
    for (const context of ['medical_symptoms', 'shopping_query', 'masked:kettle:photo1', '']) {
      const res = await classifyAndRoute({ text: 'hello', context, privacy: 'auto', prompt: 'Do it.' });
      assert.equal(res.route, 'reason');
    }
    assert.ok(seen.every((r) => r.route === 'reason'));
  });

  it('sends prompt as the task instruction and context for routing only (never mixed)', async () => {
    const seen = [];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: { answer: 'ok', classification: 'general', reason: 'general→reason' } };
    });
    const { classifyAndRoute } = freshField();
    await classifyAndRoute({
      text: 'fever for three days',
      context: 'medical_symptoms',
      privacy: 'auto',
      prompt: 'Summarize these symptoms for a clinician, no diagnosis.',
    });
    assert.equal(seen.length, 1);
    assert.equal(seen[0].prompt, 'Summarize these symptoms for a clinician, no diagnosis.');
    // ASSUMPTION: routing-only context travels in a `context` field and the
    // prompt carries no routing context (the two never mix per spec Notes).
    assert.equal(seen[0].context, 'medical_symptoms');
    assert.ok(!String(seen[0].prompt).includes('medical_symptoms'));
  });

  it('forwards preset-name-only; caller extras (model ids, endpoints) never reach dispatch', async () => {
    const seen = [];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: { answer: 'ok', classification: 'general', reason: 'general→reason' } };
    });
    const { classifyAndRoute } = freshField();
    await classifyAndRoute({
      text: 'hi', context: 'shopping_query', privacy: 'auto', prompt: 'Caption.',
      preset: 'concise', model: 'non-muse-1', endpoint: 'https://evil.test',
    });
    assert.equal(seen[0].preset, 'concise');
    assert.equal(seen[0].model, undefined);
    assert.equal(seen[0].endpoint, undefined);
  });

  it('throws only proxy taxonomy errors (never raw upstream)', async () => {
    const { classifyAndRoute } = freshField();
    for (const code of ['auth', 'upstream', 'timeout', 'schema']) {
      const proxyErr = new Error(`stubbed ${code}`);
      proxyErr.code = code;
      seedDispatchFake(async () => { throw proxyErr; });
      await assert.rejects(
        classifyAndRoute({ text: 'hi', context: 'shopping_query', privacy: 'auto', prompt: 'Caption.' }),
        (err) => err && err.code === code,
      );
    }
  });

  it('wraps non-taxonomy failures as upstream (proxy taxonomy only)', async () => {
    seedDispatchFake(async () => { throw new Error('raw socket explosion'); });
    const { classifyAndRoute } = freshField();
    await assert.rejects(
      classifyAndRoute({ text: 'hi', context: 'shopping_query', privacy: 'auto', prompt: 'Caption.' }),
      (err) => err && err.code === 'upstream',
    );
  });
});

describe('fieldError — named error states keep text, taxonomy only (AC-MUSE-013, T6 unit half)', () => {
  const cases = [
    ['auth', 'stubbed auth failure'],
    ['upstream', 'stubbed upstream failure'],
    ['timeout', 'stubbed timeout'],
    ['schema', 'stubbed schema violation'],
  ];
  for (const [code, detail] of cases) {
    it(`maps ${code} to its named error state with text preserved`, () => {
      const { fieldError } = freshField();
      // ASSUMPTION: retryable mapping (timeout/upstream retryable; auth/schema not)
      // is plan-silent; tests pin the defensible mapping below.
      const expectedRetryable = code === 'timeout' || code === 'upstream';
      const res = fieldError(code, detail);
      assert.equal(res.state, 'error');
      assert.equal(res.code, code);
      assert.equal(typeof res.message, 'string');
      assert.ok(res.message.length > 0);
      assert.equal(res.retryable, expectedRetryable);
      // Never partial text / raw upstream: no answer/text payload on the state.
      assert.equal(res.answer, undefined);
      assert.equal(res.text, undefined);
    });
  }

  it('rejects non-taxonomy codes', () => {
    const { fieldError } = freshField();
    assert.throws(() => fieldError('local-fallback', 'x'));
    assert.throws(() => fieldError('on-device', 'x'));
  });
});
