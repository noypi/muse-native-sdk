'use strict';
// Batch 3 T4+T5 executable mirror — fold/posture trigger + in-place resize
// (FR-LAYOUT-011 AC-028 trigger surface, FR-LAYOUT-004 AC-LAYOUT-007).
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no env/config reads, no filesystem, no live services, no imports
// from src/ — the contract logic is ported into this file itself. Shared file
// owned by T4 per GR-08 T1.8, mapped in both T4.md and T5.md contracts.
// Binds to the plan `Interface:` lines (verbatim in contracts):
//   T4: `void onWindowLayoutInfo(WindowLayoutInfo info) -> Status;
//        unparseable info -> Status::kIgnoredStaleInfo, class unchanged`
//   T5: `Status onWindowBoundsChanged(WindowMetrics m, DisplayMetrics d) ->
//        ok | kInvalidMetrics; recreation requested ->
//        Status::kRecreationNotAllowed`
// The C++ gate is `window_resize_test.cpp` in this directory; this file
// executes the SAME case list with the SAME pinned values (see check_* names).
//
// ASSUMPTIONS (pinned in unit-tests/T4.md and T5.md):
//   - WindowLayoutInfo is { metrics, valid }; valid=false, or non-positive /
//     non-finite metrics, counts as unparseable -> 'kIgnoredStaleInfo';
//   - DisplayMetrics is { density, displayId }; non-positive/non-finite density
//     is invalid -> 'kInvalidMetrics';
//   - observers: currentSizeClass() (last recomputed class) and
//     recreationRequestCount() (performed recreations, always 0 here);
//   - requestRecreation() returns 'kRecreationNotAllowed' and performs nothing;
//   - thresholds pinned per plan A2 (compactMaxWidth=600, mediumMaxWidth=840);
//     tie stays in the lower tier (shared with T1).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

// --- Inline port of the T4/T5 contract logic (mirrors window_resize_test.cpp) ---
const BREAKPOINTS = { compactMaxWidth: 600, mediumMaxWidth: 840 };

function classifyWidth(w) {
  if (w <= 600) return 'compact';
  if (w <= 840) return 'medium';
  return 'expanded';
}

function validMetrics(m) {
  return Number.isFinite(m.width) && Number.isFinite(m.height) && m.width > 0 && m.height > 0;
}

let currentWidthClass = 'unknown';
let recreations = 0;

function onWindowLayoutInfo(info) {
  if (!info.valid) return 'kIgnoredStaleInfo';
  if (!validMetrics(info.metrics)) return 'kIgnoredStaleInfo';
  currentWidthClass = classifyWidth(info.metrics.width);
  return 'kOk';
}

function onWindowBoundsChanged(m, d) {
  if (!validMetrics(m)) return 'kInvalidMetrics';
  if (!Number.isFinite(d.density) || d.density <= 0) return 'kInvalidMetrics';
  currentWidthClass = classifyWidth(m.width);
  return 'kOk';
}

function currentSizeClass() {
  return { widthClass: currentWidthClass };
}

function recreationRequestCount() {
  return recreations;
}

function requestRecreation() {
  return 'kRecreationNotAllowed';
}

function metrics(w, h) {
  return { width: w, height: h };
}

function info(w, h, valid = true) {
  return { metrics: metrics(w, h), valid };
}

function display(density = 2.0, id = 0) {
  return { density, displayId: id };
}

