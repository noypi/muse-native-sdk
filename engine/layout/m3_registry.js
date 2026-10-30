'use strict';
// FR-LAYOUT-010 / AC-LAYOUT-024 — M3 matrix component registry.
// Every entry is driven only by its own tabled token roles in
// `engine/layout/m3_matrix.json`: entries carry no literal style values
// (no colors, sizes, durations), only dotted token paths resolved through
// the T4 cascade (`engine/layout/tokens/store.js`) and TSX/native refs. Honors
// CONFLICT-M3-001: the 14 tier-3 composites are `kind: 'composite'` linking
// the TSX composite; tier-1/2 entries are `kind: 'native'`.

class UnknownComponentError extends Error {
  constructor(component) {
    super(`Unknown M3 component: ${String(component)}`);
    this.name = 'UnknownComponentError';
    this.component = component;
  }
}

// FR-LAYOUT-016 T1 / AC-LAYOUT-052 — read helper for the per-component M3
// token tables stored in `engine/layout/m3_matrix.json` (the store).
const fs = require('node:fs');
const path = require('node:path');

function matrixFile() {
  return path.join(__dirname, 'm3_matrix.json');
}

function tableOf(entry) {
  if (entry && typeof entry.tokenTable === 'object' && entry.tokenTable !== null && !Array.isArray(entry.tokenTable)) {
    return entry.tokenTable;
  }
  if (entry && typeof entry.tokens === 'object' && entry.tokens !== null && !Array.isArray(entry.tokens)) {
    return entry.tokens;
  }
  return null;
}

function getComponentTokenTable(component) {
  if (typeof component !== 'string' || component === '') {
    throw new UnknownComponentError(component);
  }
  let matrix;
  try {
    matrix = JSON.parse(fs.readFileSync(matrixFile(), 'utf8'));
  } catch (err) {
    throw new UnknownComponentError(component);
  }
  const entry = Array.isArray(matrix.components)
    ? matrix.components.find((c) => c && c.name === component)
    : undefined;
  const table = tableOf(entry);
  if (!table) {
    throw new UnknownComponentError(component);
  }
  return {
    container: table.container,
    label: table.label,
    state: Array.isArray(table.state) ? [...table.state] : table.state,
    shape: table.shape,
    elevation: table.elevation,
    typescale: table.typescale,
    sources: table.sources ? { ...table.sources } : table.sources,
  };
}

// FR-LAYOUT-016 T2 / AC-LAYOUT-053 — per-component table lookup only.
// Every entry's tokens are flattened from that component's own tabled roles
// (container/label/state/shape/elevation/typescale) in
// `engine/layout/m3_matrix.json` via getComponentTokenTable above: no shared
// role-token sets, two components never resolve through one shared list.
function flattenTable(table) {
  const tokens = [table.container, table.label];
  if (Array.isArray(table.state)) {
    tokens.push(...table.state);
  } else {
    tokens.push(table.state);
  }
  tokens.push(table.shape, table.elevation, table.typescale);
  return tokens;
}

const NATIVE_COMPONENTS = [
  'Button',
  'Card',
  'Chip',
  'Text',
  'FAB',
  'Slider',
  'Badge',
];

const COMPOSITE_COMPONENTS = [
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

const NATIVE_SET = new Set(NATIVE_COMPONENTS);
const COMPOSITE_SET = new Set(COMPOSITE_COMPONENTS);

function getPrimitive(component) {
  if (
    typeof component !== 'string' ||
    component === '' ||
    (!NATIVE_SET.has(component) && !COMPOSITE_SET.has(component))
  ) {
    throw new UnknownComponentError(component);
  }
  // Per-component table lookup: unknown table throws UnknownComponentError.
  const table = getComponentTokenTable(component);
  const composite = COMPOSITE_SET.has(component);
  return {
    primitive: composite ? `M3Composite${component}` : `M3Native${component}`,
    tokens: flattenTable(table),
    kind: composite ? 'composite' : 'native',
    ref: composite
      ? `engine/components/${component}.tsx`
      : `engine/layout/primitives/${component}.js`,
  };
}

module.exports = { getPrimitive, UnknownComponentError, getComponentTokenTable };
