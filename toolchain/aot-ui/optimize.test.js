'use strict';
// T8 — constant folding + tree shaking with removed-byte report
// (NFR-BUILD-013 / AC-BUILD-044). Hermetic: node:test + node:assert/strict +
// stdlib only. Pure in-memory trees; no filesystem, no network, no secrets,
// no live services.
// Red state: ./optimize.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND) until T8 lands.
//
// Interface under test (plan verbatim):
//   optimize(tree: unknown): { tree: unknown, removedBytes: number } — `removedBytes` = bytes eliminated (unreachable branches + unused components); pure function over T5's tree shape.
//
// ASSUMPTION (tree shape — plan pins "T5's tree shape", not field names):
// nodes are plain JSON objects over `{ kind, name?, value?, cond?, used?,
// children? }` as emitted by T5 `compile` (`kind` is one of
// `'element' | 'text' | 'expr' | 'if'`; element nodes carry `name`/`props`/
// `children`). The input may also be the T5 payload wrapper
// `{ version, root: [...] }`, a single node, or an array of nodes.
// ASSUMPTION (unreachable branch): an `{ kind:'if', cond:false, ... }` node
// (constant-folded false condition) is removed with its whole subtree; an
// `{ kind:'if', cond:true, ... }` node folds to its children.
// ASSUMPTION (unused component): an element node with `used:false` is removed
// with its subtree; elements without `used:false` are kept.
// ASSUMPTION (constant folding): an `{ kind:'expr', value }` node whose value
// is a JSON literal (number/boolean/null/quoted string) folds to
// `{ kind:'text', value }`.
// ASSUMPTION (removedBytes): `removedBytes` is the UTF-8 JSON byte-length
// delta `byteLength(stringify(input)) - byteLength(stringify(output))`,
// clamped at 0. A no-op optimization reports `removedBytes: 0`.
// ASSUMPTION (purity): the input tree is never mutated.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./optimize.js');
}

function bytesOf(value) {
  return Buffer.byteLength(JSON.stringify(value), 'utf8');
}

const LIVE_TREE = {
  version: 1,
  root: [
    {
      kind: 'element', name: 'View', props: {}, children: [
        { kind: 'element', name: 'Text', props: {}, children: [{ kind: 'text', value: 'hello' }] },
      ],
    },
  ],
};

const DEAD_TREE = {
  version: 1,
  root: [
    {
      kind: 'element', name: 'View', props: {}, children: [
        { kind: 'element', name: 'Text', props: {}, children: [{ kind: 'text', value: 'hello' }] },
        { kind: 'if', cond: false, children: [{ kind: 'text', value: 'never-shown' }] },
        { kind: 'element', name: 'DebugOverlay', props: {}, used: false, children: [{ kind: 'text', value: 'debug' }] },
      ],
    },
  ],
};

describe('T8 — unreachable branches and unused components removed (AC-BUILD-044)', () => {
  it('removes constant-false branches and unused components, reports removedBytes > 0', () => {
    const { optimize } = loadImpl();
    const before = bytesOf(DEAD_TREE);
    const result = optimize(structuredClone(DEAD_TREE));
    assert.ok(result.removedBytes > 0);
    const text = JSON.stringify(result.tree);
    assert.ok(!text.includes('never-shown'));
    assert.ok(!text.includes('DebugOverlay'));
    assert.ok(text.includes('hello'));
  });

  it('removedBytes equals the JSON byte-length delta on the fixture', () => {
    const { optimize } = loadImpl();
    const input = structuredClone(DEAD_TREE);
    const before = bytesOf(input);
    const result = optimize(input);
    assert.equal(result.removedBytes, before - bytesOf(result.tree));
  });

  it('live tree is kept intact with removedBytes 0', () => {
    const { optimize } = loadImpl();
    const result = optimize(structuredClone(LIVE_TREE));
    assert.equal(result.removedBytes, 0);
    assert.deepEqual(result.tree, LIVE_TREE);
  });

  it('constant-true branch folds to its children', () => {
    const { optimize } = loadImpl();
    const input = [{ kind: 'if', cond: true, children: [{ kind: 'text', value: 'shown' }] }];
    const result = optimize(input);
    assert.deepEqual(result.tree, [{ kind: 'text', value: 'shown' }]);
  });
});

describe('T8 — constant folding', () => {
  it('literal expression folds to a text node', () => {
    const { optimize } = loadImpl();
    const result = optimize([{ kind: 'expr', value: '42' }]);
    assert.deepEqual(result.tree, [{ kind: 'text', value: '42' }]);
  });

  it('non-literal expression is kept as-is', () => {
    const { optimize } = loadImpl();
    const input = [{ kind: 'expr', value: 'user.name' }];
    const result = optimize(input);
    assert.deepEqual(result.tree, input);
  });
});

describe('T8 — boundary and purity', () => {
  it('does not mutate the input tree', () => {
    const { optimize } = loadImpl();
    const input = structuredClone(DEAD_TREE);
    const snapshot = JSON.stringify(input);
    optimize(input);
    assert.equal(JSON.stringify(input), snapshot);
  });

  it('non-object input passes through with removedBytes 0', () => {
    const { optimize } = loadImpl();
    for (const value of [null, 42, 'tree', true]) {
      const result = optimize(value);
      assert.equal(result.tree, value);
      assert.equal(result.removedBytes, 0);
    }
  });

  it('empty array optimizes to empty array with removedBytes 0', () => {
    const { optimize } = loadImpl();
    const result = optimize([]);
    assert.deepEqual(result.tree, []);
    assert.equal(result.removedBytes, 0);
  });

  it('injection-style names never pollute prototypes', () => {
    const { optimize } = loadImpl();
    const input = [{ kind: 'element', name: '__proto__', props: {}, used: false, children: [] }];
    const result = optimize(input);
    assert.deepEqual(result.tree, []);
    assert.equal(Object.prototype.polluted, undefined);
    assert.ok(result.removedBytes > 0);
  });
});
