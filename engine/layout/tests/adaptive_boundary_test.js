'use strict';
// Batch 2 T3 boundary executable mirror — pane switch driven solely by the
// SizeClass boundary plus the consumed posture signal (FR-LAYOUT-011 AC-029).
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no env/config reads, no filesystem, no live services, no imports
// from src/ — the contract logic is ported into this file itself.
// Binds ONLY to the pinned T3 `Interface:` line:
//   `PaneArrangement arrangeAdaptive(AdaptiveDecl d, SizeClass s, PostureSignal p) ->
//    PaneArrangement{visiblePanes, hingeAvoidance}; unknown declaration ->
//    Status::kUnknownLayout`
// The C++ gate is `adaptive_boundary_test.cpp` in this directory; this file
// executes the SAME case list with the SAME pinned values (see check_* names).
//
// ASSUMPTIONS (pinned in unit-tests/T3.md):
//   - "boundary" means the tier step (thresholds are T1's job): Compact renders
//     single-pane, the next class up renders the wider arrangement;
//   - same SizeClass + same posture always yields the same arrangement;
//   - Unknown width class renders the safe single-pane fallback with kOk.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

// --- Inline port of the T3 contract logic (mirrors adaptive_boundary_test.cpp) ---
const kListDetailId = 1;
const kSupportingPaneId = 2;

function arrangeAdaptive(d, s, p) {
  const hingeAvoidance = Boolean(p.halfOpened && p.hingePresent);
  if (d.layoutId === kListDetailId) {
    const dual = s.widthClass === 'medium' || s.widthClass === 'expanded';
    return { primaryVisible: true, secondaryVisible: dual, hingeAvoidance, status: 'kOk' };
  }
  if (d.layoutId === kSupportingPaneId) {
    const dual = s.widthClass === 'expanded';
    return { primaryVisible: true, secondaryVisible: dual, hingeAvoidance, status: 'kOk' };
  }
  return { primaryVisible: false, secondaryVisible: false, hingeAvoidance: false, status: 'kUnknownLayout' };
}

function decl(id) {
  return { layoutId: id };
}

function sizeClass(w, h) {
  return { widthClass: w, heightClass: h };
}

function flatPosture() {
  return { halfOpened: false, hingePresent: false };
}

function hingePosture() {
  return { halfOpened: true, hingePresent: true };
}

// ---------------------------------------------------------------------------
describe('TC adaptive boundary — pane switch fires exactly at class steps, driven solely by class + posture (AC-029)', () => {
  it('check_compact_boundary_stays_single_pane', () => {
    const r = arrangeAdaptive(decl(kListDetailId), sizeClass('compact', 'compact'), flatPosture());
    assert.equal(r.status, 'kOk');
    assert.equal(r.primaryVisible, true);
    assert.equal(r.secondaryVisible, false);
    assert.equal(r.hingeAvoidance, false);
  });

  it('check_medium_step_switches_list_detail_once', () => {
    const below = arrangeAdaptive(decl(kListDetailId), sizeClass('compact', 'compact'), flatPosture());
    const at = arrangeAdaptive(decl(kListDetailId), sizeClass('medium', 'compact'), flatPosture());
    const above = arrangeAdaptive(decl(kListDetailId), sizeClass('expanded', 'compact'), flatPosture());
    assert.equal(below.secondaryVisible, false);
    assert.equal(at.secondaryVisible, true);
    assert.equal(above.secondaryVisible, true);
  });

  it('check_supporting_pane_switches_only_at_expanded', () => {
    const medium = arrangeAdaptive(decl(kSupportingPaneId), sizeClass('medium', 'medium'), flatPosture());
    const expanded = arrangeAdaptive(decl(kSupportingPaneId), sizeClass('expanded', 'medium'), flatPosture());
    assert.equal(medium.status, 'kOk');
    assert.equal(medium.secondaryVisible, false);
    assert.equal(expanded.status, 'kOk');
    assert.equal(expanded.secondaryVisible, true);
  });

  it('check_same_class_same_posture_is_deterministic', () => {
    const first = arrangeAdaptive(decl(kListDetailId), sizeClass('medium', 'medium'), flatPosture());
    const second = arrangeAdaptive(decl(kListDetailId), sizeClass('medium', 'medium'), flatPosture());
    assert.equal(first.status, second.status);
    assert.equal(first.primaryVisible, second.primaryVisible);
    assert.equal(first.secondaryVisible, second.secondaryVisible);
    assert.equal(first.hingeAvoidance, second.hingeAvoidance);
  });

  it('check_unknown_class_falls_back_to_single_pane', () => {
    const r = arrangeAdaptive(decl(kListDetailId), sizeClass('unknown', 'unknown'), flatPosture());
    assert.equal(r.status, 'kOk');
    assert.equal(r.primaryVisible, true);
    assert.equal(r.secondaryVisible, false);
  });

  it('check_fold_sequence_drives_panes_and_avoidance', () => {
    const start = arrangeAdaptive(decl(kListDetailId), sizeClass('compact', 'compact'), flatPosture());
    const folded = arrangeAdaptive(decl(kListDetailId), sizeClass('expanded', 'medium'), hingePosture());
    const back = arrangeAdaptive(decl(kListDetailId), sizeClass('compact', 'compact'), flatPosture());
    assert.equal(start.secondaryVisible, false);
    assert.equal(start.hingeAvoidance, false);
    assert.equal(folded.secondaryVisible, true);
    assert.equal(folded.hingeAvoidance, true);
    assert.equal(back.secondaryVisible, false);
    assert.equal(back.hingeAvoidance, false);
  });
});
