'use strict';
// TC-TSX-019 (AC-TSX-019) + TC-TSX-020 (AC-TSX-020) — Batch 3 (T8–T9).
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Style mirrors test/lists.test.ts (describe/it + strict assert).
// Binds ONLY to the pinned T8–T9 `Interface:` lines, never to implementation.
// Validator resolution: `../lib/style.js` once it lands, else an inline
// fallback stub whose behavior assertions fail, so every behavior case below
// is red until lib/style.js lands — and the harness itself never crashes.
// Known-list injection (plan A4): the test passes `known` explicitly —
// no CSS parsing in the compiler, no fs reads in tests.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let style = null;
try {
  style = require('../lib/style.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/style.js lands.
  style = {
    validateInlineStyle: () => ({ file: 'stub', line: -1, column: -1, nodePath: 'stub', form: 'stub', fix: 'stub' }),
    resolveClassNames: () => ({ file: 'stub', line: -1, column: -1, nodePath: 'stub', form: 'stub', fix: 'stub' }),
  };
}
const { validateInlineStyle, resolveClassNames } = style;

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
// TC-TSX-019 → AC-TSX-019 — T8 contract:
// `validateInlineStyle(style: Record<string, unknown>, loc: SlotLocation): BuiltinDiagnostic | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-019 — inline style keys camelCase, values number(dp)|string (AC-TSX-019)', () => {
  it('accepts camelCase keys with string and number(dp) values (null)', () => {
    assert.equal(
      validateInlineStyle({ backgroundColor: 'red', marginTop: 8 }, loc()),
      null,
    );
  });

  it('fails a kebab-case key with a diagnostic naming background-color', () => {
    const l = loc();
    const d = validateInlineStyle({ 'background-color': 'red' }, l);
    assert.notEqual(d, null, 'kebab-case key must return a diagnostic');
    assert.ok(d.form.includes('background-color'), 'diagnostic form must name `background-color`');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('fails a boolean value with a diagnostic naming opacity', () => {
    const l = loc();
    const d = validateInlineStyle({ opacity: true }, l);
    assert.notEqual(d, null, 'boolean value must return a diagnostic');
    assert.ok(d.form.includes('opacity'), 'diagnostic form must name `opacity`');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('fails an object value with a diagnostic naming the key', () => {
    const d = validateInlineStyle({ padding: { top: 8 } }, loc());
    assert.notEqual(d, null, 'object value must return a diagnostic');
    assert.ok(d.form.includes('padding'), 'diagnostic form must name `padding`');
  });

  it('accepts an empty style object (null)', () => {
    assert.equal(validateInlineStyle({}, loc()), null);
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-020 → AC-TSX-020 — T9 contract:
// `resolveClassNames(classes: string[], ctx: { component: string, stylesheet: string, known: string[] }): BuiltinDiagnostic | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-020 — class names resolved at build vs co-located stylesheet (AC-TSX-020)', () => {
  it('accepts all-known classes (null)', () => {
    assert.equal(
      resolveClassNames(['btn', 'btn-primary'], {
        component: 'Login',
        stylesheet: 'Login.css',
        known: ['btn', 'btn-primary'],
      }),
      null,
    );
  });

  it('fails an unknown class with a diagnostic naming nope + Login + Login.css', () => {
    const d = resolveClassNames(['btn', 'nope'], {
      component: 'Login',
      stylesheet: 'Login.css',
      known: ['btn', 'btn-primary'],
    });
    assert.notEqual(d, null, 'unknown class must return a diagnostic');
    assert.ok(d.form.includes('nope'), 'diagnostic form must name `nope`');
    assert.ok(d.form.includes('Login'), 'diagnostic form must name `Login`');
    assert.ok(d.form.includes('Login.css'), 'diagnostic form must name `Login.css`');
  });

  it('fails all-unknown classes with a non-null diagnostic', () => {
    const d = resolveClassNames(['ghost', 'phantom'], {
      component: 'Login',
      stylesheet: 'Login.css',
      known: ['btn'],
    });
    assert.notEqual(d, null, 'all-unknown classes must return a diagnostic');
    assert.ok(d.form.includes('Login'), 'diagnostic form must name `Login`');
    assert.ok(d.form.includes('Login.css'), 'diagnostic form must name `Login.css`');
  });

  it('accepts an empty class list (null)', () => {
    assert.equal(
      resolveClassNames([], { component: 'Login', stylesheet: 'Login.css', known: ['btn'] }),
      null,
    );
  });
});
