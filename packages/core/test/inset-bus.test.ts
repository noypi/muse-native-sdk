'use strict';
// T3 — Inset Vector Bus delivers typed vector data (Batch 2, FR-STATE-001 AC-STATE-008).
// Plan Interface (verbatim):
// `publishInsets(insets:{top:number,left:number,bottom:number,right:number}): void`
// — validates all four fields finite numbers else throws
// `TypeError("MUSE_STATE_BAD_INSETS")`;
// `subscribeInsets(cb): ()=>void`.
// Acceptance: inset-only publish notifies only inset subscribers
// (product-state untouched); payload deep-equals; bad payload throws before
// notification; unsubscribe stops delivery.
// Spec refs: FR-STATE-001, AC-STATE-008.
// Read-only reference (NOT modified): `engine/runtime/src/ime_inset.cpp` (+
// `engine/runtime/include/runtime/ime_inset.h`, `engine/runtime/tests/
// ime_inset_test.cpp`). Conformance note: the current C++ seam exposes a
// single `float ime_inset_height` (+ frame ids), not top/left/bottom/right;
// this test pins the JS bus payload shape per the plan Interface line
// ({top,left,bottom,right} finite numbers) and does NOT assert a C++ field
// the reference does not parse. The coder must keep the JS key names/types
// stable so a future C++ consumer can parse them.
// Hermetic: node:test + node:assert/strict only; no network, no secrets, no
// config, no live services. Inset bus wraps `state()` from lib/state.js —
// product-state isolation is asserted via a plain `state()` atom that must
// stay untouched (no scheduler spy needed; flush() drives delivery).

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const { publishInsets, subscribeInsets } = require('../lib/inset_bus.js');
const { state, getRefRecord } = require('../lib/state.js');
const scheduler = require('../lib/scheduler.js');

beforeEach(() => {
  scheduler.flush();
});

describe('T3 — publishInsets validates and notifies inset subscribers (AC-STATE-008)', () => {
  it('delivers a payload that deep-equals {top,left,bottom,right} numbers', () => {
    const seen = [];
    const unsub = subscribeInsets((v) => { seen.push(v); });
    try {
      publishInsets({ top: 10, left: 0, bottom: 24, right: 0 });
      scheduler.flush();
    } finally {
      unsub();
    }
    assert.equal(seen.length, 1);
    assert.deepEqual(seen[0], { top: 10, left: 0, bottom: 24, right: 0 });
    for (const k of ['top', 'left', 'bottom', 'right']) {
      assert.equal(typeof seen[0][k], 'number');
      assert.ok(Number.isFinite(seen[0][k]));
    }
  });

  it('payload key names are exactly top/left/bottom/right (C++ parse conformance)', () => {
    const seen = [];
    const unsub = subscribeInsets((v) => { seen.push(v); });
    try {
      publishInsets({ top: 1, left: 2, bottom: 3, right: 4 });
      scheduler.flush();
    } finally {
      unsub();
    }
    assert.deepEqual(Object.keys(seen[0]).sort(), ['bottom', 'left', 'right', 'top']);
  });

  it('inset-only publish does not touch product state', () => {
    let productNotifications = 0;
    const product = state({ widget: 'home' });
    const record = getRefRecord(product);
    record.subs.add(() => { productNotifications += 1; });
    const seen = [];
    const unsub = subscribeInsets((v) => { seen.push(v); });
    try {
      publishInsets({ top: 5, left: 5, bottom: 5, right: 5 });
      scheduler.flush();
    } finally {
      unsub();
    }
    assert.equal(seen.length, 1);
    assert.equal(productNotifications, 0);
    assert.deepEqual(product.value, { widget: 'home' });
  });

  it('bad payload throws TypeError("MUSE_STATE_BAD_INSETS") before any notification', () => {
    const bad = [
      { top: NaN, left: 0, bottom: 0, right: 0 },
      { top: Infinity, left: 0, bottom: 0, right: 0 },
      { top: '10', left: 0, bottom: 0, right: 0 },
      { top: 1, left: 2, bottom: 3 }, // missing right
      { top: 1, left: 2, bottom: 3, right: undefined },
      null,
      undefined,
      42,
      'insets',
    ];
    for (const payload of bad) {
      let notified = 0;
      const unsub = subscribeInsets(() => { notified += 1; });
      try {
        assert.throws(() => publishInsets(payload), (err) => (
          err instanceof TypeError && /MUSE_STATE_BAD_INSETS/.test(err.message)
        ));
      } finally {
        unsub();
      }
      scheduler.flush();
      assert.equal(notified, 0);
    }
  });

  it('unsubscribe stops delivery', () => {
    const seen = [];
    const unsub = subscribeInsets((v) => { seen.push(v); });
    publishInsets({ top: 1, left: 1, bottom: 1, right: 1 });
    scheduler.flush();
    assert.equal(seen.length, 1);
    unsub();
    publishInsets({ top: 2, left: 2, bottom: 2, right: 2 });
    scheduler.flush();
    assert.equal(seen.length, 1);
  });

  it('multiple subscribers each receive the publish once', () => {
    let a = 0;
    let b = 0;
    const unA = subscribeInsets(() => { a += 1; });
    const unB = subscribeInsets(() => { b += 1; });
    try {
      publishInsets({ top: 0, left: 0, bottom: 0, right: 0 });
      scheduler.flush();
    } finally {
      unA();
      unB();
    }
    assert.equal(a, 1);
    assert.equal(b, 1);
  });
});
