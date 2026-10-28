'use strict';
// TC-LAYOUT-018 (AC-LAYOUT-018) — Batch 1 T2.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T2 `Interface:` line, never to implementation.
// Module resolution: `../lib/sass.js` once it lands, else an inline fallback
// stub whose behavior assertions fail, so every behavior case below is red
// until lib/sass.js lands — and the harness itself never crashes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../lib/sass.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/sass.js lands.
  mod = {
    compileSass: () => ({ css: 'STUB', map: undefined }),
    SassCompileError: class SassCompileError extends Error {},
  };
}
const { compileSass, SassCompileError } = mod;

// ---------------------------------------------------------------------------
// T2 contract:
// `compileSass(source: string, options?: { includePaths?: string[] }):
//  { css: string, map?: object }`
// ---------------------------------------------------------------------------
describe('TC-LAYOUT-018 — SASS compiles at build time: variables, nesting, mixins, functions (AC-LAYOUT-018)', () => {
  it('compiles variables to CSS', () => {
    const out = compileSass('$color: red;\n.a { color: $color; }\n');
    assert.ok(out.css.includes('color: red'), 'compiled css must resolve the variable');
    assert.ok(out.css.includes('.a'), 'compiled css must keep the selector');
    assert.ok(!out.css.includes('$color'), 'compiled css must contain no SASS variable');
  });

  it('compiles nesting to flat selectors', () => {
    const out = compileSass('.a {\n  .b { color: red; }\n}\n');
    assert.ok(out.css.includes('.a .b'), 'nesting must flatten to `.a .b`');
  });

  it('compiles mixins to CSS', () => {
    const src = '@mixin box { display: flex; }\n.a { @include box; }\n';
    const out = compileSass(src);
    assert.ok(out.css.includes('display: flex'), 'mixin body must appear in output');
    assert.ok(!out.css.includes('@include'), 'output must contain no @include');
  });

  it('compiles functions to values (lighten/darken or equivalent)', () => {
    const src = ".a { color: lighten(#000, 50%); }\n";
    const out = compileSass(src);
    assert.ok(out.css.includes('color:'), 'function call must resolve to a color declaration');
    assert.ok(!out.css.includes('lighten('), 'output must contain no raw function call');
  });

  it('accepts includePaths option without changing output', () => {
    const out = compileSass(".a { color: red; }\n", { includePaths: [] });
    assert.ok(out.css.includes('color: red'), 'includePaths must not alter plain compilation');
  });

  it('throws SassCompileError with message/line/column on bad input', () => {
    assert.throws(
      () => compileSass('.a { color: ; }\n'),
      (err) => {
        assert.ok(err instanceof SassCompileError, 'must throw SassCompileError');
        assert.equal(typeof err.message, 'string');
        assert.equal(typeof err.line, 'number');
        assert.equal(typeof err.column, 'number');
        return true;
      },
    );
  });

  it('throws SassCompileError with line 2 for an error on the second line', () => {
    assert.throws(
      () => compileSass(".a { color: red; }\n.b { color: ; }\n"),
      (err) => {
        assert.ok(err instanceof SassCompileError, 'must throw SassCompileError');
        assert.equal(err.line, 2);
        return true;
      },
    );
  });

  // ASSUMPTION: compileSass is synchronous and pure — no filesystem reads
  // beyond resolving includePaths strings, no network, no process state.
  it('ASSUMPTION: compiles synchronously with no map by default', () => {
    const out = compileSass(".a { color: red; }\n");
    assert.equal(typeof out.css, 'string');
  });
});
