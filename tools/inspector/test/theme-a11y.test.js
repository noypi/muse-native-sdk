'use strict';
// T4 — FR-DX-003 / AC-DX-013 / TC-DX-013 (Batch 2).
// Hermetic: node:test + node:assert/strict only. previewTokens and
// mirrorSemantics are pure functions; no device, no network, no config.
// Interface under test (plan verbatim):
//   previewTokens(baseTokens, patch) => { patched, diff: Array<{ path, from, to }> }
//   mirrorSemantics(tree) => { tree } | throws { code: 'E_INSPECTOR_BAD_TREE', ... }
//   (deep-frozen read-only copy)

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'theme-a11y.js');

function load() {
  return require(LIB);
}

describe('previewTokens', () => {
  it('previews a token patch and exports a non-empty committable diff', () => {
    const { previewTokens } = load();
    const base = { color: { primary: '#111111', secondary: '#222222' }, space: { md: 16 } };
    const out = previewTokens(base, { color: { primary: '#ff0000' } });
    assert.deepEqual(Object.keys(out).sort(), ['diff', 'patched']);
    assert.equal(out.patched.color.primary, '#ff0000');
    assert.equal(out.patched.color.secondary, '#222222');
    assert.ok(Array.isArray(out.diff) && out.diff.length > 0, 'diff must be non-empty');
    const entry = out.diff.find((d) => d.path === 'color.primary');
    assert.ok(entry, 'diff must contain the changed path');
    assert.deepEqual(Object.keys(entry).sort(), ['from', 'path', 'to']);
    assert.equal(entry.from, '#111111');
    assert.equal(entry.to, '#ff0000');
  });

  it('does not mutate the base tokens (live preview is side-effect free)', () => {
    const { previewTokens } = load();
    const base = { color: { primary: '#111111' } };
    const before = JSON.parse(JSON.stringify(base));
    const out = previewTokens(base, { color: { primary: '#ff0000' } });
    assert.deepEqual(base, before);
    assert.notEqual(out.patched, base);
  });

  it('returns an empty diff for an empty patch (ASSUMPTION)', () => {
    const { previewTokens } = load();
    const out = previewTokens({ color: { primary: '#111111' } }, {});
    assert.deepEqual(out.diff, []);
    assert.deepEqual(out.patched, { color: { primary: '#111111' } });
  });

  it('lists added keys with from === undefined (ASSUMPTION)', () => {
    const { previewTokens } = load();
    const out = previewTokens({ color: {} }, { color: { accent: '#00ff00' } });
    const entry = out.diff.find((d) => d.path === 'color.accent');
    assert.ok(entry, 'added key must appear in diff');
    assert.equal(entry.from, undefined);
    assert.equal(entry.to, '#00ff00');
  });

  it('rejects a hostile patch key without polluting the prototype (ASSUMPTION)', () => {
    const { previewTokens } = load();
    assert.throws(
      () => previewTokens({}, JSON.parse('{ "__proto__": { "polluted": true } }')),
      (err) => {
        assert.match(String(err.code || err.message), /E_INSPECTOR_|BAD/i);
        assert.equal({}.polluted, undefined);
        return true;
      },
    );
  });
});

describe('mirrorSemantics', () => {
  function sampleTree() {
    return {
      role: 'root',
      label: 'app',
      children: [
        { role: 'button', label: 'Submit' },
        { role: 'text', label: 'Hello' },
      ],
    };
  }

  it('mirrors node roles and labels', () => {
    const { mirrorSemantics } = load();
    const out = mirrorSemantics(sampleTree());
    assert.ok(out && typeof out === 'object', 'must return a tree object');
    const tree = out.tree || out;
    assert.equal(tree.role, 'root');
    assert.equal(tree.children[0].role, 'button');
    assert.equal(tree.children[0].label, 'Submit');
    assert.equal(tree.children[1].role, 'text');
  });

  it('returns a deep-frozen read-only copy (mutation is rejected)', () => {
    const { mirrorSemantics } = load();
    const src = sampleTree();
    const out = mirrorSemantics(src);
    const tree = out.tree || out;
    assert.ok(Object.isFrozen(tree), 'mirror must be frozen');
    assert.ok(Object.isFrozen(tree.children), 'children must be frozen');
    assert.ok(Object.isFrozen(tree.children[0]), 'child nodes must be frozen');
    assert.throws(() => {
      'use strict';
      tree.role = 'hacked';
    }, /TypeError|read only|Cannot assign/i);
    // Source itself is not mutated by mirroring.
    assert.equal(src.role, 'root');
  });

  it('throws E_INSPECTOR_BAD_TREE for a missing or malformed tree', () => {
    const { mirrorSemantics } = load();
    for (const bad of [undefined, null, 42, 'tree', [], { children: [] }]) {
      assert.throws(
        () => mirrorSemantics(bad),
        (err) => {
          assert.equal(err.code, 'E_INSPECTOR_BAD_TREE');
          return true;
        },
        `must reject ${JSON.stringify(bad)}`,
      );
    }
  });

  it('error names the subsystem without leaking internals (ASSUMPTION)', () => {
    const { mirrorSemantics } = load();
    assert.throws(
      () => mirrorSemantics(null),
      (err) => {
        assert.equal(err.code, 'E_INSPECTOR_BAD_TREE');
        assert.doesNotMatch(String(err.cause ?? err.message ?? ''), /node_modules|at\s+Object\./);
        return true;
      },
    );
  });
});
