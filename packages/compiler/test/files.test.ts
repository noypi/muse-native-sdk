'use strict';
// TC-TSX-001 (AC-TSX-001) + TC-TSX-002 (AC-TSX-002) — Batch 2 (T4; contracts T2/T3).
// Hermetic: node:test + node:assert/strict only. Inline `sourceText` strings;
// no network, no secrets, no live services, no fs fixtures.
// Style mirrors packages/cli/test/packages.test.js (describe/it + strict assert).
// Binds ONLY to the pinned T2/T3 `Interface:` lines, never to their implementation.
// Validator resolution: `../lib/files.js` once it lands, else the `../index.js`
// shim (currently `{}`), so every behavior case below fails until T2/T3 land.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let validators = {};
try {
  validators = require('../lib/files.js');
} catch {
  validators = require('../index.js');
}
const { validateFileConvention, validateSingleComponent } = validators;

const VALID_BUTTON = [
  'export default function Button() {',
  '  return null;',
  '}',
  '',
].join('\n');

const NO_DEFAULT_EXPORT = [
  'export function Button() {',
  '  return null;',
  '}',
  '',
].join('\n');

const TWO_COMPONENTS = [
  'export default function Card() {',
  '  return null;',
  '}',
  '',
  'export function CardFooter() {',
  '  return null;',
  '}',
  '',
].join('\n');

// ---------------------------------------------------------------------------
// TC-TSX-001 → AC-TSX-001 — T2 contract:
// `validateFileConvention(filePath: string, sourceText: string): string | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-001 — file convention (AC-TSX-001)', () => {
  it('accepts Button.tsx with a default export (returns null)', () => {
    assert.equal(validateFileConvention('Button.tsx', VALID_BUTTON), null);
  });

  it('rejects a non-PascalCase basename with the exact one-line error', () => {
    const err = validateFileConvention('button.tsx', VALID_BUTTON);
    assert.equal(typeof err, 'string', 'must return an error string');
    assert.ok(err.includes('button.tsx'), `error must name the file, got: ${err}`);
    assert.ok(
      err.includes('1 component per .tsx in a PascalCase file, got button.tsx'),
      `error must state the convention with the offending basename, got: ${err}`,
    );
    assert.ok(!err.includes('\n'), `error must be one line, got: ${err}`);
  });

  it('rejects a missing default export with got no default export', () => {
    const err = validateFileConvention('Button.tsx', NO_DEFAULT_EXPORT);
    assert.equal(typeof err, 'string', 'must return an error string');
    assert.ok(err.includes('Button.tsx'), `error must name the file, got: ${err}`);
    assert.ok(
      err.includes('1 component per .tsx in a PascalCase file, got no default export'),
      `error must state the convention with no default export, got: ${err}`,
    );
    assert.ok(!err.includes('\n'), `error must be one line, got: ${err}`);
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-002 → AC-TSX-002 — T3 contract:
// `validateSingleComponent(sourceText: string, filePath: string): string | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-002 — single component per file (AC-TSX-002)', () => {
  it('accepts a single-component file (returns null)', () => {
    assert.equal(validateSingleComponent(VALID_BUTTON, 'Card.tsx'), null);
  });

  it('rejects a two-component file naming the second component and its new file', () => {
    const err = validateSingleComponent(TWO_COMPONENTS, 'Card.tsx');
    assert.equal(typeof err, 'string', 'must return an error string');
    assert.ok(err.includes('Card.tsx'), `error must name the file, got: ${err}`);
    assert.ok(
      err.includes('1 component per .tsx, move CardFooter to CardFooter.tsx'),
      `error must name the second component and its target file, got: ${err}`,
    );
    assert.ok(!err.includes('\n'), `error must be one line, got: ${err}`);
  });

  it('accepts a zero-component file (returns null; missing export is T2 domain)', () => {
    assert.equal(validateSingleComponent('const x = 1;\n', 'Card.tsx'), null);
  });
});
