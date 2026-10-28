'use strict';
// T5 — Versioned snapshot schema with migration and safe discard (Batch 3).
// Plan Interface (verbatim):
// `wrapSnapshot(payload: object, version?: number): { version: number, payload: object }; unwrapSnapshot(raw: unknown, migrations?: Record<number, (p: object) => object>): { status: 'current' | 'migrated' | 'discarded', payload: object | null }` — current version `SNAPSHOT_VERSION = 2`; unknown/corrupt/older-than-supported returns `{status:'discarded', payload:null}` without throwing.
// Spec refs: FR-LIFECYCLE-005, AC-LIFECYCLE-010 (TC-LIFECYCLE-010).
// Mirrors TC-LIFECYCLE-010 (snapshot_schema_test.cpp).
// Hermetic: node:test + node:assert/strict only; no network/fs/secrets/live services.
// ASSUMPTIONs: injected v1→v2 map is a test-local rename fixture (plan names a
// "documented map" without pinning field names; the coder applies whatever
// migrations record the caller supplies); v1 input with no matching migration
// entry is discarded as older-than-supported.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { wrapSnapshot, unwrapSnapshot, SNAPSHOT_VERSION } = require('../src/snapshot_schema.js');

describe('TC-LIFECYCLE-010 snapshot schema — module shape and current version', () => {
  it('exports wrapSnapshot/unwrapSnapshot and SNAPSHOT_VERSION = 2', () => {
    assert.equal(typeof wrapSnapshot, 'function');
    assert.equal(typeof unwrapSnapshot, 'function');
    assert.equal(SNAPSHOT_VERSION, 2);
  });
});

describe('TC-LIFECYCLE-010 snapshot schema — wrapSnapshot', () => {
  it('wraps a payload at the current version by default', () => {
    const payload = { nav: 'home', scroll: 12 };
    assert.deepEqual(wrapSnapshot(payload), { version: 2, payload });
  });

  it('explicit version argument is preserved on the envelope', () => {
    assert.deepEqual(wrapSnapshot({ a: 1 }, 1), { version: 1, payload: { a: 1 } });
    assert.deepEqual(wrapSnapshot({ a: 1 }, 2), { version: 2, payload: { a: 1 } });
  });

  it('wrap then unwrap round-trips a v2 payload as current', () => {
    const payload = { nav: 'home', scroll: 0, text: 'hi', focus: 'field-1' };
    const raw = wrapSnapshot(payload);
    assert.deepEqual(unwrapSnapshot(raw), { status: 'current', payload });
  });

  it('wrap does not mutate its input payload', () => {
    // ASSUMPTION: plan silent on aliasing; non-mutating wrap is most defensible.
    const payload = { nested: { x: 1 } };
    const before = JSON.parse(JSON.stringify(payload));
    wrapSnapshot(payload);
    assert.deepEqual(payload, before);
  });

  it('mutating the input after wrap does not affect the envelope (wrap copies)', () => {
    // Regression for review MINOR: wrapSnapshot must deep-copy the payload;
    // pre-fix `wrap(p); p.a = 999` leaked into `raw.payload.a`.
    const payload = { a: 1, nested: { x: 1 } };
    const raw = wrapSnapshot(payload);
    payload.a = 999;
    payload.nested.x = 999;
    assert.deepEqual(raw, { version: 2, payload: { a: 1, nested: { x: 1 } } });
  });

  it('mutating the unwrapped current payload does not affect the held envelope (unwrap copies)', () => {
    // Regression for review MINOR: the `current` path must return a copy;
    // pre-fix mutating the object returned by unwrap/relaunch corrupted the
    // held raw so a second unwrap saw the changed value.
    const raw = wrapSnapshot({ a: 1, nested: { x: 1 } });
    const first = unwrapSnapshot(raw);
    assert.equal(first.status, 'current');
    first.payload.a = 999;
    first.payload.nested.x = 999;
    assert.deepEqual(raw, { version: 2, payload: { a: 1, nested: { x: 1 } } });
    assert.deepEqual(unwrapSnapshot(raw), { status: 'current', payload: { a: 1, nested: { x: 1 } } });
  });
});

