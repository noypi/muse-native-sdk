'use strict';
// Batch 1 T1 executable wrapper — window size classes (FR-LAYOUT-011, AC-028).
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T1 `Interface:` line, never to implementation:
//   `SizeClass computeSizeClass(WindowMetrics m, Breakpoints b) ->
//    SizeClass{widthClass,heightClass}; invalid metrics ->
//    WidthClass::Unknown + Status::kInvalidMetrics`
// The C++ contract test is `size_class_test.cpp` in this directory; this file
// mirrors its cases so `node --test` gives a countable red/green run while no
// C++ toolchain wiring exists for the batch (repo has no usable Gradle
// wrapper for :layout tasks; engine/layout has no build dir in this
// checkout). Module resolution: `../src/size_class.js` once a JS projection
// of the contract lands, else an inline fallback stub whose behavior is
// wrong on every case, so every behavior assertion below is red until the
// contract lands — and the harness itself never crashes.
//
// ASSUMPTIONS (pinned in unit-tests/T1.md):
//   - WindowMetrics is { width, height } in dp; Breakpoints is
//     { compactMaxWidth, mediumMaxWidth, compactMaxHeight, mediumMaxHeight }
//     injected per call (plan A2), pinned here to 600/840/480/900;
//   - classes are the strings 'compact' | 'medium' | 'expanded', invalid is
//     widthClass 'unknown' with status 'kInvalidMetrics' (ok is 'kOk');
//   - a metric exactly equal to a tier max stays in the lower tier;
//   - recompute is stateless: no cached Activity state across calls.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../src/size_class.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong on every
  // input (always 'none'/kOk, never a real class, never invalid) so every
  // behavior assertion below fails (red) until ../src/size_class.js lands.
  mod = {
    computeSizeClass: () => ({
      size: { widthClass: 'none', heightClass: 'none' },
      status: 'kOk',
    }),
  };
}
const { computeSizeClass } = mod;

const BREAKPOINTS = {
  compactMaxWidth: 600,
  mediumMaxWidth: 840,
  compactMaxHeight: 480,
  mediumMaxHeight: 900,
};

// ---------------------------------------------------------------------------
// T1 contract:
// `SizeClass computeSizeClass(WindowMetrics m, Breakpoints b) ->
//  SizeClass{widthClass,heightClass}; invalid metrics ->
//  WidthClass::Unknown + Status::kInvalidMetrics`
// ---------------------------------------------------------------------------
describe('TC size classes — computeSizeClass derives compact/medium/expanded from injected breakpoints (AC-028)', () => {
  it('classifies compact metrics as compact/compact', () => {
    const r = computeSizeClass({ width: 360, height: 400 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.widthClass, 'compact');
    assert.equal(r.size.heightClass, 'compact');
  });

  it('classifies medium widths as medium', () => {
    const r = computeSizeClass({ width: 700, height: 640 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.widthClass, 'medium');
  });

  it('classifies expanded widths as expanded', () => {
    const r = computeSizeClass({ width: 1200, height: 800 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.widthClass, 'expanded');
    assert.equal(r.size.heightClass, 'medium');
  });

  it('classifies expanded heights as expanded', () => {
    const r = computeSizeClass({ width: 360, height: 1000 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.heightClass, 'expanded');
  });

  it('recomputes on resize with no cached Activity state', () => {
    const before = computeSizeClass({ width: 360, height: 640 }, BREAKPOINTS);
    const after = computeSizeClass({ width: 1200, height: 800 }, BREAKPOINTS);
    assert.equal(before.size.widthClass, 'compact');
    assert.equal(after.size.widthClass, 'expanded');
    assert.equal(after.status, 'kOk');
  });

  it('fold/unfold sequence recomputes with no stale cache', () => {
    const folded = computeSizeClass({ width: 360, height: 640 }, BREAKPOINTS);
    const unfolded = computeSizeClass({ width: 1100, height: 700 }, BREAKPOINTS);
    const refolded = computeSizeClass({ width: 360, height: 640 }, BREAKPOINTS);
    assert.equal(folded.size.widthClass, 'compact');
    assert.equal(unfolded.size.widthClass, 'expanded');
    assert.equal(refolded.size.widthClass, 'compact');
  });

  it('zero metrics yield Unknown + kInvalidMetrics, never a default class', () => {
    const r = computeSizeClass({ width: 0, height: 0 }, BREAKPOINTS);
    assert.equal(r.status, 'kInvalidMetrics');
    assert.equal(r.size.widthClass, 'unknown');
  });

  it('negative metrics yield kInvalidMetrics', () => {
    const r = computeSizeClass({ width: -10, height: 640 }, BREAKPOINTS);
    assert.equal(r.status, 'kInvalidMetrics');
    assert.equal(r.size.widthClass, 'unknown');
  });

  it('zero height is invalid even when width is valid', () => {
    const r = computeSizeClass({ width: 360, height: 0 }, BREAKPOINTS);
    assert.equal(r.status, 'kInvalidMetrics');
  });

  it('ASSUMPTION: metric exactly at compactMax stays compact', () => {
    const r = computeSizeClass({ width: 600, height: 640 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.widthClass, 'compact');
  });

  it('ASSUMPTION: metric exactly at mediumMax stays medium', () => {
    const r = computeSizeClass({ width: 840, height: 640 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.widthClass, 'medium');
  });
});
