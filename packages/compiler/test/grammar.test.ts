'use strict';
// TC-TSX-008 (AC-TSX-008) + TC-TSX-009 (AC-TSX-009) + TC-TSX-010 (AC-TSX-010)
// — Batch 1 (T1, T2, T3; T6a wiring).
// Hermetic: node:test + node:assert/strict only. Inline `sourceText` strings;
// no network, no secrets, no live services, no fs fixtures.
// Style mirrors packages/compiler/test/files.test.ts (describe/it + strict assert).
// Binds ONLY to the pinned T1/T2/T3 `Interface:` lines, never to their implementation.
// Validator resolution: `../lib/grammar.js` once it lands, else an inline stub
// whose behavior assertions fail, so every behavior case below is red until
// T1/T2/T3 land — and the harness itself never crashes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let grammar = null;
try {
  grammar = require('../lib/grammar.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/grammar.js lands.
  grammar = {
    validateExpressionSlot: () => null,
    compileExpressionSlot: () => null,
    liftPureSlot: () => ({ id: 'missing', computedSource: 'missing' }),
    listLiftedNodeIds: () => [],
    formatGrammarDiagnostic: () => '',
  };
}
const {
  validateExpressionSlot,
  compileExpressionSlot,
  liftPureSlot,
  listLiftedNodeIds,
  formatGrammarDiagnostic,
} = grammar;

const CALL_FIX =
  'put the call in computed(() => ...) and bind the result, or add /** @pure */ above the callee if it only reads its arguments';
const MOVE_FIX = 'move this logic into computed(() => ...) and bind the result';

function loc(overrides = {}) {
  return {
    file: 'Card.tsx',
    line: 12,
    column: 9,
    nodePath: 'Card > Text[0]',
    ...overrides,
  };
}

// ---------------------------------------------------------------------------
// TC-TSX-009 → AC-TSX-009 (+ AC-TSX-010 diagnostic shape) — T1 contract:
// `validateExpressionSlot(slotSource: string, loc: SlotLocation): GrammarDiagnostic | null`
// `formatGrammarDiagnostic(d: GrammarDiagnostic): string` returning exactly
// `${d.file}:${d.line}:${d.column}: ${d.form} at ${d.nodePath}. Fix: ${d.fix}`.
// Non-zero-exit semantics: the validator returns a diagnostic, never throws.
// ---------------------------------------------------------------------------
describe('TC-TSX-009 — forbidden expression forms are rejected (AC-TSX-009)', () => {
  it('rejects assignment with the exact one-line diagnostic', () => {
    const l = loc();
    let d = null;
    assert.doesNotThrow(() => {
      d = validateExpressionSlot('count = count + 1', l);
    }, 'validator must return a diagnostic, never throw (non-zero-exit semantics)');
    assert.notEqual(d, null, 'assignment must return a non-null diagnostic');
    assert.equal(d.form, 'assignment');
    assert.equal(d.fix, MOVE_FIX);
    assert.equal(
      formatGrammarDiagnostic(d),
      `${l.file}:${l.line}:${l.column}: assignment at ${l.nodePath}. Fix: ${MOVE_FIX}`,
    );
    assert.ok(!formatGrammarDiagnostic(d).includes('\n'), 'diagnostic must be one line');
  });

  it('rejects `new` with the exact one-line diagnostic', () => {
    const l = loc({ line: 13, column: 14 });
    const d = validateExpressionSlot('new Date()', l);
    assert.notEqual(d, null, '`new` must return a non-null diagnostic');
    assert.equal(d.form, 'new expression');
    assert.equal(d.fix, MOVE_FIX);
    assert.equal(
      formatGrammarDiagnostic(d),
      `${l.file}:${l.line}:${l.column}: new expression at ${l.nodePath}. Fix: ${MOVE_FIX}`,
    );
  });

  it('rejects `await` with the exact one-line diagnostic', () => {
    const l = loc({ line: 14 });
    const d = validateExpressionSlot('await fetchData()', l);
    assert.notEqual(d, null, '`await` must return a non-null diagnostic');
    assert.equal(d.form, 'await');
    assert.equal(d.fix, MOVE_FIX);
    assert.equal(
      formatGrammarDiagnostic(d),
      `${l.file}:${l.line}:${l.column}: await at ${l.nodePath}. Fix: ${MOVE_FIX}`,
    );
  });

  it('rejects a statement in an expression with the exact one-line diagnostic', () => {
    const l = loc({ line: 15 });
    const d = validateExpressionSlot('if (ok) { show(); }', l);
    assert.notEqual(d, null, 'statement-in-expression must return a non-null diagnostic');
    assert.equal(d.form, 'statement in expression');
    assert.equal(d.fix, MOVE_FIX);
    assert.equal(
      formatGrammarDiagnostic(d),
      `${l.file}:${l.line}:${l.column}: statement in expression at ${l.nodePath}. Fix: ${MOVE_FIX}`,
    );
  });

  it('rejects an unannotated call naming the callee with the computed-or-annotate fix', () => {
    const l = loc({ line: 16 });
    const d = validateExpressionSlot('doThing(a)', l);
    assert.notEqual(d, null, 'unannotated call must return a non-null diagnostic');
    assert.equal(d.form, 'unannotated call to doThing');
    assert.equal(d.fix, CALL_FIX);
    assert.equal(
      formatGrammarDiagnostic(d),
      `${l.file}:${l.line}:${l.column}: unannotated call to doThing at ${l.nodePath}. Fix: ${CALL_FIX}`,
    );
  });

  it('rejects a side-effecting call with the computed-or-annotate fix', () => {
    const l = loc({ line: 17 });
    const d = validateExpressionSlot('console.log(x)', l);
    assert.notEqual(d, null, 'side-effecting call must return a non-null diagnostic');
    assert.ok(
      d.form.includes('console.log'),
      `form must name the callee, got: ${d.form}`,
    );
    assert.equal(d.fix, CALL_FIX);
    assert.equal(
      formatGrammarDiagnostic(d),
      `${l.file}:${l.line}:${l.column}: ${d.form} at ${l.nodePath}. Fix: ${CALL_FIX}`,
    );
  });

  it('rejects a dynamic tag name with the exact one-line diagnostic', () => {
    const l = loc({ line: 18 });
    const d = validateExpressionSlot('<TagName />', l);
    assert.notEqual(d, null, 'dynamic tag name must return a non-null diagnostic');
    assert.equal(d.form, 'dynamic tag name');
    assert.equal(d.fix, MOVE_FIX);
    assert.equal(
      formatGrammarDiagnostic(d),
      `${l.file}:${l.line}:${l.column}: dynamic tag name at ${l.nodePath}. Fix: ${MOVE_FIX}`,
    );
  });

  it('returns null for a clean identifier slot (no forbidden form)', () => {
    assert.equal(validateExpressionSlot('userName', loc()), null);
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-008 → AC-TSX-008 — T2 contract:
// `compileExpressionSlot(slotSource: string): { kind: ExpressionKind, pure: boolean } | null`
// where `ExpressionKind = 'identifier' | 'member' | 'literal' | 'ternary' |
// 'logical' | 'comparison' | 'arithmetic' | 'pure-call' | 'map'`;
// `pure` is true only for `pure-call`. Returns `null` for any T1-rejected slot.
// ---------------------------------------------------------------------------
describe('TC-TSX-008 — allowed expression forms lower to IR (AC-TSX-008)', () => {
  it('lowers an identifier to { kind: identifier, pure: false }', () => {
    assert.deepEqual(compileExpressionSlot('userName'), { kind: 'identifier', pure: false });
  });

  it('lowers member access to { kind: member, pure: false }', () => {
    assert.deepEqual(compileExpressionSlot('a.value'), { kind: 'member', pure: false });
  });

  it('lowers a literal to { kind: literal, pure: false }', () => {
    assert.deepEqual(compileExpressionSlot('"—"'), { kind: 'literal', pure: false });
  });

  it('lowers a ternary to { kind: ternary, pure: false }', () => {
    assert.deepEqual(compileExpressionSlot('ok ? a : b'), { kind: 'ternary', pure: false });
  });

  it('lowers && to { kind: logical, pure: false }', () => {
    assert.deepEqual(compileExpressionSlot('ok && label'), { kind: 'logical', pure: false });
  });

  it('lowers || to { kind: logical, pure: false }', () => {
    assert.deepEqual(compileExpressionSlot('a || b'), { kind: 'logical', pure: false });
  });

  it('lowers a comparison to { kind: comparison, pure: false }', () => {
    assert.deepEqual(compileExpressionSlot('count > 0'), { kind: 'comparison', pure: false });
  });

  it('lowers arithmetic to { kind: arithmetic, pure: false }', () => {
    assert.deepEqual(compileExpressionSlot('a + b * 2'), { kind: 'arithmetic', pure: false });
  });

  it('lowers a /** @pure */ call to { kind: pure-call, pure: true }', () => {
    assert.deepEqual(compileExpressionSlot('/** @pure */ myFormat(a.value)'), {
      kind: 'pure-call',
      pure: true,
    });
  });

  it('lowers the canonical single-JSX .map() to { kind: map, pure: false }', () => {
    assert.deepEqual(compileExpressionSlot('items.map((item) => <Row key={item.id} />)'), {
      kind: 'map',
      pure: false,
    });
  });

  it('returns null for every T1-forbidden form instead of an IR node', () => {
    const l = loc();
    const forbidden = [
      'count = count + 1',
      'new Date()',
      'await fetchData()',
      'if (ok) { show(); }',
      'doThing(a)',
      'console.log(x)',
      '<TagName />',
    ];
    for (const slot of forbidden) {
      assert.notEqual(
        validateExpressionSlot(slot, l),
        null,
        `precondition: T1 must reject ${slot}`,
      );
      assert.equal(
        compileExpressionSlot(slot),
        null,
        `forbidden slot must emit nothing, got IR for: ${slot}`,
      );
    }
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-010 → AC-TSX-010 — T3 contract:
// `liftPureSlot(slotSource: string, calleeName: string): { id: string, computedSource: string }`
// `listLiftedNodeIds(): string[]` — sorted, deduplicated.
// ---------------------------------------------------------------------------
describe('TC-TSX-010 — pure-call lifting to stable computed nodes (AC-TSX-010)', () => {
  it('lifts a slot to a __c_<hash> id with the exact computed source', () => {
    const slot = 'ok ? myFormat(a.value) : "—"';
    const lifted = liftPureSlot(slot, 'myFormat');
    assert.ok(
      /^__c_[0-9a-f]{8}$/.test(lifted.id),
      `id must match /^__c_[0-9a-f]{8}$/, got: ${lifted.id}`,
    );
    assert.equal(lifted.computedSource, `computed(() => ${slot})`);
  });

  it('is content-stable: the same source yields the same id', () => {
    const slot = 'ok ? myFormat(a.value) : "—"';
    assert.equal(liftPureSlot(slot, 'myFormat').id, liftPureSlot(slot, 'myFormat').id);
  });

  it('gives distinct ids for different sources', () => {
    const a = liftPureSlot('ok ? myFormat(a.value) : "—"', 'myFormat').id;
    const b = liftPureSlot('otherFormat(a.value)', 'otherFormat').id;
    assert.notEqual(a, b, 'different sources must lift to different ids');
  });

  it('lists lifted ids sorted and deduplicated', () => {
    const a = liftPureSlot('zeta(a)', 'zeta').id;
    const b = liftPureSlot('alpha(a)', 'alpha').id;
    liftPureSlot('zeta(a)', 'zeta');
    const ids = listLiftedNodeIds();
    assert.ok(ids.includes(a), `registry must contain ${a}`);
    assert.ok(ids.includes(b), `registry must contain ${b}`);
    assert.deepEqual(
      ids,
      [...new Set(ids)].sort(),
      `ids must be sorted and deduplicated, got: ${JSON.stringify(ids)}`,
    );
  });
});
