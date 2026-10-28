'use strict';
// Batch 2 T3 executable mirror — declarative canonical adaptive layouts
// (FR-LAYOUT-003 AC-LAYOUT-006; TC-LAYOUT-006 gate).
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no env/config reads, no filesystem, no live services, no imports
// from src/ — the contract logic is ported into this file itself.
// Binds ONLY to the pinned T3 `Interface:` line:
//   `PaneArrangement arrangeAdaptive(AdaptiveDecl d, SizeClass s, PostureSignal p) ->
//    PaneArrangement{visiblePanes, hingeAvoidance}; unknown declaration ->
//    Status::kUnknownLayout`
// The C++ gate is `adaptive_test.cpp` in this directory; this file executes
// the SAME case list with the SAME pinned values (see check_* names).
//
// ASSUMPTIONS (pinned in unit-tests/T3.md):
//   - AdaptiveDecl is { layoutId } with kListDetailId == 1,
//     kSupportingPaneId == 2, kFeedId == 3; any other id is unknown;
//   - SizeClass is { widthClass, heightClass } with tiers
//     'unknown' | 'compact' | 'medium' | 'expanded' (thresholds are T1's job);
//   - PostureSignal is { halfOpened, hingePresent }; flat/no-hinge is
//     { halfOpened: false, hingePresent: false };
//   - PaneArrangement is { primaryVisible, secondaryVisible, hingeAvoidance, status };
//   - list-detail dual-pane at Medium+; supporting-pane dual at Expanded only;
//     feed always single-pane; half-opened + hinge => avoidance.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

// --- Inline port of the T3 contract logic (mirrors adaptive_test.cpp) ---
const kListDetailId = 1;
const kSupportingPaneId = 2;
const kFeedId = 3;

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
  if (d.layoutId === kFeedId) {
    return { primaryVisible: true, secondaryVisible: false, hingeAvoidance, status: 'kOk' };
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
describe('TC adaptive canonical layouts — list-detail/supporting-pane/feed derive panes from SizeClass + posture (AC-LAYOUT-006)', () => {
  it('check_list_detail_compact_shows_list_only', () => {
    const r = arrangeAdaptive(decl(kListDetailId), sizeClass('compact', 'compact'), flatPosture());
    assert.equal(r.status, 'kOk');
    assert.equal(r.primaryVisible, true);
    assert.equal(r.secondaryVisible, false);
  });

  it('check_list_detail_medium_shows_both_panes', () => {
    const r = arrangeAdaptive(decl(kListDetailId), sizeClass('medium', 'medium'), flatPosture());
    assert.equal(r.status, 'kOk');
    assert.equal(r.primaryVisible, true);
    assert.equal(r.secondaryVisible, true);
  });

  it('check_list_detail_expanded_shows_both_panes', () => {
    const r = arrangeAdaptive(decl(kListDetailId), sizeClass('expanded', 'expanded'), flatPosture());
    assert.equal(r.status, 'kOk');
    assert.equal(r.primaryVisible, true);
    assert.equal(r.secondaryVisible, true);
  });

  it('check_supporting_pane_compact_single_expanded_dual', () => {
    const compact = arrangeAdaptive(decl(kSupportingPaneId), sizeClass('compact', 'compact'), flatPosture());
    const expanded = arrangeAdaptive(decl(kSupportingPaneId), sizeClass('expanded', 'expanded'), flatPosture());
    assert.equal(compact.status, 'kOk');
    assert.equal(compact.primaryVisible, true);
    assert.equal(compact.secondaryVisible, false);
    assert.equal(expanded.status, 'kOk');
    assert.equal(expanded.primaryVisible, true);
    assert.equal(expanded.secondaryVisible, true);
  });

  it('check_feed_single_pane_at_all_classes', () => {
    const compact = arrangeAdaptive(decl(kFeedId), sizeClass('compact', 'compact'), flatPosture());
    const expanded = arrangeAdaptive(decl(kFeedId), sizeClass('expanded', 'expanded'), flatPosture());
    assert.equal(compact.status, 'kOk');
    assert.equal(compact.primaryVisible, true);
    assert.equal(compact.secondaryVisible, false);
    assert.equal(expanded.status, 'kOk');
    assert.equal(expanded.primaryVisible, true);
    assert.equal(expanded.secondaryVisible, false);
  });

  it('check_unknown_declaration_yields_unknown_layout', () => {
    const compact = arrangeAdaptive(decl(999), sizeClass('compact', 'compact'), flatPosture());
    const expanded = arrangeAdaptive(decl(999), sizeClass('expanded', 'expanded'), flatPosture());
    assert.equal(compact.status, 'kUnknownLayout');
    assert.equal(expanded.status, 'kUnknownLayout');
  });

  it('check_half_opened_hinge_sets_hinge_avoidance', () => {
    const r = arrangeAdaptive(decl(kListDetailId), sizeClass('expanded', 'medium'), hingePosture());
    assert.equal(r.status, 'kOk');
    assert.equal(r.hingeAvoidance, true);
  });

  it('check_flat_posture_has_no_hinge_avoidance', () => {
    const r = arrangeAdaptive(decl(kListDetailId), sizeClass('expanded', 'medium'), flatPosture());
    assert.equal(r.status, 'kOk');
    assert.equal(r.hingeAvoidance, false);
  });
});
