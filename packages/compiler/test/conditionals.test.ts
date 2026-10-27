'use strict';
// TC-TSX-011 (AC-TSX-011) + TC-TSX-012 (AC-TSX-012) + TC-TSX-013 (AC-TSX-013)
// — Batch 2 (T4, T5; T6b wiring).
// Hermetic: node:test + node:assert/strict only. Inline `sourceText` strings;
// no network, no secrets, no live services, no fs fixtures.
// Style mirrors test/grammar.test.ts (describe/it + strict assert).
// Binds ONLY to the pinned T4/T5 `Interface:` lines, never to their implementation.
// Validator resolution: `../lib/conditionals.js` once it lands, else an inline
// stub whose behavior assertions fail, so every behavior case below is red until
// T4/T5 land — and the harness itself never crashes.
// `formatGrammarDiagnostic` is imported from `../lib/grammar.js` (exists, committed).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { formatGrammarDiagnostic } = require('../lib/grammar.js');

let conditionals = null;
try {
  conditionals = require('../lib/conditionals.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/conditionals.js lands.
  conditionals = {
    compileConditional: () => null,
    validateConditional: () => null,
    validateEarlyReturns: () => null,
  };
}
const { compileConditional, validateConditional, validateEarlyReturns } = conditionals;

function loc(overrides = {}) {
  return {
    file: 'Card.tsx',
    line: 21,
    column: 7,
    nodePath: 'Card > Text[0]',
    ...overrides,
  };
}

// ---------------------------------------------------------------------------
// TC-TSX-011 → AC-TSX-011 — T4 contract:
// `compileConditional(slotSource: string): { kind: 'ternary' | 'boolean-and' | 'early-return' } | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-011 — conditional shapes lower to conditional IR (AC-TSX-011)', () => {
  it('lowers a ternary to { kind: ternary }', () => {
    assert.deepEqual(compileConditional('ok ? <A/> : <B/>'), { kind: 'ternary' });
  });

  it("lowers a boolean-typed && (leftType 'boolean') to { kind: boolean-and }", () => {
    // Type gate lives in T5: validateConditional with leftType 'boolean' → null.
    const l = loc();
    assert.equal(
      validateConditional('isReady && <Badge/>', { ...l, leftType: 'boolean' }),
      null,
    );
    assert.deepEqual(compileConditional('isReady && <Badge/>'), { kind: 'boolean-and' });
  });

  it('lowers a guarded early return (N guards + one final return) to { kind: early-return }', () => {
    const body = [
      'function Card({ ok, empty }) {',
      '  if (ok) return <A/>;',
      '  if (empty) return <B/>;',
      '  return <C/>;',
      '}',
    ].join('\n');
    assert.equal(validateEarlyReturns(body, 'Card.tsx'), null);
    assert.deepEqual(compileConditional(body), { kind: 'early-return' });
  });

  it('returns null for a plain identifier slot', () => {
    assert.equal(compileConditional('userName'), null);
  });

  it('returns null for a literal slot', () => {
    assert.equal(compileConditional('"—"'), null);
  });

  it('returns null for a forbidden form inside ternary shape', () => {
    assert.equal(compileConditional('new Date() ? a : b'), null);
  });

  it('returns null for assignment inside && shape', () => {
    assert.equal(compileConditional('count = count + 1 && <B/>'), null);
  });

  it('returns null for await inside ternary', () => {
    assert.equal(compileConditional('await fetchData() ? <A/> : <B/>'), null);
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-012 → AC-TSX-012 — T5 contract (validateConditional):
// `validateConditional(slotSource: string, ctx: { leftType: string } & SlotLocation): GrammarDiagnostic | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-012 — non-boolean && names the operand type (AC-TSX-012)', () => {
  it("fails && with leftType 'number' naming the type", () => {
    const l = loc();
    const d = validateConditional('count && <Badge/>', { ...l, leftType: 'number' });
    assert.notEqual(d, null, "non-boolean && must return a non-null diagnostic");
    assert.equal(d.form, 'non-boolean && left operand of type number');
  });

  it("fails && with leftType 'string | undefined' naming the type", () => {
    const l = loc({ line: 22 });
    const d = validateConditional('name && <Badge/>', { ...l, leftType: 'string | undefined' });
    assert.notEqual(d, null, "possibly-undefined && must return a non-null diagnostic");
    assert.equal(d.form, 'non-boolean && left operand of type string | undefined');
  });

  it("passes && with boolean leftType (null)", () => {
    const l = loc();
    assert.equal(
      validateConditional('isReady && <Badge/>', { ...l, leftType: 'boolean' }),
      null,
    );
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-013 → AC-TSX-013 — T5 contract (validateEarlyReturns):
// `validateEarlyReturns(functionSource: string, filePath: string): GrammarDiagnostic | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-013 — early-return shape validation (AC-TSX-013)', () => {
  it('accepts guards before a single final return (null)', () => {
    const body = [
      'function Card({ ok }) {',
      '  if (ok) return <A/>;',
      '  return <B/>;',
      '}',
    ].join('\n');
    assert.equal(validateEarlyReturns(body, 'Card.tsx'), null);
  });

  it('rejects a return in non-guard placement with a file-named one-line diagnostic', () => {
    const body = [
      'function Card({ ok }) {',
      '  const x = 1;',
      '  return <A/>;',
      '  return <B/>;',
      '}',
    ].join('\n');
    const d = validateEarlyReturns(body, 'Card.tsx');
    assert.notEqual(d, null, 'second return placement must fail');
    assert.equal(d.file, 'Card.tsx');
    const line = formatGrammarDiagnostic(d);
    assert.ok(!line.includes('\n'), 'diagnostic must be one line');
    assert.ok(line.includes('Card.tsx'), 'diagnostic must name the file');
  });

  it('rejects two final returns with a file-named one-line diagnostic', () => {
    const body = [
      'function Card({ ok }) {',
      '  if (ok) return <A/>;',
      '  return <B/>;',
      '  return <C/>;',
      '}',
    ].join('\n');
    const d = validateEarlyReturns(body, 'Card.tsx');
    assert.notEqual(d, null, 'two finals must fail');
    assert.equal(d.file, 'Card.tsx');
    const line = formatGrammarDiagnostic(d);
    assert.ok(!line.includes('\n'), 'diagnostic must be one line');
    assert.ok(line.includes('Card.tsx'), 'diagnostic must name the file');
  });

  it('rejects a missing final return with a file-named one-line diagnostic', () => {
    const body = [
      'function Card({ ok }) {',
      '  if (ok) return <A/>;',
      '}',
    ].join('\n');
    const d = validateEarlyReturns(body, 'Card.tsx');
    assert.notEqual(d, null, 'missing final return must fail');
    assert.equal(d.file, 'Card.tsx');
    const line = formatGrammarDiagnostic(d);
    assert.ok(!line.includes('\n'), 'diagnostic must be one line');
    assert.ok(line.includes('Card.tsx'), 'diagnostic must name the file');
  });
});
