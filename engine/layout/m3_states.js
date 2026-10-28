'use strict';
// FR-LAYOUT-010 / AC-LAYOUT-025 — component state -> token-reference map.
// Every returned value is a dotted token reference (never a literal), resolved
// through the T4 cascade (`engine/layout/tokens/store.js`). The same state
// shares a common state-layer role token across all components so the
// cross-component consistency check holds.

const STATES = ['enabled', 'hovered', 'focused', 'pressed', 'dragged', 'disabled', 'error'];

// All matrix components from `engine/layout/m3_registry.js` (T8). Read-only
// mirror — the registry remains the source of truth; this list only gates the
// unknown-component throw without requiring the registry at runtime.
const KNOWN_COMPONENTS = new Set([
  'Button',
  'Card',
  'Chip',
  'Text',
  'FAB',
  'Slider',
  'Badge',
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
]);

const STATE_SET = new Set(STATES);

// Shared per-state role token: identical value for every component in the
// same state, distinct per state.
const STATE_LAYER_TOKENS = Object.create(null);
for (const state of STATES) {
  STATE_LAYER_TOKENS[state] = `md.sys.state.${state}.opacity`;
}

function slugOf(name) {
  return name.replace(/([a-z0-9])([A-Z])/g, '$1-$2').toLowerCase();
}

function stateTokens(component, state) {
  if (typeof component !== 'string' || component === '' || !KNOWN_COMPONENTS.has(component)) {
    throw new Error(`Unknown M3 component: ${String(component)}`);
  }
  if (typeof state !== 'string' || state === '' || !STATE_SET.has(state)) {
    throw new Error(`Unknown M3 state: ${String(state)}`);
  }
  const slug = slugOf(component);
  return {
    container: `md.comp.${slug}.container`,
    label: `md.comp.${slug}.label`,
    'state-layer': STATE_LAYER_TOKENS[state],
    elevation: `md.sys.state.${state}.elevation`,
  };
}

module.exports = { stateTokens };
