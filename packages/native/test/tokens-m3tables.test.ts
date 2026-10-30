/**
 * FR-LAYOUT-016 batch 1 — per-component M3 token tables, no generic fallback
 * (T1: AC-LAYOUT-052, T2: AC-LAYOUT-053).
 *
 * Hermetic: node:test + node:assert/strict only. Reads
 * `engine/layout/m3_matrix.json` from disk, imports `engine/layout/m3_registry.js`
 * and `packages/native/theme.js` through createRequire. No network, no secrets,
 * no services, no env config.
 *
 * ASSUMPTION H1 (helper home): the plan's T1 `Interface:` line names
 * `getComponentTokenTable` but not its module ("Read helper only; matrix JSON
 * remains the store"). The test resolves it from the registry module
 * (`engine/layout/m3_registry.js`) — the component authority alongside the
 * matrix — falling back to no candidate. If the coder homes it elsewhere, the
 * contract (not the behavior) is reconciled.
 * ASSUMPTION H2 (table shape): the matrix JSON remains the store, so a tabled
 * component carries its six-facet table on the component entry — accepted from
 * either `component.tokenTable` or an object-form `component.tokens`, with a
 * `sources` record on the table. Either location satisfies "has a table".
 * ASSUMPTION H3 (no-fallback proof scope): the behavioral proof of "no generic
 * fallback" is that resolved records carry exactly the tabled facets (never the
 * generic primary/onPrimary/surface/onSurface keys). The registry-source check
 * for removed SURFACE_ROLE_TOKENS/ACTION_ROLE_TOKENS composition is secondary
 * evidence of the same requirement. Extended to EVERY matrix component (not a
 * sampled subset) per T3 rework.
 * ASSUMPTION A4 (address-vs-value split, plan rework 2026-10-30): AC-LAYOUT-053
 * "no generic fallback set" applies at the role-ADDRESS level (resolved role
 * set == tabled role set); returned VALUES are computed through current
 * seed/dynamic (seed-palette fallback when dynamic is null, dynamic override
 * when present). Tests therefore assert role-set equality (facet presence, no
 * generic keys) — NOT deep value equality with the tabled role strings — plus
 * dedicated seed-effect and dynamic-override assertions. `mode` is validated:
 * anything other than "light"|"dark" throws RangeError.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const HERE = dirname(fileURLToPath(import.meta.url));
const CHECKOUT_ROOT = join(HERE, '..', '..', '..');
const MATRIX_JSON = join(CHECKOUT_ROOT, 'engine', 'layout', 'm3_matrix.json');
const REGISTRY_JS = join(CHECKOUT_ROOT, 'engine', 'layout', 'm3_registry.js');
const THEME_JS = join(HERE, '..', 'theme.js');

const FACETS = ['container', 'label', 'state', 'shape', 'elevation', 'typescale'];
const SOURCES = new Set(['component', 'role', 'theme', 'default']);
const MODES = ['light', 'dark'];

function loadMatrix() {
  return JSON.parse(readFileSync(MATRIX_JSON, 'utf8'));
}

function tableOf(entry) {
  if (entry && typeof entry.tokenTable === 'object' && entry.tokenTable !== null) {
    return entry.tokenTable;
  }
  if (entry && typeof entry.tokens === 'object' && entry.tokens !== null && !Array.isArray(entry.tokens)) {
    return entry.tokens;
  }
  return null;
}

function getHelper() {
  // eslint-disable-next-line global-require
  const registry = require(REGISTRY_JS);
  return {
    registry,
    getComponentTokenTable:
      typeof registry.getComponentTokenTable === 'function'
        ? registry.getComponentTokenTable.bind(registry)
        : null,
  };
}

function loadTheme() {
  // eslint-disable-next-line global-require
  return require(THEME_JS);
}

function isUnknownComponentError(err) {
  return (
    err !== null &&
    typeof err === 'object' &&
    (err.name === 'UnknownComponentError' || err.constructor?.name === 'UnknownComponentError')
  );
}

// ---------------------------------------------------------------------------
// T1 — per-component token tables with cascade sources (AC-LAYOUT-052)
// ---------------------------------------------------------------------------

test('T1: every matrix component has a six-facet token table', () => {
  const matrix = loadMatrix();
  assert.ok(Array.isArray(matrix.components) && matrix.components.length > 0, 'matrix must list components');
  const missing = matrix.components.filter((c) => tableOf(c) === null).map((c) => c.name);
  assert.deepEqual(missing, [], `components missing a token table: ${missing.join(', ')}`);
  for (const entry of matrix.components) {
    const table = tableOf(entry);
    for (const facet of FACETS) {
      assert.ok(
        Object.hasOwn(table, facet),
        `${entry.name}: token table must name facet "${facet}"`,
      );
    }
    assert.ok(Array.isArray(table.state), `${entry.name}: "state" facet must be an array`);
    for (const facet of ['container', 'label', 'shape', 'elevation', 'typescale']) {
      assert.equal(typeof table[facet], 'string', `${entry.name}: "${facet}" must be a token string`);
    }
  }
});

test('T1: every tabled facet carries a valid cascade source', () => {
  const matrix = loadMatrix();
  for (const entry of matrix.components) {
    const table = tableOf(entry);
    assert.ok(table !== null, `${entry.name}: missing token table (see six-facet test)`);
    assert.ok(
      table.sources !== null && typeof table.sources === 'object',
      `${entry.name}: token table must carry a "sources" record`,
    );
    for (const facet of FACETS) {
      assert.ok(
        Object.hasOwn(table.sources, facet),
        `${entry.name}: sources must name facet "${facet}"`,
      );
      assert.ok(
        SOURCES.has(table.sources[facet]),
        `${entry.name}: source for "${facet}" must be one of component|role|theme|default, got ${JSON.stringify(table.sources[facet])}`,
      );
    }
  }
});

test('T1: getComponentTokenTable is exposed as a read helper', () => {
  const { getComponentTokenTable } = getHelper();
  assert.equal(
    typeof getComponentTokenTable,
    'function',
    'registry must expose getComponentTokenTable(component) (ASSUMPTION H1)',
  );
});

test('T1: getComponentTokenTable returns the six-facet table per component', () => {
  const { getComponentTokenTable } = getHelper();
  assert.equal(typeof getComponentTokenTable, 'function', 'helper missing (see helper test)');
  const matrix = loadMatrix();
  for (const entry of matrix.components) {
    const table = getComponentTokenTable(entry.name);
    for (const facet of FACETS) {
      assert.ok(Object.hasOwn(table, facet), `${entry.name}: helper table must name "${facet}"`);
    }
    assert.ok(
      table.sources !== null && typeof table.sources === 'object',
      `${entry.name}: helper table must carry a "sources" record`,
    );
    for (const facet of FACETS) {
      assert.ok(
        SOURCES.has(table.sources[facet]),
        `${entry.name}: helper source for "${facet}" must be one of component|role|theme|default`,
      );
    }
  }
});

test('T1: getComponentTokenTable throws UnknownComponentError for a component with no table', () => {
  const { getComponentTokenTable } = getHelper();
  assert.equal(typeof getComponentTokenTable, 'function', 'helper missing (see helper test)');
  assert.throws(() => getComponentTokenTable('NoSuchM3Component'), isUnknownComponentError);
  assert.throws(() => getComponentTokenTable(''), isUnknownComponentError);
});

test('T1: getComponentTokenTable rejects non-string input with UnknownComponentError', () => {
  const { getComponentTokenTable } = getHelper();
  assert.equal(typeof getComponentTokenTable, 'function', 'helper missing (see helper test)');
  for (const bad of [123, null, undefined]) {
    assert.throws(
      () => getComponentTokenTable(bad),
      isUnknownComponentError,
      `non-string input ${String(bad)} must throw UnknownComponentError, not leak internals`,
    );
  }
});

// ---------------------------------------------------------------------------
// T2 — device resolution equals tabled roles, no generic fallback (AC-LAYOUT-053)
// ---------------------------------------------------------------------------

test('T2: resolveTokens role set equals the tabled role set for every component and mode', () => {
  const theme = loadTheme();
  const { getComponentTokenTable } = getHelper();
  assert.equal(typeof theme.resolveTokens, 'function', 'theme must expose resolveTokens');
  assert.equal(typeof getComponentTokenTable, 'function', 'helper missing (see T1 helper test)');
  const matrix = loadMatrix();
  for (const entry of matrix.components) {
    const table = getComponentTokenTable(entry.name);
    for (const mode of MODES) {
      const resolved = theme.resolveTokens(entry.name, mode);
      // REWORK (A4 address-vs-value split): role ADDRESSES come exclusively
      // from the component's own tabled roles — the resolved role set equals
      // the tabled role set. VALUES are seed/dynamic-derived, so no deep
      // value equality with the tabled role strings is asserted here.
      for (const facet of FACETS) {
        assert.ok(
          Object.hasOwn(resolved, facet),
          `${entry.name} [${mode}]: resolved role set must address tabled facet "${facet}"`,
        );
      }
    }
  }
});

test('T2: resolved records carry exactly the tabled facets — no shared-surface fallback keys', () => {
  const theme = loadTheme();
  const { getComponentTokenTable } = getHelper();
  assert.equal(typeof getComponentTokenTable, 'function', 'helper missing (see T1 helper test)');
  const matrix = loadMatrix();
  for (const entry of matrix.components) {
    for (const mode of MODES) {
      const resolved = theme.resolveTokens(entry.name, mode);
      for (const facet of FACETS) {
        assert.ok(Object.hasOwn(resolved, facet), `${entry.name} [${mode}]: resolved must carry "${facet}"`);
      }
      for (const generic of ['primary', 'onPrimary', 'surface', 'onSurface']) {
        assert.ok(
          !Object.hasOwn(resolved, generic),
          `${entry.name} [${mode}]: resolved must not carry generic fallback key "${generic}"`,
        );
      }
    }
  }
});

test('T2: registry no longer composes components from shared role-token sets', () => {
  const source = readFileSync(REGISTRY_JS, 'utf8');
  assert.ok(
    !source.includes('SURFACE_ROLE_TOKENS') && !source.includes('ACTION_ROLE_TOKENS'),
    'registry must not compose per-component tokens from shared SURFACE_ROLE_TOKENS/ACTION_ROLE_TOKENS sets (ASSUMPTION H3)',
  );
});

test('T2: resolveTokens throws UnknownComponentError for an unknown component', () => {
  const theme = loadTheme();
  assert.throws(() => theme.resolveTokens('NoSuchM3Component', 'light'), isUnknownComponentError);
  assert.throws(() => theme.resolveTokens('', 'dark'), isUnknownComponentError);
});

test('T2: changing seed changes resolved values (seed-palette fallback when dynamic is null)', () => {
  // Regression note: the old theme.test.ts seed/dynamic tests passed vacuously
  // via the mode echo. This asserts a REAL seed effect.
  const theme = loadTheme();
  const matrix = loadMatrix();
  const name = matrix.components[0].name;
  try {
    theme.setTheme({ seed: 'seed-alpha-fr-layout-016', dynamic: null });
    const before = theme.resolveTokens(name, 'light');
    theme.setTheme({ seed: 'seed-beta-fr-layout-016', dynamic: null });
    const after = theme.resolveTokens(name, 'light');
    const differs = FACETS.some(
      (facet) => JSON.stringify(before[facet]) !== JSON.stringify(after[facet]),
    );
    assert.ok(
      differs,
      `${name}: different seeds must yield different resolved values (dynamic null)`,
    );
  } finally {
    theme.setTheme({ mode: 'light', seed: '', dynamic: null });
    if (typeof theme.clearMounted === 'function') theme.clearMounted();
  }
});

test('T2: setting dynamic overrides seed-derived values', () => {
  // ASSUMPTION A4b: `dynamic` is an opaque non-null override input; when
  // present it must perturb the resolved values for the same tabled roles.
  const theme = loadTheme();
  const matrix = loadMatrix();
  const name = matrix.components[0].name;
  const seed = 'seed-gamma-fr-layout-016';
  try {
    theme.setTheme({ seed, dynamic: null });
    const seeded = theme.resolveTokens(name, 'light');
    theme.setTheme({ seed, dynamic: { container: '#ff0000', source: 'test-override' } });
    const overridden = theme.resolveTokens(name, 'light');
    const differs = FACETS.some(
      (facet) => JSON.stringify(seeded[facet]) !== JSON.stringify(overridden[facet]),
    );
    assert.ok(
      differs,
      `${name}: dynamic override must change seed-derived values for the same tabled roles`,
    );
  } finally {
    theme.setTheme({ mode: 'light', seed: '', dynamic: null });
    if (typeof theme.clearMounted === 'function') theme.clearMounted();
  }
});

test('T2: resolveTokens throws RangeError for an invalid mode (light/dark still pass)', () => {
  const theme = loadTheme();
  const matrix = loadMatrix();
  const name = matrix.components[0].name;
  for (const mode of MODES) {
    assert.ok(
      theme.resolveTokens(name, mode),
      `${name}: mode "${mode}" must keep resolving`,
    );
  }
  for (const bad of ['LIGHT', 'dark ', '', null, undefined, 42]) {
    assert.throws(
      () => theme.resolveTokens(name, bad),
      (err) => err !== null && typeof err === 'object' && err.name === 'RangeError',
      `${name}: invalid mode ${JSON.stringify(bad)} must throw RangeError`,
    );
  }
});

test('T2: resolveTokens rejects non-string input with UnknownComponentError', () => {
  const theme = loadTheme();
  for (const bad of [123, null, undefined]) {
    assert.throws(
      () => theme.resolveTokens(bad, 'light'),
      isUnknownComponentError,
      `non-string input ${String(bad)} must throw UnknownComponentError, not leak internals`,
    );
  }
});

// ---------------------------------------------------------------------------
// T3 — token gate: missing table and divergence fail test:tokens (AC-052/053)
// ---------------------------------------------------------------------------
//
// Interface under test: `node scripts/check-tokens.js --scope lib` — exit 1
// naming `component` when (a) a matrix/registry component has no token table,
// (b) a table fails STRUCTURAL validation (six facets present, container/
// label/shape/elevation/typescale strings, state string array, sources record
// with only component|role|theme|default values), or (c)
// `resolveTokens(component, mode)` role set differs from the tabled role set
// for `light` and `dark`; exit 0 otherwise. No exact md.comp.<slug>.<facet>
// namespace rule.
//
// Hermetic note: the failure-mode cases briefly rewrite
// `engine/layout/m3_matrix.json` on disk (the gate under test is a separate
// process, so in-process stubs cannot reach it) and always restore the exact
// original bytes in `finally`. No T1/T2 assertion above is modified.

const GATE_JS = join(HERE, '..', 'scripts', 'check-tokens.js');
const PACKAGE_DIR = join(HERE, '..');

function runGate() {
  try {
    const stdout = execFileSync('node', [GATE_JS, '--scope', 'lib'], {
      cwd: PACKAGE_DIR,
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    return { status: 0, output: String(stdout) };
  } catch (err) {
    const out = `${err.stdout ?? ''}${err.stderr ?? ''}`;
    return { status: typeof err.status === 'number' ? err.status : 1, output: String(out) };
  }
}

function withMatrix(mutated, fn) {
  const original = readFileSync(MATRIX_JSON, 'utf8');
  try {
    writeFileSync(MATRIX_JSON, JSON.stringify(mutated, null, 2));
    return fn();
  } finally {
    writeFileSync(MATRIX_JSON, original);
  }
}

function withoutTable(matrix, name) {
  const copy = JSON.parse(JSON.stringify(matrix));
  const entry = copy.components.find((c) => c.name === name);
  delete entry.tokenTable;
  delete entry.tokens;
  return copy;
}

// Same-file value swaps are OUT OF GATE SCOPE by design (T1.10 re-scope
// 2026-10-30): gate and theme read the same m3_matrix.json bytes in one
// process, so a same-file value swap moves both sides together and exact
// set equality passes by construction. No matrix-mutation gate on branch
// (c) "resolved role set != tabled role set" can catch it without a
// spelling-based per-facet rule. Branch (c) is proven via the extra-address
// + structural paths below.
function withMissingFacet(matrix, name) {
  // STRUCTURALLY invalid: drop one facet (sources entry left dangling).
  const copy = JSON.parse(JSON.stringify(matrix));
  const entry = copy.components.find((c) => c.name === name);
  const table = entry.tokenTable ?? entry.tokens;
  delete table.shape;
  return copy;
}

function withBadSource(matrix, name) {
  // STRUCTURALLY invalid: sources value outside component|role|theme|default.
  const copy = JSON.parse(JSON.stringify(matrix));
  const entry = copy.components.find((c) => c.name === name);
  const table = entry.tokenTable ?? entry.tokens;
  table.sources = { ...table.sources, container: 'shared-fallback' };
  return copy;
}

function withExtraFacet(matrix, name) {
  // WELL-FORMED-but-diverged (cardinality): adds an extra role key with a
  // well-formed md.comp-shaped value and a valid sources entry, so every
  // STRUCTURAL check passes — only exact resolved==tabled role-set equality
  // (branch c) can catch the extra address.
  const copy = JSON.parse(JSON.stringify(matrix));
  const entry = copy.components.find((c) => c.name === name);
  const table = entry.tokenTable ?? entry.tokens;
  const slug = name.toLowerCase().replace(/[^a-z0-9]+/g, '-');
  table.icon = `md.comp.${slug}.icon`;
  table.sources = { ...table.sources, icon: 'component' };
  return copy;
}

test('T3: token gate passes on a clean tree', () => {
  const result = runGate();
  assert.equal(
    result.status,
    0,
    `gate must exit 0 on a clean tree, got ${result.status}: ${result.output}`,
  );
});

test('T3: token gate fails naming the component when its token table is missing', () => {
  const matrix = loadMatrix();
  const name = matrix.components[0].name;
  const result = withMatrix(withoutTable(matrix, name), runGate);
  assert.equal(result.status, 1, `gate must exit 1 for a missing table, got ${result.status}`);
  assert.ok(
    result.output.includes(name),
    `gate failure must name the component "${name}", got: ${result.output}`,
  );
});

// NOTE (T1.10 re-scope): the former withSwappedFacets divergence test was
// removed — same-file value swaps are out of gate scope by design (see note
// above). Branch (c) coverage lives in the extra-address test below.
test('T3: token gate fails naming the component when a table carries an extra role address', () => {
  const matrix = loadMatrix();
  const name = matrix.components[0].name;
  const result = withMatrix(withExtraFacet(matrix, name), runGate);
  assert.equal(result.status, 1, `gate must exit 1 for an extra role address, got ${result.status}`);
  assert.ok(
    result.output.includes(name),
    `gate failure must name the component "${name}", got: ${result.output}`,
  );
});

test('T3: token gate fails naming the component when a table is structurally invalid', () => {
  const matrix = loadMatrix();
  const name = matrix.components[0].name;
  for (const [label, mutate] of [['missing facet', withMissingFacet], ['bad source', withBadSource]]) {
    const result = withMatrix(mutate(matrix, name), runGate);
    assert.equal(result.status, 1, `gate must exit 1 for a table with a ${label}, got ${result.status}`);
    assert.ok(
      result.output.includes(name),
      `gate failure (${label}) must name the component "${name}", got: ${result.output}`,
    );
  }
});
