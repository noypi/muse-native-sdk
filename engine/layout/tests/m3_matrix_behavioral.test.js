'use strict';
// BUG-M3-FIDELITY-010 T1 — behavioral matrix contract (AC-M3-003/005/006, D1).
// Hermetic: node:test + node:assert/strict only. Reads exactly one repo path
// (`../m3_matrix.json`) via node:fs relative to __dirname. No network, no
// secrets, no application configuration, no live services.
//
// Contract under test:
//   loadMatrix(): { components: Array<{ name, container, label,
//     opacity: { hover: 0.08, focus: 0.12, press: 0.16 },
//     shape, elevation, typescale, keyboard, focus,
//     motion: { token, durationMs }, role }> }
// throws Error("matrix: <name> missing <field>") when any tier-2/3 row lacks
// a measured field; returns rows keyed by spec-canonical names.
//
// ASSUMPTION (canonical key): the spec-canonical key for the FAB entry is
// `Fab` (matching the `components` export in packages/native/index.js); a
// separate all-caps `FAB` row is non-canonical and must not exist.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const MATRIX_PATH = path.join(__dirname, '..', 'm3_matrix.json');

const SPEC_TIER_2 = [
  'Button',
  'IconButton',
  'Fab',
  'ExtendedFab',
  'Card',
  'Chip',
  'Checkbox',
  'Radio',
  'Switch',
  'Slider',
  'Badge',
  'LinearProgress',
  'CircularProgress',
  'ListItem',
  'Avatar',
  'Tooltip',
];

const SPEC_TIER_3 = [
  'TopAppBar',
  'NavigationBar',
  'NavigationRail',
  'NavigationDrawer',
  'SegmentedButton',
  'Dialog',
  'BottomSheet',
  'Menu',
  'SnackbarHost',
  'SearchBar',
  'Carousel',
  'BottomAppBar',
  'ButtonGroup',
  'SplitButton',
];

// D1: rows with no spec counterpart — must be dropped or remapped, never kept.
const NON_SPEC_ROWS = [
  'FAB',
  'FABMenu',
  'Image',
  'List',
  'LoadingIndicator',
  'ScrollView',
  'Snackbar',
  'Text',
  'TextField',
  'Toolbar',
  'Touchable',
  'View',
];

const MEASURED_FIELDS = [
  'container',
  'label',
  'opacity',
  'shape',
  'elevation',
  'typescale',
  'keyboard',
  'focus',
  'motion',
  'role',
];

function readRaw() {
  return JSON.parse(fs.readFileSync(MATRIX_PATH, 'utf8'));
}

// loadMatrix per the T1 Interface line: rows keyed by spec-canonical names,
// throws Error("matrix: <name> missing <field>") on any missing measured field.
function loadMatrix(matrix) {
  const doc = matrix === undefined ? readRaw() : matrix;
  const rows = Array.isArray(doc.components) ? doc.components : [];
  const byName = new Map(rows.map((row) => [row && row.name, row]));
  return {
    components: [...SPEC_TIER_2, ...SPEC_TIER_3].map((name) => {
      const row = byName.get(name);
      if (row === undefined) {
        throw new Error(`matrix: ${name} missing row`);
      }
      for (const field of MEASURED_FIELDS) {
        if (row[field] === undefined) {
          throw new Error(`matrix: ${name} missing ${field}`);
        }
      }
      return row;
    }),
  };
}

describe('BUG-M3-FIDELITY-010 T1 — matrix rows are spec-canonical with measured fields', () => {
  it('resolves every tier-2 spec name to exactly one row', () => {
    const rows = readRaw().components;
    for (const name of SPEC_TIER_2) {
      const matches = rows.filter((row) => row && row.name === name);
      assert.equal(matches.length, 1, `expected exactly one matrix row for tier-2 ${name}`);
    }
  });

  it('resolves every tier-3 spec name to exactly one row', () => {
    const rows = readRaw().components;
    for (const name of SPEC_TIER_3) {
      const matches = rows.filter((row) => row && row.name === name);
      assert.equal(matches.length, 1, `expected exactly one matrix row for tier-3 ${name}`);
    }
  });

  it('carries all ten measured fields on every tier-2/3 row', () => {
    const loaded = loadMatrix();
    assert.equal(loaded.components.length, SPEC_TIER_2.length + SPEC_TIER_3.length);
  });

  it('records hover/focus/press opacity as 0.08/0.12/0.16 on every tier-2/3 row', () => {
    const loaded = loadMatrix();
    for (const row of loaded.components) {
      assert.deepEqual(
        row.opacity,
        { hover: 0.08, focus: 0.12, press: 0.16 },
        `${row.name}: opacity must be the measured 8/12/16 state opacities`,
      );
    }
  });

  it('records a motion token with a numeric duration on every tier-2/3 row', () => {
    const loaded = loadMatrix();
    for (const row of loaded.components) {
      assert.equal(typeof row.motion.token, 'string', `${row.name}: motion.token missing`);
      assert.ok(row.motion.token.length > 0, `${row.name}: motion.token must resolve`);
      assert.equal(typeof row.motion.durationMs, 'number', `${row.name}: motion.durationMs missing`);
    }
  });

  it('records shape, elevation, typescale, keyboard, focus and role per row', () => {
    const loaded = loadMatrix();
    for (const row of loaded.components) {
      for (const field of ['container', 'label', 'shape', 'elevation', 'typescale', 'keyboard', 'focus', 'role']) {
        assert.equal(typeof row[field], 'string', `${row.name}: ${field} missing`);
        assert.ok(row[field].length > 0, `${row.name}: ${field} must be non-empty`);
      }
    }
  });

  it('throws Error("matrix: <name> missing <field>") when a row lacks a measured field', () => {
    const complete = (name) => ({
      name,
      container: `${name}.container`,
      label: `${name}.label`,
      opacity: { hover: 0.08, focus: 0.12, press: 0.16 },
      shape: `${name}.shape`,
      elevation: `${name}.elevation`,
      typescale: `${name}.typescale`,
      keyboard: `${name}.keyboard`,
      focus: `${name}.focus`,
      motion: { token: 'motion.token', durationMs: 200 },
      role: `${name}.role`,
    });
    const mutated = {
      components: [...SPEC_TIER_2, ...SPEC_TIER_3].map((name) => complete(name)),
    };
    delete mutated.components.find((row) => row.name === 'Button').shape;
    assert.throws(() => loadMatrix(mutated), /matrix: Button missing shape/);
  });

  it('throws for a missing row with the matrix prefix', () => {
    assert.throws(() => loadMatrix({ components: [] }), /^Error: matrix: Button missing row$|^matrix: Button missing row/);
  });

  it('keeps no non-spec rows (D1: drop or remap, no quarantine)', () => {
    const names = new Set(readRaw().components.map((row) => row && row.name));
    for (const name of NON_SPEC_ROWS) {
      assert.ok(!names.has(name), `non-spec row must be dropped or remapped: ${name}`);
    }
  });

  it('shares no identical full token set between two tier-2/3 rows', () => {
    const rows = readRaw().components.filter(
      (row) => row && (SPEC_TIER_2.includes(row.name) || SPEC_TIER_3.includes(row.name)),
    );
    const seen = new Map();
    for (const row of rows) {
      const key = JSON.stringify([...(row.tokens || [])].sort());
      assert.ok(!seen.has(key), `${row.name} shares an identical full token set with ${seen.get(key)}`);
      seen.set(key, row.name);
    }
  });
});

module.exports = { loadMatrix, SPEC_TIER_2, SPEC_TIER_3 };
