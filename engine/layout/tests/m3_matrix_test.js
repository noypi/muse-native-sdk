'use strict';
// TC-LAYOUT-027 (AC-LAYOUT-027) — BUG-LAYOUT-002 Batch 1 (T1+T2).
// Hermetic: node:test + node:assert/strict only, inline expectations, no
// network, no secrets, no application configuration, no live services.
// The artifact under test IS a repo data file, so this file reads exactly
// one repo path — `../m3_matrix.json` — via node:fs relative to __dirname.
// Binds ONLY to the Batch 1 `Interface:` lines, never to implementation.
//
// Envelope contract (T9-canonical, restored by BUG-LAYOUT-001): the document
// is `{ schemaVersion: 1, tokenSnapshot: string, components: [{ name,
// primitive?, composite?, tokens, states[7], adaptive:bool, semantics,
// goldenTest, status:"present", ledgers:{clone,competitive} }] }`.
// The flat component-map shape (`{ primitive | composite, tokens, semantics,
// golden }` + top-level `snapshot`/`cadence`) is rejected — see the C++ gate
// `flat-map-no-envelope` case (m3_matrix_check.cpp lines 477-488).
//
// ASSUMPTION (entry shape, live-matrix survey 2026-10-28): 31/31 rows carry
// `primitive`, 14/31 additionally carry `composite` (`.tsx` paths), 0/31 carry
// `motionTokens` or bare `golden`, 31/31 carry `goldenTest`,
// `ledgers:{clone,competitive}`, 7-element `states`, boolean `adaptive`,
// `status:"present"` — so the old primitive/composite XOR is unassertable
// and the row contract requires at-least-one of primitive/composite.
// ASSUMPTION (pin, per plan A2): `cadence` has no home in the envelope and
// Q-LAYOUT-001/A3 `snapshot`/`cadence` is superseded by `tokenSnapshot` for
// test purposes — this file asserts `schemaVersion === 1` + non-empty
// `tokenSnapshot` and drops `cadence`.
// NOTE (TC-LAYOUT-027 manual-walk half): the spec Method is `manual` —
// "open the matrix, confirm no missing, confirm every entry links its
// primitive, token set, semantics and golden test". That human walk is
// recorded as MANUAL in unit-tests/T1.md; this file is the automatable
// link-integrity half. It does NOT invent the walk's outcome.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const MATRIX_PATH = path.join(__dirname, '..', 'm3_matrix.json');
const TOKEN_REF_RE = /^[a-z][a-z0-9-]*(\.[a-z0-9-]+)+$/;
const EXPECTED_STATES = ['enabled', 'hovered', 'focused', 'pressed', 'dragged', 'disabled', 'error'];

function loadMatrix() {
  const raw = fs.readFileSync(MATRIX_PATH, 'utf8');
  return JSON.parse(raw);
}

function entriesOf(matrix) {
  assert.equal(typeof matrix, 'object');
  // Envelope shape: `components` array of `{ name, ...entry }` rows.
  // NOTE: the legacy `snapshot`/`cadence` key filter on the flat-map branch
  // below is dead code under the envelope (neither key exists); kept harmless
  // per plan A4 with no behavior change.
  if (matrix.components !== undefined) {
    assert.ok(Array.isArray(matrix.components), '`components` must be an array when present');
    return matrix.components.map((row) => {
      assert.equal(typeof row.name, 'string', 'every matrix row must name its component');
      return [row.name, row];
    });
  }
  return Object.entries(matrix).filter(([k]) => k !== 'snapshot' && k !== 'cadence');
}

function assertEntryLinks(name, entry) {
  const hasPrimitive = typeof entry.primitive === 'string' && entry.primitive.length > 0;
  const hasComposite = typeof entry.composite === 'string' && entry.composite.length > 0;
  assert.ok(
    hasPrimitive || hasComposite,
    `${name}: must link at least one of primitive/composite (envelope row contract)`
  );
  if (hasComposite) {
    assert.ok(
      entry.composite.endsWith('.tsx'),
      `${name}: composite link must be a TSX path per live data, got: ${entry.composite}`
    );
  }
  assert.ok(Array.isArray(entry.tokens) && entry.tokens.length > 0, `${name}: tokens must be non-empty (T4)`);
  for (const t of entry.tokens) {
    assert.equal(typeof t, 'string');
    assert.match(t, TOKEN_REF_RE, `${name}: token link must resolve as a reference, got: ${t}`);
  }
  assert.ok(
    Array.isArray(entry.states) && entry.states.length === 7,
    `${name}: states must be the 7-element state set (FR-LAYOUT-010/TC-LAYOUT-025)`
  );
  assert.deepEqual([...entry.states].sort(), [...EXPECTED_STATES].sort(), `${name}: states must cover the canonical 7 states`);
  assert.equal(typeof entry.adaptive, 'boolean', `${name}: adaptive must be a boolean`);
  assert.equal(typeof entry.semantics, 'string', `${name}: semantics link must be a string`);
  assert.ok(entry.semantics.length > 0, `${name}: semantics link must resolve (specs-accessibility.md)`);
  assert.equal(typeof entry.goldenTest, 'string', `${name}: goldenTest link must be a string`);
  assert.ok(entry.goldenTest.length > 0, `${name}: golden-test link must resolve`);
  assert.equal(entry.status, 'present', `${name}: status must be "present"`);
  assert.equal(typeof entry.ledgers, 'object', `${name}: ledgers must be an object`);
  assert.equal(typeof entry.ledgers.competitive, 'string', `${name}: competitive ledger link missing`);
  assert.ok(entry.ledgers.competitive.length > 0, `${name}: competitive ledger link must resolve`);
  assert.equal(typeof entry.ledgers.clone, 'string', `${name}: clone ledger link missing`);
  assert.ok(entry.ledgers.clone.length > 0, `${name}: clone ledger link must resolve`);
}

describe('TC-LAYOUT-027 — completeness matrix has no gaps and every link resolves (AC-LAYOUT-027)', () => {
  it('publishes a parseable matrix artifact', () => {
    const matrix = loadMatrix();
    assert.equal(typeof matrix, 'object');
  });

  it('contains at least one component entry', () => {
    const entries = entriesOf(loadMatrix());
    assert.ok(entries.length > 0, 'matrix must contain component entries');
  });

  it('contains no component marked missing', () => {
    const entries = entriesOf(loadMatrix());
    for (const [name, entry] of entries) {
      assert.notEqual(entry.status, 'missing', `${name}: marked missing`);
      assert.ok(!/missing/i.test(name), `${name}: name suggests a missing row`);
    }
    const serialized = JSON.stringify(entries);
    assert.ok(!/"status"\s*:\s*"missing"/.test(serialized), 'matrix contains a missing row');
  });

  it('every entry links primitive/composite, tokens, semantics and golden test', () => {
    const entries = entriesOf(loadMatrix());
    for (const [name, entry] of entries) {
      assertEntryLinks(name, entry);
    }
  });

  it('pins schemaVersion/tokenSnapshot (envelope; supersedes A3 snapshot/cadence)', () => {
    const matrix = loadMatrix();
    assert.equal(matrix.schemaVersion, 1, 'schemaVersion pin missing');
    assert.equal(typeof matrix.tokenSnapshot, 'string', 'tokenSnapshot pin missing');
    assert.ok(matrix.tokenSnapshot.length > 0, 'tokenSnapshot must resolve');
  });
});
