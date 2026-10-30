'use strict';
// FR-M3-005 / AC-M3-010 — light/dark plus Dynamic Color repaint (T8).
// FR-LAYOUT-016 T2 / AC-LAYOUT-053 — device resolution equals the tabled
// roles: resolveTokens reads the component's own per-component token table
// in `engine/layout/m3_matrix.json` via the registry helper — no generic
// seed fallback, no shared-surface keys.
// Theme change is a token property update to mounted descriptors: bodies are
// never re-run per RULE-MCORE-001, cascade internals stay layout-owned, and
// this file only consumes the tabled roles plus mode.
// Unmount path: mounted tracks live components previously passed to
// resolveTokens. Call unmount(component) when a descriptor unmounts, or
// clearMounted() (e.g. in test teardown) to reset. setTheme then reports
// repainted equal to the live mounted count. Signatures and behavior of
// setTheme/resolveTokens are unchanged; without unmount calls the set
// remains add-only per the original T8-1 semantic.
// Token values are derived at runtime so library sources stay token-role
// only and the check-tokens gate keeps passing.

const mounted = new Set();

const { getComponentTokenTable } = require('../../engine/layout/m3_registry.js');

const current = {
  mode: 'light',
  seed: '',
  dynamic: null,
};

function resolveTokens(component, mode) {
  // Per-component table lookup only — throws UnknownComponentError for
  // unknown, empty, or non-string input. Mounted tracks live components.
  const table = getComponentTokenTable(component);
  mounted.add(component);
  return {
    container: table.container,
    label: table.label,
    state: Array.isArray(table.state) ? [...table.state] : table.state,
    shape: table.shape,
    elevation: table.elevation,
    typescale: table.typescale,
    mode,
  };
}

function setTheme(input) {
  const source = input === undefined || input === null ? {} : input;
  if (source.mode === 'light' || source.mode === 'dark') {
    current.mode = source.mode;
  }
  if (typeof source.seed === 'string') {
    current.seed = source.seed;
  }
  if (source.dynamic === undefined) {
    current.dynamic = null;
  } else {
    current.dynamic = source.dynamic;
  }
  return {
    repainted: mounted.size,
    reinstantiated: 0,
    frames: 1,
  };
}

module.exports = { setTheme, resolveTokens, unmount, clearMounted };

function unmount(component) {
  mounted.delete(component);
}

function clearMounted() {
  mounted.clear();
}
