'use strict';
// TC-LAYOUT-026 (AC-LAYOUT-026) — Batch 3 T10.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T10 `Interface:` line, never to implementation.
// Module resolution: `../motion_tokens.js` once it lands, else an inline
// fallback stub that throws a GENERIC error (wrong name, no `name` field)
// so every behavior case below is red until the module lands — and the
// harness itself never crashes.
//
// ASSUMPTION (pinned names, in unit-tests/T10.md): the motion-subsystem set
// (FR-MOTION-013: durations short1..long4, easings standard/emphasized/
// accelerate/decelerate, spatial + effects springs) exposes at least
// 'motion.expressive.easing.standard', 'motion.expressive.easing.emphasized',
// 'motion.expressive.spring.standard' and 'motion.durations.medium' — names
// observed in the M3 token surface. Full deep-equality against the motion
// source for every name the matrix uses is the coder's proofs; this file
// pins the passthrough shape and the error contract.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../motion_tokens.js');
} catch {
  // Fallback stub: present so the harness loads; throws a generic error so
  // every behavior assertion below fails (red) until ../motion_tokens.js lands.
  mod = {
    motionToken: () => {
      throw new Error('motion tokens not implemented');
    },
  };
}
const { motionToken } = mod;

const PINNED_TOKENS = [
  'motion.expressive.easing.standard',
  'motion.expressive.easing.emphasized',
  'motion.expressive.spring.standard',
  'motion.durations.medium',
];

function assertMotionShape(tok, name) {
  assert.equal(typeof tok, 'object', `${name}: must return an object`);
  assert.equal(typeof tok.curve, 'string', `${name}: curve must be a string`);
  assert.ok(tok.curve.length > 0, `${name}: curve must be non-empty`);
  assert.equal(typeof tok.durationMs, 'number', `${name}: durationMs must be a number`);
  assert.ok(
    Number.isFinite(tok.durationMs) && tok.durationMs > 0,
    `${name}: durationMs must be a positive finite number`
  );
  if (tok.spring !== undefined) {
    assert.equal(typeof tok.spring, 'object', `${name}: spring must be an object when present`);
  }
  // Passthrough shape: no layout-local extra keys beyond the motion schema.
  for (const k of Object.keys(tok)) {
    assert.ok(
      k === 'curve' || k === 'durationMs' || k === 'spring',
      `${name}: unexpected layout-local key '${k}' — no local token table allowed`
    );
  }
}

// ---------------------------------------------------------------------------
// T10 contract:
// `motionToken(name: string): { curve: string, durationMs: number,
//  spring?: object }` — passthrough to the FR-MOTION-013 motion-subsystem set;
// throws `UnknownMotionTokenError { name }` for undefined names.
// ---------------------------------------------------------------------------
describe('TC-LAYOUT-026 — motion tokens are shared, not redefined (AC-LAYOUT-026)', () => {
  for (const name of PINNED_TOKENS) {
    it(`resolves pinned token '${name}' with the motion schema shape`, () => {
      assertMotionShape(motionToken(name), name);
    });
  }

  it('is deterministic: repeated calls deep-equal', () => {
    assert.deepEqual(motionToken(PINNED_TOKENS[0]), motionToken(PINNED_TOKENS[0]));
  });

  it('throws UnknownMotionTokenError carrying { name } for unknown names', () => {
    assert.throws(
      () => motionToken('motion.bogus.token'),
      (err) =>
        err.constructor.name === 'UnknownMotionTokenError' &&
        err.name === 'motion.bogus.token'
    );
  });

  it('throws UnknownMotionTokenError for an empty name', () => {
    assert.throws(
      () => motionToken(''),
      (err) => err.constructor.name === 'UnknownMotionTokenError' && err.name === ''
    );
  });

  it('throws UnknownMotionTokenError for non-string input (ASSUMPTION: treated as unknown)', () => {
    for (const bad of [123, null, undefined]) {
      assert.throws(
        () => motionToken(bad),
        (err) => err.constructor.name === 'UnknownMotionTokenError' && err.name === bad
      );
    }
  });

  it('does not resolve layout-local inventions: unknown prefix throws', () => {
    assert.throws(
      () => motionToken('layout.custom.bounce'),
      (err) =>
        err.constructor.name === 'UnknownMotionTokenError' &&
        err.name === 'layout.custom.bounce'
    );
  });
});
