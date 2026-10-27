'use strict';
// TC-TSX-031 (AC-TSX-031) — Batch 3 T8.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T8 `Interface:` line, never to implementation.
// Validator resolution: `../lib/motion.js` once it lands, else an inline
// fallback stub whose behavior assertions fail, so every behavior case below
// is red until lib/motion.js lands — and the harness itself never crashes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../lib/motion.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/motion.js lands.
  mod = {
    checkSharedIds: () => [{ code: 'STUB' }],
  };
}
const { checkSharedIds } = mod;

function loc(overrides = {}) {
  return {
    file: 'Login.tsx',
    line: 12,
    column: 5,
    nodePath: 'Login > View[0]',
    ...overrides,
  };
}

// ---------------------------------------------------------------------------
// TC-TSX-031 → AC-TSX-031 — T8 contract:
// `checkSharedIds(nodes: Array<{sharedId?: string, route: string, loc: Loc}>): Diagnostic[]`
// ---------------------------------------------------------------------------
describe('TC-TSX-031 — matched sharedId pairs clean, lone sharedId warns (AC-TSX-031)', () => {
  it('matched id on two routes yields []', () => {
    const out = checkSharedIds([
      { sharedId: 'hero', route: '/a', loc: loc() },
      { sharedId: 'hero', route: '/b', loc: loc() },
    ]);
    assert.deepEqual(out, []);
  });

  it('lone id yields exactly one TSX_SHARED_ID_LONE warning', () => {
    const l = loc();
    const out = checkSharedIds([{ sharedId: 'hero', route: '/a', loc: l }]);
    assert.equal(out.length, 1);
    const d = out[0];
    assert.equal(d.code, 'TSX_SHARED_ID_LONE');
    assert.equal(d.sharedId, 'hero');
    assert.equal(d.route, '/a');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
    assert.equal(d.nodePath, l.nodePath);
    assert.equal(d.severity, 'warning');
  });

  it('no ids yields []', () => {
    assert.deepEqual(checkSharedIds([]), []);
    assert.deepEqual(
      checkSharedIds([{ route: '/a', loc: loc() }]),
      []
    );
  });

  it('two distinct lone ids yield two warnings, never throws', () => {
    const out = checkSharedIds([
      { sharedId: 'a', route: '/x', loc: loc() },
      { sharedId: 'b', route: '/y', loc: loc() },
    ]);
    assert.equal(out.length, 2);
    assert.doesNotThrow(() => checkSharedIds(undefined));
  });
});
