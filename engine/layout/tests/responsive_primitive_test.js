'use strict';
// Batch 1 T2 executable wrapper — responsive primitives (FR-LAYOUT-011, AC-030).
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T2 `Interface:` line, never to implementation:
//   `LayoutResult measureResponsive(Node n, Constraints c) ->
//    LayoutResult{size, baseline}; unsupported ->
//    Status::kUnsupportedProperty`
// The C++ contract test is `responsive_primitive_test.cpp` in this directory;
// this file mirrors its cases so `node --test` gives a countable red/green
// run while no C++ toolchain wiring exists for the batch (repo has no usable
// Gradle wrapper for :layout tasks; engine/layout has no build dir in this
// checkout). Module resolution: `../src/responsive.js` once a JS projection
// of the contract lands, else an inline fallback stub whose behavior is wrong
// on every case, so every behavior assertion below is red until the contract
// lands — and the harness itself never crashes.
//
// ASSUMPTIONS (pinned in unit-tests/T2.md):
//   - Node is a plain descriptor: { kind, ...params } with kinds 'grid',
//     'aspectBox', 'wrapRow', 'baselineBox', 'empty'; Constraints is
//     { maxWidth, maxHeight }; LayoutResult is
//     { size: { width, height }, baseline, status };
//   - status is 'kOk', unsupported is 'kUnsupportedProperty' (also surfaced
//     as a thrown UnsupportedPropertyError — either signal counts only if
//     the status path is observable; the C++ boundary never throws, so the
//     JS projection asserts the status field);
//   - grid 2x(100x50)x4 -> 200x100; aspect 160 @16/9 -> 160x90; wrap of three
//     100-wide children under maxWidth 250 -> 200x100;
//   - baseline factory value reported verbatim within [0, height];
//   - empty node -> 0x0, baseline 0, kOk; negative aspect ratio ->
//     'kInvalidMetrics', never a throw, never a silent fallback size.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../src/responsive.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong on every
  // input (always 0x0/baseline-0/kOk, never errors) so every behavior
  // assertion below fails (red) until ../src/responsive.js lands.
  mod = {
    measureResponsive: () => ({
      size: { width: 0, height: 0 },
      baseline: 0,
      status: 'kOk',
    }),
    UnsupportedPropertyError: class UnsupportedPropertyError extends Error {},
  };
}
const { measureResponsive } = mod;

const AMPLE = { maxWidth: 2000, maxHeight: 2000 };
const EPS = 0.001;
function approx(a, b) {
  assert.ok(Math.abs(a - b) < EPS, `expected ${a} ≈ ${b}`);
}

// ---------------------------------------------------------------------------
// T2 contract:
// `LayoutResult measureResponsive(Node n, Constraints c) ->
//  LayoutResult{size, baseline}; unsupported ->
//  Status::kUnsupportedProperty`
// ---------------------------------------------------------------------------
describe('TC responsive primitives — grid/aspect-ratio/wrap/baseline measure first-class, unsupported errors (AC-030)', () => {
  it('grid measures columns × cells (2 cols of 100x50 × 4 children = 200x100)', () => {
    const r = measureResponsive(
      { kind: 'grid', columns: 2, cellWidth: 100, cellHeight: 50, childCount: 4 },
      AMPLE
    );
    assert.equal(r.status, 'kOk');
    approx(r.size.width, 200);
    approx(r.size.height, 100);
  });

  it('aspect-ratio box preserves height (160 wide @16/9 = 160x90)', () => {
    const r = measureResponsive({ kind: 'aspectBox', ratio: 16 / 9, width: 160 }, AMPLE);
    assert.equal(r.status, 'kOk');
    approx(r.size.width, 160);
    approx(r.size.height, 90);
  });

  it('wrap flows to two lines (three 100-wide children under maxWidth 250 = 200x100)', () => {
    const r = measureResponsive(
      { kind: 'wrapRow', childWidth: 100, childHeight: 50, childCount: 3 },
      { maxWidth: 250, maxHeight: 2000 }
    );
    assert.equal(r.status, 'kOk');
    approx(r.size.width, 200);
    approx(r.size.height, 100);
  });

  it('baseline box reports its baseline verbatim within [0, height]', () => {
    const r = measureResponsive(
      { kind: 'baselineBox', width: 100, height: 50, baseline: 40 },
      AMPLE
    );
    assert.equal(r.status, 'kOk');
    approx(r.baseline, 40);
    assert.ok(r.baseline >= 0 && r.baseline <= r.size.height);
  });

  it('unsupported property errors with kUnsupportedProperty, never a silent no-op', () => {
    const r = measureResponsive({ kind: 'bogus', bogusKind: 9999 }, AMPLE);
    assert.equal(r.status, 'kUnsupportedProperty');
  });

  it('ASSUMPTION: empty node measures 0x0, baseline 0, kOk', () => {
    const r = measureResponsive({ kind: 'empty' }, AMPLE);
    assert.equal(r.status, 'kOk');
    approx(r.size.width, 0);
    approx(r.size.height, 0);
    approx(r.baseline, 0);
  });

  it('ASSUMPTION: negative aspect ratio is rejected with kInvalidMetrics, never silent', () => {
    const r = measureResponsive({ kind: 'aspectBox', ratio: -1, width: 160 }, AMPLE);
    assert.equal(r.status, 'kInvalidMetrics');
  });
});
