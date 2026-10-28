'use strict';
// T2 (FR-LIFECYCLE-003) — in-place configuration handling with no recreation.
// Maps a config `change` verbatim to the T1 `config:<change>` self-loop and
// dispatches it through the T2 observer hub. The machine state is unchanged
// by construction; `recreated` is always `false`. Unknown change strings
// propagate the hub/machine throw.
//
// T3 (FR-LIFECYCLE-006, AC-LIFECYCLE-014) — declared config → minimal
// invalidation map (layout/resources/fonts/theme, never full rebuild).

// Frozen `android:configChanges` 13-tuple per plan A2.
const CONFIG_CHANGES = Object.freeze([
  'locale',
  'layoutDirection',
  'density',
  'fontScale',
  'orientation',
  'screenSize',
  'smallestScreenSize',
  'uiMode',
  'colorMode',
  'keyboard',
  'keyboardHidden',
  'navigation',
  'screenLayout',
]);

const CANONICAL_ORDER = Object.freeze(['layout', 'resources', 'fonts', 'theme']);

// Minimal invalidation sets per plan Detail. Stored as plain arrays;
// getInvalidation projects through CANONICAL_ORDER so output order is
// enforced programmatically rather than by entry convention.
const INVALIDATION_MAP = Object.freeze({
  orientation: ['layout'],
  screenSize: ['layout'],
  smallestScreenSize: ['layout'],
  screenLayout: ['layout'],
  locale: ['resources'],
  layoutDirection: ['resources'],
  navigation: ['resources'],
  keyboard: ['resources'],
  keyboardHidden: ['resources'],
  density: ['fonts'],
  fontScale: ['layout', 'fonts'],
  uiMode: ['resources', 'theme'],
  colorMode: ['resources', 'theme'],
});

const KNOWN_CHANGES = new Set(CONFIG_CHANGES);

function unknownChangeError() {
  const err = new Error('CONFIG_UNKNOWN_CHANGE');
  err.code = 'CONFIG_UNKNOWN_CHANGE';
  return err;
}

function getInvalidation(change) {
  if (typeof change !== 'string' || !KNOWN_CHANGES.has(change)) {
    throw unknownChangeError();
  }
  return { targets: CANONICAL_ORDER.filter((t) => INVALIDATION_MAP[change].includes(t)) };
}

function createConfigManager(hub) {
  function onConfigurationChanged(change) {
    const result = hub.dispatch(`config:${change}`);
    return { applied: result.applied, state: result.state, recreated: false };
  }

  return { onConfigurationChanged, getInvalidation };
}

module.exports = { createConfigManager, getInvalidation, CONFIG_CHANGES };
