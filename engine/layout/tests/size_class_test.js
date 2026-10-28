'use strict';
// Batch 1 T1 executable mirror — window size classes (FR-LAYOUT-011, AC-028).
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no env/config reads, no filesystem, no live services, no imports
// from src/ — the contract logic is ported into this file itself.
// Binds ONLY to the pinned T1 `Interface:` line:
//   `SizeClass computeSizeClass(WindowMetrics m, Breakpoints b) ->
//    SizeClass{widthClass,heightClass}; invalid metrics ->
//    WidthClass::Unknown + Status::kInvalidMetrics`
// The C++ gate is `size_class_test.cpp` in this directory; this file executes
// the SAME case list with the SAME pinned values (see check_* names below).
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

// --- Inline port of the T1 contract logic (mirrors size_class_test.cpp) ---
function classifyTier(value, compactMax, mediumMax) {
  if (value <= compactMax) return 'compact';
  if (value <= mediumMax) return 'medium';
  return 'expanded';
}

function computeSizeClass(m, b) {
  if (
    !Number.isFinite(m.width) ||
    !Number.isFinite(m.height) ||
    m.width <= 0 ||
    m.height <= 0
  ) {
    return {
      size: { widthClass: 'unknown', heightClass: 'unknown' },
      status: 'kInvalidMetrics',
    };
  }
  return {
    size: {
      widthClass: classifyTier(m.width, b.compactMaxWidth, b.mediumMaxWidth),
      heightClass: classifyTier(m.height, b.compactMaxHeight, b.mediumMaxHeight),
    },
    status: 'kOk',
  };
}

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
  it('check_compact_metrics_yield_compact_compact', () => {
    const r = computeSizeClass({ width: 360, height: 400 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.widthClass, 'compact');
    assert.equal(r.size.heightClass, 'compact');
  });

  it('check_medium_width_yields_medium', () => {
    const r = computeSizeClass({ width: 700, height: 640 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.widthClass, 'medium');
  });

  it('check_expanded_width_yields_expanded', () => {
    const r = computeSizeClass({ width: 1200, height: 800 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.widthClass, 'expanded');
    assert.equal(r.size.heightClass, 'medium');
  });

  it('check_expanded_height_yields_expanded', () => {
    const r = computeSizeClass({ width: 360, height: 1000 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.heightClass, 'expanded');
  });

  it('check_recompute_on_resize_returns_new_class', () => {
    const before = computeSizeClass({ width: 360, height: 640 }, BREAKPOINTS);
    const after = computeSizeClass({ width: 1200, height: 800 }, BREAKPOINTS);
    assert.equal(before.size.widthClass, 'compact');
    assert.equal(after.size.widthClass, 'expanded');
    assert.equal(after.status, 'kOk');
  });

  it('check_fold_sequence_has_no_stale_cache', () => {
    const folded = computeSizeClass({ width: 360, height: 640 }, BREAKPOINTS);
    const unfolded = computeSizeClass({ width: 1100, height: 700 }, BREAKPOINTS);
    const refolded = computeSizeClass({ width: 360, height: 640 }, BREAKPOINTS);
    assert.equal(folded.size.widthClass, 'compact');
    assert.equal(unfolded.size.widthClass, 'expanded');
    assert.equal(refolded.size.widthClass, 'compact');
  });

  it('check_zero_metrics_yield_unknown_invalid', () => {
    const r = computeSizeClass({ width: 0, height: 0 }, BREAKPOINTS);
    assert.equal(r.status, 'kInvalidMetrics');
    assert.equal(r.size.widthClass, 'unknown');
  });

  it('check_negative_metrics_yield_invalid', () => {
    const r = computeSizeClass({ width: -10, height: 640 }, BREAKPOINTS);
    assert.equal(r.status, 'kInvalidMetrics');
    assert.equal(r.size.widthClass, 'unknown');
  });

  it('check_zero_height_yields_invalid', () => {
    const r = computeSizeClass({ width: 360, height: 0 }, BREAKPOINTS);
    assert.equal(r.status, 'kInvalidMetrics');
  });

  it('check_compact_max_boundary_stays_compact', () => {
    const r = computeSizeClass({ width: 600, height: 640 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.widthClass, 'compact');
  });

  it('check_medium_max_boundary_stays_medium', () => {
    const r = computeSizeClass({ width: 840, height: 640 }, BREAKPOINTS);
    assert.equal(r.status, 'kOk');
    assert.equal(r.size.widthClass, 'medium');
  });
});
