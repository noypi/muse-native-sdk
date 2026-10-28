'use strict';
// T2 — FR-DX-002 (AC-DX-006): style/token hot swap without reload or re-instantiation.
// Hermetic: node:test + node:assert/strict + stdlib only. No network, no secrets,
// no live services, no filesystem, no clock.
// ASSUMPTIONS (contract genuinely silent on structural shape; most defensible
// behavior tested — marked ASSUMPTION in T2.md):
// - A-T2a: ComponentTree is a plain object { components: [{id, instanceId,
//   resolved: { color: string }}], fullReload?: boolean }; TokenDelta is
//   { tokens: { [name]: string }, mapping?: { [tokenName]: string[] } } where
//   mapping names which component ids consume a token; default is all.
// - A-T2b: token name `colorPrimary` resolves to `resolved.color` on consuming
//   components; unknown token names throw.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { applyTokenSwap } = require('../lib/token-swap.js');

function makeTree() {
  return {
    components: [
      { id: 'a', instanceId: 'inst-a-1', resolved: { color: '#ffffff' } },
      { id: 'b', instanceId: 'inst-b-1', resolved: { color: '#ffffff' } },
    ],
  };
}

describe('applyTokenSwap — re-theme without re-instantiation', () => {
  it('re-themes resolved colors from the token delta', () => {
    const out = applyTokenSwap(makeTree(), { tokens: { colorPrimary: '#000000' } });
    for (const c of out.tree.components) {
      assert.equal(c.resolved.color, '#000000');
    }
  });

  it('returns zero re-instantiations', () => {
    const out = applyTokenSwap(makeTree(), { tokens: { colorPrimary: '#000000' } });
    assert.deepEqual(out.reinstantiated, []);
  });

  it('sets no full reload flag', () => {
    const out = applyTokenSwap(makeTree(), { tokens: { colorPrimary: '#000000' } });
    assert.notEqual(out.tree.fullReload, true);
  });

  it('preserves component instance identity', () => {
    const tree = makeTree();
    const out = applyTokenSwap(tree, { tokens: { colorPrimary: '#000000' } });
    assert.deepEqual(
      out.tree.components.map((c) => c.instanceId),
      ['inst-a-1', 'inst-b-1'],
    );
  });

  it('scopes the delta to mapped components when a mapping is given', () => {
    const out = applyTokenSwap(makeTree(), {
      tokens: { colorPrimary: '#111111' },
      mapping: { colorPrimary: ['a'] },
    });
    assert.equal(out.tree.components.find((c) => c.id === 'a').resolved.color, '#111111');
    assert.equal(out.tree.components.find((c) => c.id === 'b').resolved.color, '#ffffff');
    assert.deepEqual(out.reinstantiated, []);
  });

  it('empty delta leaves colors untouched with zero re-instantiations', () => {
    const out = applyTokenSwap(makeTree(), { tokens: {} });
    assert.equal(out.tree.components[0].resolved.color, '#ffffff');
    assert.deepEqual(out.reinstantiated, []);
  });

  it('throws on an unknown token name', () => {
    assert.throws(
      () => applyTokenSwap(makeTree(), { tokens: { nopeToken: '#000' } }),
      /nopeToken/,
    );
  });

  it('rejects a non-array token-scope mapping safely', () => {
    assert.throws(
      () => applyTokenSwap(makeTree(), {
        tokens: { colorPrimary: '#111111' },
        mapping: { colorPrimary: 'a' },
      }),
      TypeError,
    );
  });
});
