'use strict';
// TC-TSX-032 (AC-TSX-032) — Batch 3 T9.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T9 `Interface:` line, never to implementation.
// Validator resolution: `../lib/motion.js` once it lands, else an inline
// fallback stub whose behavior assertions fail, so every behavior case below
// is red until lib/motion.js lands — and the harness itself never crashes.
// Known-pattern injection (plan A4): the test passes `knownPatterns`
// explicitly — caller-supplied list, no engine work, no fs reads in tests.
// Canonical FR-MOTION-014 default names (AC-MOTION-045): 'fade-through',
// 'shared-axis-x', 'shared-axis-y', 'shared-axis-z', 'container-transform'.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../lib/motion.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/motion.js lands.
  mod = {
    checkRoutePattern: () => ({ code: 'STUB' }),
  };
}
const { checkRoutePattern } = mod;

function loc(overrides = {}) {
  return {
    file: 'routes.tsx',
    line: 4,
    column: 3,
    nodePath: 'Routes > Route[0]',
    ...overrides,
  };
}

const KNOWN_PATTERNS = [
  'fade-through',
  'shared-axis-x',
  'shared-axis-y',
  'shared-axis-z',
  'container-transform',
];

// ---------------------------------------------------------------------------
// TC-TSX-032 → AC-TSX-032 — T9 contract:
// `checkRoutePattern(route: string, pattern: string, loc: Loc, knownPatterns: string[]): Diagnostic|null`
// ---------------------------------------------------------------------------
describe('TC-TSX-032 — known route patterns accepted, unknown rejected (AC-TSX-032)', () => {
  it('known pattern returns null (each of the 5 canonical names)', () => {
    for (const p of KNOWN_PATTERNS) {
      assert.equal(
        checkRoutePattern('/home', p, loc(), KNOWN_PATTERNS),
        null,
        `pattern ${p} must be accepted`
      );
    }
  });

  it('unknown pattern returns a diagnostic naming route + pattern', () => {
    const l = loc();
    const d = checkRoutePattern('/home', 'spin-zoom', l, KNOWN_PATTERNS);
    assert.notEqual(d, null, 'unknown pattern must return a diagnostic');
    assert.equal(d.code, 'TSX_UNKNOWN_PATTERN');
    assert.equal(d.route, '/home');
    assert.equal(d.pattern, 'spin-zoom');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
    assert.equal(d.nodePath, l.nodePath);
    assert.equal(d.severity, 'error');
  });

  it('caller-supplied list governs: pattern outside a custom list fails', () => {
    const l = loc();
    const d = checkRoutePattern('/home', 'fade-through', l, ['shared-axis-x']);
    assert.notEqual(d, null, 'pattern outside caller list must fail');
    assert.equal(d.code, 'TSX_UNKNOWN_PATTERN');
    assert.equal(d.route, '/home');
    assert.equal(d.pattern, 'fade-through');
  });

  it('never throws on empty known-pattern list (unknown → diagnostic)', () => {
    assert.doesNotThrow(() => checkRoutePattern('/home', 'fade-through', loc(), []));
    const d = checkRoutePattern('/home', 'fade-through', loc(), []);
    assert.notEqual(d, null);
    assert.equal(d.code, 'TSX_UNKNOWN_PATTERN');
  });
});
