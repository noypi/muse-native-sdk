'use strict';
// T1 — FR-DX-003 / AC-DX-010 / TC-DX-010 (Batch 1).
// Hermetic: node:test + node:assert/strict only. The LayoutSource seam
// ({ getConstraintInput, getComputedBox, getWinningDeclaration }) is an
// in-process stub; no layout engine, no device, no network.
// Interface under test (plan verbatim):
//   describeLayout(nodeId, source) => { nodeId, constraintInput,
//     computedBox: {x,y,width,height},
//     styleProvenance: { declaration, origin } }
//     | throws { code: 'E_INSPECTOR_UNKNOWN_NODE', cause, action }

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'layout.js');

function load() {
  return require(LIB);
}

function stubSource() {
  const nodes = {
    'node-1': {
      constraintInput: { engine: 'yoga', width: 390, height: 844, flexDirection: 'column' },
      computedBox: { x: 0, y: 64, width: 390, height: 120 },
      winningDeclaration: { declaration: '.card { height: 120px; }', origin: 'app/styles/card.css:12' },
    },
  };
  return {
    nodes,
    getConstraintInput(nodeId) {
      const n = nodes[nodeId];
      if (!n) return undefined;
      return n.constraintInput;
    },
    getComputedBox(nodeId) {
      const n = nodes[nodeId];
      if (!n) return undefined;
      return n.computedBox;
    },
    getWinningDeclaration(nodeId) {
      const n = nodes[nodeId];
      if (!n) return undefined;
      return n.winningDeclaration;
    },
  };
}

describe('describeLayout', () => {
  it('returns constraint input, computed box and style provenance for a known node', () => {
    const { describeLayout } = load();
    const out = describeLayout('node-1', stubSource());
    assert.equal(out.nodeId, 'node-1');
    assert.deepEqual(Object.keys(out).sort(), ['computedBox', 'constraintInput', 'nodeId', 'styleProvenance']);
    assert.equal(out.constraintInput.engine, 'yoga');
    assert.deepEqual(out.computedBox, { x: 0, y: 64, width: 390, height: 120 });
    assert.equal(out.styleProvenance.declaration, '.card { height: 120px; }');
    assert.equal(out.styleProvenance.origin, 'app/styles/card.css:12');
  });

  it('computed box contains numeric x, y, width and height', () => {
    const { describeLayout } = load();
    const { computedBox } = describeLayout('node-1', stubSource());
    for (const k of ['x', 'y', 'width', 'height']) {
      assert.equal(typeof computedBox[k], 'number', `computedBox.${k} must be a number`);
    }
  });

  it('throws E_INSPECTOR_UNKNOWN_NODE for an unknown node', () => {
    const { describeLayout } = load();
    assert.throws(
      () => describeLayout('no-such-node', stubSource()),
      (err) => {
        assert.equal(err.code, 'E_INSPECTOR_UNKNOWN_NODE');
        assert.ok(err.cause && typeof err.cause === 'string', 'cause must be present');
        assert.ok(err.action && typeof err.action === 'string', 'action must be present');
        assert.match(String(err.cause) + String(err.action), /DX-003|AC-DX-010|inspector/i);
        return true;
      },
    );
  });

  it('throws E_INSPECTOR_UNKNOWN_NODE for a missing or empty node id (ASSUMPTION)', () => {
    const { describeLayout } = load();
    for (const bad of [undefined, null, '']) {
      assert.throws(() => describeLayout(bad, stubSource()), (err) => err.code === 'E_INSPECTOR_UNKNOWN_NODE');
    }
  });

  it('does not mutate the injected source (read-only view)', () => {
    const { describeLayout } = load();
    const source = stubSource();
    const before = JSON.parse(JSON.stringify(source.nodes));
    describeLayout('node-1', source);
    assert.deepEqual(source.nodes, before);
  });

  it('rejects a hostile node id without leaking internals (ASSUMPTION)', () => {
    const { describeLayout } = load();
    assert.throws(
      () => describeLayout('__proto__', stubSource()),
      (err) => {
        assert.match(String(err.code), /^E_INSPECTOR_/);
        assert.doesNotMatch(String(err.cause ?? ''), /node_modules|at\s+Object\./);
        return true;
      },
    );
  });
});