describe('TC-LIFECYCLE-010 snapshot schema — current version unwraps as current', () => {
  it('v2 envelope returns status current with the identical payload', () => {
    const payload = { nav: 'details/7', scroll: 42 };
    assert.deepEqual(unwrapSnapshot({ version: 2, payload }), { status: 'current', payload });
  });

  it('v2 envelope with an empty-object payload is still current', () => {
    assert.deepEqual(unwrapSnapshot({ version: 2, payload: {} }), { status: 'current', payload: {} });
  });
});

describe('TC-LIFECYCLE-010 snapshot schema — v1 migrates to v2', () => {
  it('v1 input with a matching migration entry returns migrated payload', () => {
    // Fixture map: legacy `title` renames to v2 `heading` (test-local stand-in
    // for the plan's "documented map"; the contract is that the supplied
    // migrations[1] function produces the v2 shape).
    const migrations = {
      1: (p) => ({ heading: p.title, scroll: p.scroll }),
    };
    const result = unwrapSnapshot({ version: 1, payload: { title: 'Home', scroll: 5 } }, migrations);
    assert.deepEqual(result, { status: 'migrated', payload: { heading: 'Home', scroll: 5 } });
  });

  it('migration result unwraps shape is exactly {status, payload}', () => {
    const migrations = { 1: (p) => ({ ...p, migrated: true }) };
    const result = unwrapSnapshot({ version: 1, payload: { a: 1 } }, migrations);
    assert.equal(result.status, 'migrated');
    assert.deepEqual(result.payload, { a: 1, migrated: true });
  });

  it('v1 input with no matching migration entry is discarded, not thrown', () => {
    // ASSUMPTION: older-than-supported without a migration path discards safely.
    assert.deepEqual(unwrapSnapshot({ version: 1, payload: { a: 1 } }), {
      status: 'discarded',
      payload: null,
    });
    assert.deepEqual(unwrapSnapshot({ version: 1, payload: { a: 1 } }, {}), {
      status: 'discarded',
      payload: null,
    });
  });
});

describe('TC-LIFECYCLE-010 snapshot schema — incompatible input discards without throwing', () => {
  const discardedCases = [
    ['null', null],
    ['undefined', undefined],
    ['string', 'v2-payload'],
    ['number', 42],
    ['array', [{ version: 2, payload: {} }]],
    ['empty object', {}],
    ['missing payload', { version: 2 }],
    ['null payload', { version: 2, payload: null }],
    ['non-object payload', { version: 2, payload: 7 }],
    ['string version', { version: '2', payload: {} }],
    ['missing version', { payload: {} }],
    ['version 0', { version: 0, payload: {} }],
    ['negative version', { version: -1, payload: {} }],
    ['unknown old version', { version: 0, payload: {} }],
    ['future version 3', { version: 3, payload: {} }],
    ['far future version 99', { version: 99, payload: {} }],
  ];

  for (const [name, raw] of discardedCases) {
    it(`discards ${name} as {status:'discarded', payload:null} without throwing`, () => {
      let result;
      assert.doesNotThrow(() => {
        result = unwrapSnapshot(raw);
      });
      assert.deepEqual(result, { status: 'discarded', payload: null });
    });
  }

  it('does not throw when migrations are omitted entirely for corrupt input', () => {
    assert.deepEqual(unwrapSnapshot(null), { status: 'discarded', payload: null });
    assert.deepEqual(unwrapSnapshot('garbage', { 1: (p) => p }), {
      status: 'discarded',
      payload: null,
    });
  });

  it('a throwing migration function does not propagate; input is discarded', () => {
    // ASSUMPTION: plan says incompatible snapshots never crash; a migration
    // that itself throws is treated as incompatible and discarded safely.
    const migrations = {
      1: () => {
        throw new Error('boom');
      },
    };
    let result;
    assert.doesNotThrow(() => {
      result = unwrapSnapshot({ version: 1, payload: { a: 1 } }, migrations);
    });
    assert.deepEqual(result, { status: 'discarded', payload: null });
  });
});
