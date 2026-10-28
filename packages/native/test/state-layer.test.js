'use strict';
// T2 regression tests — BUG-M3-NATIVE-001: prop-path `state` validation.
// FR-M3-001 / AC-M3-002: each interactive primitive drives states through the
// shared Ripple/state layer; present `state` routes through driveState.
// Failing-first: MUST FAIL on the stub base (silent passthrough, no throw).
const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { components } = require('../index.js');

// ASSUMPTION: interactive subset wired through withStateLayer by T3.
// Manifest names (run checkout): View, Text, Image, ScrollView, TextInput,
// Button, FlatList, TouchableOpacity, Modal. T3 wires these three;
// the rest keep verbatim passthrough.
const INTERACTIVE = ['Button', 'TouchableOpacity', 'TextInput'];

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
      assert.ok(!('stateLayer' in desc));
    });

    it('returns descriptor with no stateLayer and no throw for undefined props', () => {
      const desc = components[name]();
      assert.ok(desc);
      assert.equal(desc.type, name);
      assert.ok(!('stateLayer' in desc));
    });

    it('yields a stateLayer for valid state "hover"', () => {
      const desc = components[name]({ state: 'hover' });
      assert.ok(desc);
      assert.ok('stateLayer' in desc, 'expected stateLayer on descriptor');
    });
  });
}
