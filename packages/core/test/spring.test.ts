'use strict';
// TC-TSX-030 (AC-TSX-030) — Batch 3 T7.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T7 `Interface:` line, never to implementation.
// Validator resolution: `../index.js` (real @muse/core module) once `spring`
// lands, else an inline fallback stub whose behavior assertions fail, so every
// behavior case below is red until spring() lands — and the harness itself
// never crashes. NOTE: T7 extends @muse/core — the test requires the real
// module once implemented.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../index.js');
} catch {
  mod = null;
}
if (!mod || typeof mod.spring !== 'function') {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until spring() lands.
  mod = {
    spring: () => ({ value: 'STUB', animateTo: () => {}, getVelocity: () => -999 }),
  };
}
const { spring } = mod;

// ---------------------------------------------------------------------------
// TC-TSX-030 → AC-TSX-030 — T7 contract:
// `spring(initial: number): {value: number, animateTo(target: number, opts?: {velocity?: number}): void, getVelocity(): number}`
// ---------------------------------------------------------------------------
describe('TC-TSX-030 — spring() writable handle with velocity re-targeting (AC-TSX-030)', () => {
  it('spring(0) exposes value 0', () => {
    const h = spring(0);
    assert.equal(h.value, 0);
  });

  it('value is writable (h.value = 5 → 5)', () => {
    const h = spring(0);
    h.value = 5;
    assert.equal(h.value, 5);
  });

  it('animateTo(10, {velocity: 2}) records target + velocity (getVelocity() === 2)', () => {
    const h = spring(0);
    h.animateTo(10, { velocity: 2 });
    assert.equal(h.value, 10);
    assert.equal(h.getVelocity(), 2);
  });

  it('re-target animateTo(20) without velocity preserves value and prior velocity', () => {
    const h = spring(0);
    h.animateTo(10, { velocity: 2 });
    h.animateTo(20);
    assert.equal(h.value, 20);
    assert.equal(h.getVelocity(), 2);
  });

  it('getVelocity() defaults to 0', () => {
    const h = spring(3);
    assert.equal(h.getVelocity(), 0);
  });
});
