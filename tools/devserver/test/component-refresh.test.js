'use strict';
// T1 — FR-DX-002 (AC-DX-005): component-level refresh preserving instances/nav/signals.
// Hermetic: node:test + node:assert/strict + stdlib only. No network, no secrets,
// no live services, no filesystem, no clock.
// ASSUMPTIONS (contract genuinely silent on structural shape; most defensible
// behavior tested — marked ASSUMPTION in T1.md):
// - A-T1a: ComponentTree is a plain object { components: [{id, output,
//   instanceId, localState, signals, form}], navStack: string[], signals: {},
//   forms: {} }; ComponentEdit is { componentId, nextOutput }.
// - A-T1b: applyComponentRefresh mutates tree in place for the edited leaf's
//   output and returns { preserved, reset, overlayNotice }.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { applyComponentRefresh } = require('../lib/refresh.js');

function makeTree() {
  return {
    components: [
      {
        id: 'leaf',
        output: '<Text>v1</Text>',
        instanceId: 'inst-leaf-1',
        localState: { counter: 3 },
        signals: { theme: 'light' },
        form: { draft: 'hello' },
      },
      {
        id: 'sibling',
        output: '<Text>sib</Text>',
        instanceId: 'inst-sib-1',
        localState: {},
        signals: { theme: 'light' },
        form: { draft: 'typed-input' },
      },
    ],
    navStack: ['Home', 'Detail'],
    signals: { theme: 'light', count: 7 },
    forms: { sibling: { draft: 'typed-input' }, leaf: { draft: 'hello' } },
  };
}

describe('applyComponentRefresh — edited leaf swaps output', () => {
  it('swaps the edited leaf output in place', () => {
    const tree = makeTree();
    applyComponentRefresh(tree, { componentId: 'leaf', nextOutput: '<Text>v2</Text>' });
    assert.equal(
      tree.components.find((c) => c.id === 'leaf').output,
      '<Text>v2</Text>',
    );
  });

  it('keeps sibling instance identity unchanged', () => {
    const tree = makeTree();
    const before = tree.components.find((c) => c.id === 'sibling').instanceId;
    const result = applyComponentRefresh(tree, { componentId: 'leaf', nextOutput: '<Text>v2</Text>' });
    assert.equal(tree.components.find((c) => c.id === 'sibling').instanceId, before);
    assert.ok(result.preserved.includes('sibling'));
  });

  it('keeps nav stack depth and entries unchanged', () => {
    const tree = makeTree();
    applyComponentRefresh(tree, { componentId: 'leaf', nextOutput: '<Text>v2</Text>' });
    assert.deepEqual(tree.navStack, ['Home', 'Detail']);
  });

  it('keeps form input unchanged', () => {
    const tree = makeTree();
    applyComponentRefresh(tree, { componentId: 'leaf', nextOutput: '<Text>v2</Text>' });
    assert.deepEqual(tree.forms, {
      sibling: { draft: 'typed-input' },
      leaf: { draft: 'hello' },
    });
  });

  it('keeps signal values unchanged', () => {
    const tree = makeTree();
    applyComponentRefresh(tree, { componentId: 'leaf', nextOutput: '<Text>v2</Text>' });
    assert.deepEqual(tree.signals, { theme: 'light', count: 7 });
  });

  it('surfaces overlayNotice naming the reset local state', () => {
    const tree = makeTree();
    const result = applyComponentRefresh(tree, { componentId: 'leaf', nextOutput: '<Text>v2</Text>' });
    assert.ok(result.reset.includes('counter'));
    assert.equal(typeof result.overlayNotice, 'string');
    assert.ok(result.overlayNotice.includes('counter'));
  });

  it('returns null overlayNotice when edited leaf has no non-signal local state', () => {
    const tree = makeTree();
    tree.components.find((c) => c.id === 'leaf').localState = {};
    const result = applyComponentRefresh(tree, { componentId: 'leaf', nextOutput: '<Text>v2</Text>' });
    assert.deepEqual(result.reset, []);
    assert.equal(result.overlayNotice, null);
  });

  it('throws for an unknown component id', () => {
    const tree = makeTree();
    assert.throws(
      () => applyComponentRefresh(tree, { componentId: 'nope', nextOutput: 'x' }),
      /nope/,
    );
  });
});
