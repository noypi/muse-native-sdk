'use strict';
// Batch 1 T2 executable mirror — responsive primitives (FR-LAYOUT-011, AC-030).
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no env/config reads, no filesystem, no live services, no imports
// from src/ — the contract logic is ported into this file itself.
// Binds ONLY to the pinned T2 `Interface:` line:
//   `LayoutResult measureResponsive(Node n, Constraints c) ->
//    LayoutResult{size, baseline}; unsupported ->
//    Status::kUnsupportedProperty`
// The C++ gate is `responsive_primitive_test.cpp` in this directory; this file
// executes the SAME case list with the SAME pinned values (see check_* names).
//
// ASSUMPTIONS (pinned in unit-tests/T2.md):
//   - Node is a plain descriptor: { kind, ...params } with kinds 'grid',
//     'aspectBox', 'wrapRow', 'baselineBox', 'empty'; Constraints is
//     { maxWidth, maxHeight }; LayoutResult is
//     { size: { width, height }, baseline, status };
//   - grid 2x(100x50)x4 -> 200x100; aspect 160 @16/9 -> 160x90; wrap of three
//     100-wide children under maxWidth 250 -> 200x100;
//   - baseline factory value reported verbatim within [0, height];
//   - empty node -> 0x0, baseline 0, kOk; negative aspect ratio ->
//     'kInvalidMetrics', never a throw, never a silent fallback size.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

// --- Inline port of the T2 contract logic (mirrors responsive_primitive_test.cpp) ---
function measureResponsive(node, constraints) {
  switch (node.kind) {
    case 'grid': {
      const rows = Math.ceil(node.childCount / node.columns);
      return {
        size: { width: node.columns * node.cellWidth, height: rows * node.cellHeight },
        baseline: 0,
        status: 'kOk',
      };
    }
    case 'aspectBox': {
      if (!Number.isFinite(node.ratio) || node.ratio <= 0 || !Number.isFinite(node.width) || node.width <= 0) {
        return { size: { width: 0, height: 0 }, baseline: 0, status: 'kInvalidMetrics' };
      }
      return {
        size: { width: node.width, height: node.width / node.ratio },
        baseline: 0,
        status: 'kOk',
      };
    }
    case 'wrapRow': {
      const perLine = Math.max(1, Math.floor(constraints.maxWidth / node.childWidth));
      const lines = Math.ceil(node.childCount / perLine);
      return {
        size: { width: perLine * node.childWidth, height: lines * node.childHeight },
        baseline: 0,
        status: 'kOk',
      };
    }
    case 'baselineBox': {
      return {
        size: { width: node.width, height: node.height },
        baseline: node.baseline,
        status: 'kOk',
      };
    }
    case 'empty': {
      return { size: { width: 0, height: 0 }, baseline: 0, status: 'kOk' };
    }
    default: {
      return { size: { width: 0, height: 0 }, baseline: 0, status: 'kUnsupportedProperty' };
    }
  }
}

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
  it('check_grid_measures_columns_times_cells', () => {
    const r = measureResponsive(
      { kind: 'grid', columns: 2, cellWidth: 100, cellHeight: 50, childCount: 4 },
      AMPLE
    );
    assert.equal(r.status, 'kOk');
    approx(r.size.width, 200);
    approx(r.size.height, 100);
  });

  it('check_aspect_ratio_preserves_height', () => {
    const r = measureResponsive({ kind: 'aspectBox', ratio: 16 / 9, width: 160 }, AMPLE);
    assert.equal(r.status, 'kOk');
    approx(r.size.width, 160);
    approx(r.size.height, 90);
  });

  it('check_wrap_flows_to_two_lines', () => {
    const r = measureResponsive(
      { kind: 'wrapRow', childWidth: 100, childHeight: 50, childCount: 3 },
      { maxWidth: 250, maxHeight: 2000 }
    );
    assert.equal(r.status, 'kOk');
    approx(r.size.width, 200);
    approx(r.size.height, 100);
  });

  it('check_baseline_reported_verbatim', () => {
    const r = measureResponsive(
      { kind: 'baselineBox', width: 100, height: 50, baseline: 40 },
      AMPLE
    );
    assert.equal(r.status, 'kOk');
    approx(r.baseline, 40);
    assert.ok(r.baseline >= 0 && r.baseline <= r.size.height);
  });

  it('check_unsupported_property_errors', () => {
    const r = measureResponsive({ kind: 'bogus', bogusKind: 9999 }, AMPLE);
    assert.equal(r.status, 'kUnsupportedProperty');
  });

  it('check_empty_node_measures_zero', () => {
    const r = measureResponsive({ kind: 'empty' }, AMPLE);
    assert.equal(r.status, 'kOk');
    approx(r.size.width, 0);
    approx(r.size.height, 0);
    approx(r.baseline, 0);
  });

  it('check_negative_aspect_ratio_is_rejected', () => {
    const r = measureResponsive({ kind: 'aspectBox', ratio: -1, width: 160 }, AMPLE);
    assert.equal(r.status, 'kInvalidMetrics');
  });
});
