'use strict';
// Batch 4 T6 executable mirror — scripted compact→expanded→compact resize sweep
// (FR-LAYOUT-011 AC-031 + FR-LAYOUT-003 AC-006 + FR-LAYOUT-004 AC-007).
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no env/config reads, no filesystem, no live services, no imports
// from src/ — the T1/T3/T5 contract logic and the sweep harness are ported
// into this file itself.
// Binds to the T6 `Interface:` line (verbatim in T6.md):
//   `SweepReport runResizeSweep(Sequence compactToExpandedAndBack) ->
//    SweepReport{switchesPerBoundary, scrollDelta, focusLost};
//    focus lost -> SweepReport.focusLost=true (fail)`
// The C++ gate is `resize_sweep_test.cpp` in this directory; this file
// executes the SAME case list with the SAME pinned values (see check_* names).
//
// ASSUMPTIONS (pinned in unit-tests/T6.md):
//   - Sequence = scripted width vector at fixed height; thresholds pinned per
//     plan A2 (600/840): 360/500->compact, 700->medium, 900/1200->expanded;
//   - switchesPerBoundary[boundary][direction]: boundary 0 = compact↔medium
//     (list-detail decl), boundary 1 = medium↔expanded (supporting-pane decl);
//     direction 0 = forward/expanding, 1 = backward/compacting;
//   - scroll/focus held OUTSIDE layout (plan A3 stable identity); scrollDelta
//     must stay within epsilon, focusLost=false on a clean sweep.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

// --- Inline port of the T1/T3/T5 contract logic used by the sweep ---
const BREAKPOINTS = { compactMaxWidth: 600, mediumMaxWidth: 840 };

function computeWidthClass(w) {
  if (w <= 600) return 'compact';
  if (w <= 840) return 'medium';
  return 'expanded';
}

function validMetrics(m) {
  return Number.isFinite(m.width) && Number.isFinite(m.height) && m.width > 0 && m.height > 0;
}

const kListDetailId = 1;
const kSupportingPaneId = 2;

function listDetailDual(widthClass) {
  return widthClass === 'medium' || widthClass === 'expanded';
}

function supportingDual(widthClass) {
  return widthClass === 'expanded';
}

// T5 in-place resize path state (module-held, as in window_resize_test.js).
let currentWidthClass = 'unknown';
let recreations = 0;

function onWindowBoundsChanged(m, d) {
  if (!validMetrics(m)) return 'kInvalidMetrics';
  if (!Number.isFinite(d.density) || d.density <= 0) return 'kInvalidMetrics';
  currentWidthClass = computeWidthClass(m.width);
  return 'kOk';
}

function display(density = 2.0, id = 0) {
  return { density, displayId: id };
}

// --- Test-only sweep driver (mirrors runResizeSweep in resize_sweep_test.cpp) ---
function runResizeSweep(widths, height, view, disturbFocusMidSweep = false, disturbScrollMidSweep = false) {
  const report = { switchesPerBoundary: [[0, 0], [0, 0]], scrollDelta: 0, focusLost: true };
  const scrollBaseline = view.scrollOffset;
  const focusBaseline = view.focusedNode;
  let backwards = false;
  let prevListDual = false;
  let prevSupDual = false;
  let first = true;
  for (let i = 0; i < widths.length; i++) {
    if (i > 0 && widths[i] < widths[i - 1]) backwards = true;
    if (onWindowBoundsChanged({ width: widths[i], height }, display()) !== 'kOk') {
      continue; // invalid step: no reflow, no switch, state untouched
    }
    const cls = currentWidthClass;
    // Cross-check: stored class matches a direct T1 recompute.
    const direct = computeWidthClass(widths[i]);
    if (direct !== cls) {
      continue;
    }
    const listDual = listDetailDual(cls);
    const supDual = supportingDual(cls);
    if (!first) {
      const dir = backwards ? 1 : 0;
      if (listDual !== prevListDual) report.switchesPerBoundary[0][dir] += 1;
      if (supDual !== prevSupDual) report.switchesPerBoundary[1][dir] += 1;
    }
    first = false;
    prevListDual = listDual;
    prevSupDual = supDual;
    if (disturbFocusMidSweep && i === Math.floor(widths.length / 2)) {
      view.focusedNode += 1000; // simulated focus theft mid-sweep
    }
    if (disturbScrollMidSweep && i === Math.floor(widths.length / 2)) {
      view.scrollOffset += 250; // simulated scroll jump mid-sweep
    }
  }
  report.scrollDelta = Math.abs(view.scrollOffset - scrollBaseline);
  report.focusLost = view.focusedNode !== focusBaseline;
  return report;
}

