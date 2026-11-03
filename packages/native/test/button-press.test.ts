/**
 * TC-M3-037 (AC-M3-036) — Button combined press() contract pin, Batch 1 RED.
 * Sibling of TC-M3-038 (AC-M3-037 endorsement-gate conformance), which is
 * EXCLUDED here and belongs to Batch 3 / T8 (`--conformance-gate`).
 *
 * Hermetic: node:test + node:assert/strict only. Imports the combined Button
 * contract through the package public surface (`../button.js`, the canonical
 * delivery-lane copy picked in Batch 2 per plan T7). No network, no secrets,
 * no services, no device.
 *
 * Batch-1 RED state: `../button.js` does not exist yet, so this file fails
 * at import today. Every test is written against the plan's `Interface:`
 * line verbatim:
 *   lease ticket `{tapId}` single-use: replay=>fired:0,
 *   disabled-never-mint=>null, forgery=>TypeError,
 *   throwing-listener=>recorded-no-escape; arbitration=>exactly 1 winner.
 *
 * ASSUMPTION M-1 (per-button mint): the plan names the lease behaviors but
 * no mint function. FR-M3-013 Notes scope the pending-registry per-button,
 * so minting is a handle method: `handle.mint()` returns a single-use
 * `{tapId}` lease, or `null` when disabled. Rename-safe: update call sites,
 * not behavior.
 * ASSUMPTION REC-1 (throw record channel): AC-M3-036 requires the throwing
 * listener to be "recorded (never throws through)". The record channel name
 * is unsettled; these tests read `result.errors` (array of the caught
 * errors, length 1). If the coder names it differently, update the call
 * site — not the behavior (fired:1 + no escape).
 * ASSUMPTION FR-1 (fold frame shape): `foldButtonFrames(rawFrames)->0|1` is
 * pure over raw frames. The raw frame field names are unsettled; these tests
 * use `{ tap: boolean, cancel: boolean, drag: boolean }` and pin only the
 * 0|1 codomain plus agreement with `press()` on the same tap. Rename-safe.
 * ASSUMPTION ARB-1 (arbitration locus): AC-M3-036 requires "arbitration
 * yields exactly 1 winner" with the engine arena reused unchanged. At the JS
 * level this test pins single-winner consumption: one tapId presented to two
 * handles fires exactly once in total (consume-before-notify is global to the
 * tap). If the coder loci arbitration elsewhere, update the call site — not
 * the exactly-1 behavior.
 * ASSUMPTION EN-1 (delivery-time veto path): AC-M3-036 requires the
 * false-at-delivery veto, so `enabled` must be re-checkable at delivery.
 * These tests use `handle.setEnabled(bool)` as that mutation path. If the
 * coder exposes it differently (live binding, re-created handle), update
 * the call site — not the veto behavior.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
// RED today: the canonical delivery-lane copy does not exist yet (plan T7).
const { createButton, foldButtonFrames } = require('../button.js');

function makeButton(onPress, extra = {}) {
  return createButton({ enabled: true, onPress, ...extra });
}

// ---------------------------------------------------------------------------
// Combined press() shape: fires at most once per tapId
// ---------------------------------------------------------------------------

test('TC-M3-037: press() returns the combined shape {fired,frames:1,reinstantiated:0}', () => {
  let calls = 0;
  const button = makeButton(() => { calls += 1; });
  const result = button.press(button.mint());
  assert.deepEqual(
    { fired: result.fired, frames: result.frames, reinstantiated: result.reinstantiated },
    { fired: 1, frames: 1, reinstantiated: 0 },
    'every press result carries frames:1/reinstantiated:0',
  );
  assert.equal(calls, 1);
});

test('TC-M3-037: 100x replay of one lease fires exactly once in total', () => {
  let calls = 0;
  const button = makeButton(() => { calls += 1; });
  const lease = button.mint();
  let total = 0;
  for (let i = 0; i < 100; i += 1) {
    const r = button.press(lease);
    total += r.fired;
    if (i > 0) assert.equal(r.fired, 0, `replay #${i} must fire nothing`);
    assert.equal(r.frames, 1);
    assert.equal(r.reinstantiated, 0);
  }
  assert.equal(total, 1, 'exactly 1 fire per 100x replay');
  assert.equal(calls, 1);
});

test('TC-M3-037: disabled-never-mint — mint() returns null when disabled', () => {
  const button = createButton({ enabled: false, onPress: () => {} });
  assert.equal(button.mint(), null, 'a disabled button mints no lease');
});

test('TC-M3-037: false-at-delivery — disabling before press vetoes the fire', () => {
  let calls = 0;
  const button = makeButton(() => { calls += 1; });
  const lease = button.mint();
  button.setEnabled(false);
  const result = button.press(lease);
  assert.equal(result.fired, 0, 'a tap delivered while disabled must fire nothing');
  assert.equal(calls, 0);
});

test('TC-M3-037: drag/slop/CANCEL revoke the lease — press fires nothing', () => {
  for (const reason of ['drag', 'slop', 'cancel']) {
    let calls = 0;
    const button = makeButton(() => { calls += 1; });
    const lease = button.mint();
    lease.revoked = reason;
    const result = button.press(lease);
    assert.equal(result.fired, 0, `a ${reason}-revoked lease must fire nothing`);
    assert.equal(calls, 0, `a ${reason}-revoked lease must not notify`);
  }
});

test('TC-M3-037: a forged lease is rejected with an honest TypeError and fires nothing', () => {
  let calls = 0;
  const button = makeButton(() => { calls += 1; });
  assert.throws(
    () => button.press({ tapId: 'forged-not-real' }),
    TypeError,
    'forgery must reject with an honest TypeError (forgery by design is forbidden)',
  );
  assert.equal(calls, 0, 'a forged lease must never notify');
});

test('TC-M3-037: consume-before-notify — a reentrant press of the same lease fires once', () => {
  let calls = 0;
  let button;
  button = makeButton(() => {
    calls += 1;
    const again = button.press(sameLease);
    assert.equal(again.fired, 0, 'reentrant press of a consumed lease must fire nothing');
  });
  const sameLease = button.mint();
  const result = button.press(sameLease);
  assert.equal(result.fired, 1);
  assert.equal(calls, 1, 'exactly one notification despite reentrancy');
});

test('TC-M3-037: throwing listener is recorded without escape (fired:1 + recorded)', () => {
  const button = makeButton(() => { throw new Error('boom-not-real'); });
  let result;
  assert.doesNotThrow(() => { result = button.press(button.mint()); });
  assert.equal(result.fired, 1, 'the tap still counts as fired exactly once');
  assert.ok(
    Array.isArray(result.errors) && result.errors.length === 1,
    'the thrown listener error must be honestly recorded on the result (ASSUMPTION REC-1)',
  );
});

// ---------------------------------------------------------------------------
// foldButtonFrames <=> press() parity, no `pressed` prop, single winner
// ---------------------------------------------------------------------------

test('TC-M3-037: foldButtonFrames agrees with press() on the same taps (parity)', () => {
  let calls = 0;
  const button = makeButton(() => { calls += 1; });
  const frames = [];
  let expected = 0;
  for (let i = 0; i < 300; i += 1) {
    const tapped = i % 3 !== 0;
    frames.push({ tap: tapped, cancel: false, drag: false });
    if (tapped) {
      const r = button.press(button.mint());
      expected += r.fired;
    }
  }
  const folded = foldButtonFrames(frames);
  assert.ok(folded === 0 || folded === 1, 'fold codomain is exactly 0|1');
  assert.equal(calls, expected, 'press fires exactly once per minted tap in the parity stream');
  assert.equal(folded, expected > 0 ? 1 : 0, 'fold agrees with press on the parity stream');
});

test('TC-M3-037: the handle exposes no `pressed` prop (minimal {enabled,onPress} surface)', () => {
  const button = createButton({ enabled: true, onPress: () => {}, pressed: true });
  assert.equal(button.pressed, undefined, 'no `pressed` prop may be exposed');
  assert.ok(!Object.keys(button).includes('pressed'), 'no `pressed` key on the handle');
  const result = button.press(button.mint());
  assert.equal(result.fired, 1, 'a stray `pressed` input must not disturb the tap path');
});

test('TC-M3-037: arbitration — one tapId presented to two handles fires exactly once', () => {
  let aCalls = 0;
  let bCalls = 0;
  const a = makeButton(() => { aCalls += 1; });
  const b = makeButton(() => { bCalls += 1; });
  const lease = a.mint();
  const ra = a.press(lease);
  const rb = b.press(lease);
  assert.equal(
    ra.fired + rb.fired,
    1,
    'arbitration yields exactly 1 winner for one tap (ASSUMPTION ARB-1)',
  );
  assert.equal(aCalls + bCalls, 1);
});
