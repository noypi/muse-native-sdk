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
 * ASSUMPTION H3 (no-fallback source check): the behavioral proof of "no generic
 * fallback" is that resolved records carry exactly the tabled facets (never the
 * generic primary/onPrimary/surface/onSurface keys). The registry-source check
 * for removed SURFACE_ROLE_TOKENS/ACTION_ROLE_TOKENS composition is secondary
 * evidence of the same requirement.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
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

test('T2: resolveTokens output equals the tabled roles for every component and mode', () => {
  const theme = loadTheme();
  const { getComponentTokenTable } = getHelper();
  assert.equal(typeof theme.resolveTokens, 'function', 'theme must expose resolveTokens');
  assert.equal(typeof getComponentTokenTable, 'function', 'helper missing (see T1 helper test)');
  const matrix = loadMatrix();
  for (const entry of matrix.components) {
    const table = getComponentTokenTable(entry.name);
    for (const mode of MODES) {
      const resolved = theme.resolveTokens(entry.name, mode);
      for (const facet of FACETS) {
        assert.deepEqual(
          resolved[facet],
          table[facet],
          `${entry.name} [${mode}]: resolved "${facet}" must equal the tabled role`,
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
  const first = matrix.components[0].name;
  for (const mode of MODES) {
    const resolved = theme.resolveTokens(first, mode);
    for (const facet of FACETS) {
      assert.ok(Object.hasOwn(resolved, facet), `${first} [${mode}]: resolved must carry "${facet}"`);
    }
    for (const generic of ['primary', 'onPrimary', 'surface', 'onSurface']) {
      assert.ok(
        !Object.hasOwn(resolved, generic),
        `${first} [${mode}]: resolved must not carry generic fallback key "${generic}"`,
      );
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
