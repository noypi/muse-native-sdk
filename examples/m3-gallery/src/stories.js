'use strict';

// Gallery story index (FR-M3-007 / AC-M3-015, AC-M3-016, T5).
// Manifest-driven: every story is derived from `packages/native/muse.manifest.json`
// via the pure `buildStories(manifest)` function below. No hand-listed component
// names live here, so adding a component to the manifest appears in the gallery
// (and removing it drops its stories) with no edit to this file.

const STATE_PROPS = ['enabled', 'selected', 'checked', 'open', 'error'];
const OVERLAY_COMPONENTS = new Set(['Dialog', 'BottomSheet', 'Menu', 'SnackbarHost', 'Tooltip']);
const THEMES = ['light', 'dark'];

function valuesOf(entry) {
  const out = [];
  const props = (entry && entry.props) || {};
  for (const prop of Object.keys(props)) {
    const schema = props[prop];
    if (schema !== null && typeof schema === 'object' && Array.isArray(schema.values)) {
      for (const value of schema.values) out.push({ prop, value: String(value) });
    }
  }
  return out;
}

function statePropsOf(entry) {
  const props = (entry && entry.props) || {};
  return Object.keys(props).filter(
    (key) =>
      STATE_PROPS.includes(key) &&
      props[key] !== null &&
      typeof props[key] === 'object' &&
      props[key].type === 'boolean',
  );
}

function makeDescriptor(entry, variant, state, theme) {
  const props = { theme };
  if (variant.prop !== null) props[variant.prop] = variant.value;
  if (state !== 'default') props[state] = true;
  const descriptor = {
    type: entry.name,
    theme,
    variant: variant.value,
    state,
    props,
  };
  if (OVERLAY_COMPONENTS.has(entry.name)) {
    // RULE-M3-006: overlay members layer via OverlayHost. Kept as a nested
    // descriptor field (not `type`) so `type` still names the component.
    descriptor.overlay = 'OverlayHost';
    descriptor.host = { type: 'OverlayHost', content: { type: entry.name, props: { ...props } } };
  }
  return descriptor;
}

// One story per (component x documented variant/state x light/dark).
// Pure function of the manifest object: never mutates its input.
function buildStories(manifest) {
  if (!manifest || typeof manifest !== 'object' || !Array.isArray(manifest.components)) {
    return [];
  }
  const stories = [];
  for (const entry of manifest.components) {
    if (!entry || typeof entry.name !== 'string') continue;
    const documented = valuesOf(entry);
    const variants = documented.length > 0 ? documented : [{ prop: null, value: 'default' }];
    const owned = statePropsOf(entry);
    const states = owned.length > 0 ? ['default', ...owned] : ['default'];
    for (const variant of variants) {
      for (const theme of THEMES) {
        for (const state of states) {
          const component = entry.name;
          const { tier } = entry;
          const variantValue = variant.value;
          stories.push({
            component,
            tier,
            variant: variantValue,
            state,
            theme,
            render: () => makeDescriptor(entry, variant, state, theme),
          });
        }
      }
    }
  }
  return stories;
}

module.exports = { buildStories, STATE_PROPS, THEMES };
