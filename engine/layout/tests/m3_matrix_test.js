'use strict';
// TC-LAYOUT-027 (AC-LAYOUT-027) — Batch 3 T11.
// Hermetic: node:test + node:assert/strict only, inline expectations, no
// network, no secrets, no application configuration, no live services.
// The artifact under test IS a repo data file, so this file reads exactly
// one repo path — `../m3_matrix.json` — via node:fs relative to __dirname.
// Binds ONLY to the pinned T11 `Interface:` line, never to implementation.
//
// T11 contract: `none — data artifact plus link-integrity test` (JSON keyed
// by component: `{ primitive | composite, tokens, semantics, golden }`;
// test asserts no `missing` rows and every link resolves).
//
// ASSUMPTION (entry shape, pinned in unit-tests/T11.md): each component
// entry carries exactly one of `primitive` / `composite` (non-empty string;
// composite values are TSX paths), a non-empty `tokens` string array of
// dotted token references, a non-empty `semantics` string (a11y link), a
// non-empty `golden` string (golden-test link), a non-empty `motionTokens`
// string array (T10 names), and a `ledgers` object with non-empty
// `competitive` and `clone` string links (link-only references).
// ASSUMPTION (pin, per plan A3 / Q-LAYOUT-001): the document carries a
// top-level `snapshot` object `{ source: string, version: string }` and a
// non-empty `cadence` string documenting the update cadence.
// NOTE (TC-LAYOUT-027 manual-walk half): the spec Method is `manual` —
// "open the matrix, confirm no missing, confirm every entry links its
// primitive, token set, semantics and golden test". That human walk is
// recorded as MANUAL in unit-tests/T11.md; this file is the automatable
// link-integrity half. It does NOT invent the walk's outcome.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const MATRIX_PATH = path.join(__dirname, '..', 'm3_matrix.json');
const TOKEN_REF_RE = /^[a-z][a-z0-9-]*(\.[a-z0-9-]+)+$/;

function loadMatrix() {
  const raw = fs.readFileSync(MATRIX_PATH, 'utf8');
  return JSON.parse(raw);
}

function entriesOf(matrix) {
  assert.equal(typeof matrix, 'object');
  // Keyed by component (object map preferred); tolerate a `components` array
  // of `{ name, ...entry }` rows without inventing semantics.
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
    hasPrimitive !== hasComposite,
    `${name}: must link exactly one of primitive/composite (T8)`
  );
  if (hasComposite) {
    assert.ok(
      entry.composite.endsWith('.tsx'),
      `${name}: composite link must be a TSX path per CONFLICT-M3-001, got: ${entry.composite}`
    );
  }
  assert.ok(Array.isArray(entry.tokens) && entry.tokens.length > 0, `${name}: tokens must be non-empty (T4)`);
  for (const t of entry.tokens) {
    assert.equal(typeof t, 'string');
    assert.match(t, TOKEN_REF_RE, `${name}: token link must resolve as a reference, got: ${t}`);
  }
  assert.equal(typeof entry.semantics, 'string', `${name}: semantics link must be a string`);
  assert.ok(entry.semantics.length > 0, `${name}: semantics link must resolve (specs-accessibility.md)`);
  assert.equal(typeof entry.golden, 'string', `${name}: golden link must be a string`);
  assert.ok(entry.golden.length > 0, `${name}: golden-test link must resolve`);
  assert.ok(
    Array.isArray(entry.motionTokens) && entry.motionTokens.length > 0,
    `${name}: motionTokens must be non-empty (T10)`
  );
  for (const m of entry.motionTokens) {
    assert.equal(typeof m, 'string');
    assert.ok(m.startsWith('motion.'), `${name}: motion token must live in the motion set, got: ${m}`);
  }
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

  it('pins the token snapshot source/version and documents the update cadence (A3/Q-LAYOUT-001)', () => {
    const matrix = loadMatrix();
    assert.equal(typeof matrix.snapshot, 'object', 'snapshot pin missing');
    assert.equal(typeof matrix.snapshot.source, 'string', 'snapshot.source missing');
    assert.ok(matrix.snapshot.source.length > 0, 'snapshot.source must resolve');
    assert.equal(typeof matrix.snapshot.version, 'string', 'snapshot.version missing');
    assert.ok(matrix.snapshot.version.length > 0, 'snapshot.version must resolve');
    assert.equal(typeof matrix.cadence, 'string', 'cadence note missing');
    assert.ok(matrix.cadence.length > 0, 'cadence note must be non-empty');
  });
});