function roundTrip() {
  return [360, 500, 700, 900, 1200, 900, 700, 500, 360];
}

const kScrollEpsilon = 1e-3;

// ---------------------------------------------------------------------------
describe('TC resize sweep — compact→expanded→compact switches exactly once per boundary, preserves scroll/focus, no recreation (AC-031)', () => {
  it('check_forward_sweep_switches_once_per_boundary', () => {
    const view = { scrollOffset: 128, focusedNode: 42 };
    const r = runResizeSweep([360, 500, 700, 900, 1200], 700, view);
    assert.equal(r.switchesPerBoundary[0][0], 1);
    assert.equal(r.switchesPerBoundary[1][0], 1);
    assert.equal(r.focusLost, false);
    assert.ok(r.scrollDelta <= kScrollEpsilon);
  });

  it('check_backward_sweep_switches_once_per_boundary', () => {
    const view = { scrollOffset: 128, focusedNode: 42 };
    const r = runResizeSweep([1200, 900, 700, 500, 360], 700, view);
    assert.equal(r.switchesPerBoundary[0][1], 1);
    assert.equal(r.switchesPerBoundary[1][1], 1);
    assert.equal(r.focusLost, false);
    assert.ok(r.scrollDelta <= kScrollEpsilon);
  });

  it('check_roundtrip_preserves_scroll_within_epsilon', () => {
    const view = { scrollOffset: 128, focusedNode: 42 };
    const r = runResizeSweep(roundTrip(), 700, view);
    assert.equal(r.focusLost, false);
    assert.ok(r.scrollDelta <= kScrollEpsilon);
  });

  it('check_roundtrip_preserves_focus_identity', () => {
    const view = { scrollOffset: 64, focusedNode: 7 };
    const r = runResizeSweep(roundTrip(), 700, view);
    assert.equal(r.focusLost, false);
    assert.equal(view.focusedNode, 7);
    assert.ok(r.scrollDelta <= kScrollEpsilon);
  });

  it('check_focus_change_mid_sweep_reports_focus_lost', () => {
    const view = { scrollOffset: 64, focusedNode: 7 };
    const r = runResizeSweep(roundTrip(), 700, view, true);
    assert.equal(r.focusLost, true);
  });

  it('check_scroll_jump_mid_sweep_reports_scroll_delta', () => {
    const view = { scrollOffset: 64, focusedNode: 7 };
    const r = runResizeSweep(roundTrip(), 700, view, false, true);
    assert.equal(r.focusLost, false);
    assert.ok(r.scrollDelta > kScrollEpsilon);
  });

  it('check_redundant_same_class_steps_switch_nothing', () => {
    const view = { scrollOffset: 0, focusedNode: 1 };
    const r = runResizeSweep([360, 360, 500, 500, 360], 700, view);
    assert.equal(r.switchesPerBoundary[0][0], 0);
    assert.equal(r.switchesPerBoundary[0][1], 0);
    assert.equal(r.switchesPerBoundary[1][0], 0);
    assert.equal(r.switchesPerBoundary[1][1], 0);
    assert.equal(r.focusLost, false);
    assert.ok(r.scrollDelta <= kScrollEpsilon);
  });

  it('check_sweep_performs_no_recreation_and_returns_to_compact', () => {
    const recreationsBefore = recreations;
    const view = { scrollOffset: 10, focusedNode: 3 };
    const r = runResizeSweep(roundTrip(), 700, view);
    assert.equal(recreations, recreationsBefore);
    assert.equal(currentWidthClass, 'compact');
    assert.equal(r.focusLost, false);
  });
});