// ---------------------------------------------------------------------------
// T4: fold/posture trigger surface (FR-LAYOUT-011 AC-028)
// ---------------------------------------------------------------------------
describe('TC window layout info — fold notification recomputes class, stale info ignored (T4)', () => {
  it('check_valid_layout_info_recomputes_class', () => {
    const recreationsBefore = recreationRequestCount();
    assert.equal(onWindowLayoutInfo(info(360, 640)), 'kOk');
    assert.equal(currentSizeClass().widthClass, 'compact');
    assert.equal(onWindowLayoutInfo(info(1200, 800)), 'kOk');
    assert.equal(currentSizeClass().widthClass, 'expanded');
    assert.equal(recreationRequestCount(), recreationsBefore);
  });

  it('check_fold_sequence_retriggers_without_recreation', () => {
    const recreationsBefore = recreationRequestCount();
    onWindowLayoutInfo(info(360, 640));
    onWindowLayoutInfo(info(1100, 700));
    const unfoldedExpanded = currentSizeClass().widthClass === 'expanded';
    onWindowLayoutInfo(info(360, 640));
    assert.equal(unfoldedExpanded, true);
    assert.equal(currentSizeClass().widthClass, 'compact');
    assert.equal(recreationRequestCount(), recreationsBefore);
  });

  it('check_stale_info_ignored_class_unchanged', () => {
    onWindowLayoutInfo(info(700, 640)); // Medium baseline
    assert.equal(currentSizeClass().widthClass, 'medium');
    assert.equal(onWindowLayoutInfo(info(1200, 800, false)), 'kIgnoredStaleInfo');
    assert.equal(currentSizeClass().widthClass, 'medium');
  });

  it('check_garbage_metrics_info_ignored', () => {
    onWindowLayoutInfo(info(700, 640)); // Medium baseline
    assert.equal(onWindowLayoutInfo(info(0, 0)), 'kIgnoredStaleInfo');
    assert.equal(currentSizeClass().widthClass, 'medium');
    assert.equal(onWindowLayoutInfo(info(-50, 640)), 'kIgnoredStaleInfo');
  });

  it('check_boundary_info_triggers_boundary_class', () => {
    assert.equal(onWindowLayoutInfo(info(600, 640)), 'kOk');
    assert.equal(currentSizeClass().widthClass, 'compact');
  });
});

// ---------------------------------------------------------------------------
// T5: in-place resize path (FR-LAYOUT-004 AC-LAYOUT-007)
// ---------------------------------------------------------------------------
describe('TC window bounds changed — continuous resize reflows in place, recreation refused (T5)', () => {
  it('check_continuous_resize_reflows_in_place', () => {
    const recreationsBefore = recreationRequestCount();
    for (const w of [360, 500, 700, 900, 1200]) {
      assert.equal(onWindowBoundsChanged(metrics(w, 700), display()), 'kOk');
    }
    assert.equal(currentSizeClass().widthClass, 'expanded');
    assert.equal(recreationRequestCount(), recreationsBefore);
  });

  it('check_cross_display_move_relayouts_with_new_density', () => {
    const recreationsBefore = recreationRequestCount();
    assert.equal(onWindowBoundsChanged(metrics(700, 640), display(2.0, 0)), 'kOk');
    assert.equal(onWindowBoundsChanged(metrics(700, 640), display(3.0, 1)), 'kOk');
    assert.equal(currentSizeClass().widthClass, 'medium');
    assert.equal(recreationRequestCount(), recreationsBefore);
  });

  it('check_zero_bounds_yield_invalid', () => {
    assert.equal(onWindowBoundsChanged(metrics(0, 0), display()), 'kInvalidMetrics');
  });

  it('check_negative_bounds_yield_invalid', () => {
    assert.equal(onWindowBoundsChanged(metrics(-10, 640), display()), 'kInvalidMetrics');
  });

  it('check_invalid_display_density_yield_invalid', () => {
    assert.equal(onWindowBoundsChanged(metrics(700, 640), display(0, 0)), 'kInvalidMetrics');
  });

  it('check_recreation_request_refused', () => {
    onWindowBoundsChanged(metrics(700, 640), display());
    const recreationsBefore = recreationRequestCount();
    assert.equal(requestRecreation(), 'kRecreationNotAllowed');
    assert.equal(recreationRequestCount(), recreationsBefore);
    assert.equal(currentSizeClass().widthClass, 'medium');
  });
});
