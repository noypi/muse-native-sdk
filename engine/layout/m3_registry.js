'use strict';
// FR-LAYOUT-010 / AC-LAYOUT-024 — M3 matrix component registry.
// Every entry is driven only by token references + the shared signal roles
// below: entries carry no literal style values (no colors, sizes, durations),
// only dotted token paths resolved through the T4 cascade
// (`engine/layout/tokens/store.js`) and TSX/native refs. Honors
// CONFLICT-M3-001: the 14 tier-3 composites are `kind: 'composite'` linking
// the TSX composite; tier-1/2 entries are `kind: 'native'`.

class UnknownComponentError extends Error {
  constructor(component) {
    super(`Unknown M3 component: ${String(component)}`);
    this.name = 'UnknownComponentError';
    this.component = component;
  }
}

// Shared token roles (single source of truth). Per-component token sets are
// composed from these roles — no per-component literal values.
const SURFACE_ROLE_TOKENS = [
  'md.sys.color.surface',
  'md.sys.color.on-surface',
  'md.sys.shape.corner.medium',
  'md.sys.state.elevation.level-1',
];

const ACTION_ROLE_TOKENS = [
  'md.sys.color.primary',
  'md.sys.color.on-primary',
  'md.sys.shape.corner.full',
  'md.sys.motion.duration.short-2',
];

const TEXT_ROLE_TOKENS = [
  'md.sys.color.on-surface',
  'md.sys.typescale.body-large',
  'md.sys.motion.easing.standard',
];

// Component token namespace, derived from the registry name (a reference,
// never a literal value): `md.comp.<slug>.<facet>`.
function componentTokens(name, roles) {
  const slug = name
    .replace(/([a-z0-9])([A-Z])/g, '$1-$2')
    .toLowerCase();
  return [
    `md.comp.${slug}.container`,
    `md.comp.${slug}.label`,
    ...roles,
  ];
}

function nativeEntry(name, roles) {
  return {
    primitive: `M3Native${name}`,
    tokens: componentTokens(name, roles),
    kind: 'native',
    ref: `engine/layout/primitives/${name}.js`,
  };
}

function compositeEntry(name, roles) {
  return {
    primitive: `M3Composite${name}`,
    tokens: componentTokens(name, roles),
    kind: 'composite',
    ref: `engine/components/${name}.tsx`,
  };
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

const TEXT_COMPONENTS = new Set(['Text', 'SearchBar']);

const REGISTRY = Object.create(null);
for (const name of NATIVE_COMPONENTS) {
  REGISTRY[name] = nativeEntry(
    name,
    TEXT_COMPONENTS.has(name) ? TEXT_ROLE_TOKENS : SURFACE_ROLE_TOKENS
  );
}
// FAB/Slider/Button-family entries share the action role tokens.
for (const name of ['Button', 'FAB', 'Slider', 'Chip', 'Badge']) {
  REGISTRY[name] = nativeEntry(
    name,
    name === 'Chip' || name === 'Badge' ? SURFACE_ROLE_TOKENS : ACTION_ROLE_TOKENS
  );
}
for (const name of COMPOSITE_COMPONENTS) {
  REGISTRY[name] = compositeEntry(
    name,
    TEXT_COMPONENTS.has(name) ? TEXT_ROLE_TOKENS : SURFACE_ROLE_TOKENS
  );
}

function getPrimitive(component) {
  if (
    typeof component !== 'string' ||
    component === '' ||
    !Object.hasOwn(REGISTRY, component)
  ) {
    throw new UnknownComponentError(component);
  }
  const entry = REGISTRY[component];
  return { primitive: entry.primitive, tokens: [...entry.tokens], kind: entry.kind, ref: entry.ref };
}

module.exports = { getPrimitive, UnknownComponentError };
