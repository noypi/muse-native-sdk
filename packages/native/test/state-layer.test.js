'use strict';
// T2 regression tests — BUG-M3-NATIVE-001: prop-path `state` validation.
// FR-M3-001 / AC-M3-002: each interactive primitive drives states through the
// shared Ripple/state layer; present `state` routes through driveState.
// Failing-first: MUST FAIL on the stub base (silent passthrough, no throw).
const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { components, driveState } = require('../index.js');

// Interactive subset wired through withStateLayer (index.js):
// Surface, Ripple, TextFieldEditCore. Real shape is
// `{ type, props: { ...next, stateLayer } }` — stateLayer lives on
// `desc.props`, produced by driveState(name, state).
const INTERACTIVE = ['Surface', 'Ripple', 'TextFieldEditCore'];

for (const name of INTERACTIVE) {
  describe(`${name} state prop`, () => {
    it('throws TypeError for present non-string state (e.g. 42)', () => {
      assert.throws(() => components[name]({ state: 42 }), TypeError);
    });

    it('throws RangeError for unknown string state (e.g. "giant")', () => {
      assert.throws(() => components[name]({ state: 'giant' }), RangeError);
    });

    it('returns descriptor with no stateLayer and no throw when state absent', () => {
      const desc = components[name]({});
      assert.ok(desc);
      assert.equal(desc.type, name);
      assert.ok(desc.props && !('stateLayer' in desc.props));
    });

    it('returns descriptor with no stateLayer and no throw for undefined props', () => {
      const desc = components[name]();
      assert.ok(desc);
      assert.equal(desc.type, name);
      assert.ok(desc.props && !('stateLayer' in desc.props));
    });

    it('yields a stateLayer for valid state "hover"', () => {
      const desc = components[name]({ state: 'hover' });
      assert.ok(desc);
      assert.ok(desc.props && 'stateLayer' in desc.props, 'expected stateLayer on descriptor props');
      assert.equal(desc.props.stateLayer.state, 'hover');
    });
  });
}

// ---------------------------------------------------------------------------
// BUG-M3-FIDELITY-001 T1: driveState resolves NUMERIC M3 state-layer
// opacities from the token table (FR-M3-001/AC-M3-002, FR-M3-008/AC-M3-018).
// Root cause: STATE_OPACITY returned symbolic strings ('hover-opacity', …)
// no device can paint. Failing-first: every numeric assertion below MUST
// FAIL on the symbolic-string base.
// ---------------------------------------------------------------------------

const SYMBOLIC_OPACITIES = new Set([
  'hover-opacity',
  'focus-opacity',
  'pressed-opacity',
  'disabled-opacity',
]);

describe('driveState numeric M3 opacities (T1)', () => {
  it('resolves hover opacity to the number 0.08', () => {
    const spec = driveState('Surface', 'hover');
    assert.equal(typeof spec.layer.props.opacity, 'number');
    assert.equal(spec.layer.props.opacity, 0.08);
  });

  it('resolves focus opacity to the number 0.12', () => {
    const spec = driveState('Surface', 'focus');
    assert.equal(typeof spec.layer.props.opacity, 'number');
    assert.equal(spec.layer.props.opacity, 0.12);
  });

  it('resolves pressed opacity to the number 0.16', () => {
    const spec = driveState('Surface', 'pressed');
    assert.equal(typeof spec.layer.props.opacity, 'number');
    assert.equal(spec.layer.props.opacity, 0.16);
  });

  it('resolves disabled opacity to a number from the baseline (never a symbolic string)', () => {
    // ASSUMPTION D1: AC-M3-018 names only hover/focus/press (8/12/16); the
    // disabled numeric lives in the conformance baseline (plan A1). This test
    // pins numeric-type + range; the exact baseline value is re-proved by the
    // T3 conformance rows, not guessed here.
    const spec = driveState('Surface', 'disabled');
    assert.equal(typeof spec.layer.props.opacity, 'number');
    assert.ok(Number.isFinite(spec.layer.props.opacity));
    assert.ok(spec.layer.props.opacity >= 0 && spec.layer.props.opacity <= 1);
    assert.ok(!SYMBOLIC_OPACITIES.has(spec.layer.props.opacity));
  });

  it('never returns a symbolic opacity string for any known state', () => {
    for (const state of ['hover', 'focus', 'pressed', 'disabled']) {
      const spec = driveState('Surface', state);
      assert.equal(typeof spec.layer.props.opacity, 'number', `opacity for "${state}" must be a number`);
    }
  });

  it('keeps the Ripple layer shape with token-role color primary', () => {
    const spec = driveState('Surface', 'hover');
    assert.equal(spec.target, 'Surface');
    assert.equal(spec.state, 'hover');
    assert.equal(spec.layer.type, 'Ripple');
    assert.equal(spec.layer.props.color, 'primary');
  });

  it('throws TypeError for non-string states (42, null, undefined, object)', () => {
    for (const bad of [42, null, undefined, {}, true]) {
      assert.throws(() => driveState('Surface', bad), TypeError);
    }
  });

  it('throws RangeError for unknown strings ("giant", "", "hovered")', () => {
    // 'hovered' is the engine/layout token-state spelling; driveState keeps
    // the existing 'hover' contract, so 'hovered' is an unknown string here.
    for (const bad of ['giant', '', 'hovered']) {
      assert.throws(() => driveState('Surface', bad), RangeError);
    }
  });
});
