/**
 * TC-M3-003 — the 16 tier-2 components construct with documented variants
 * over tier-1 primitives only (FR-M3-002, AC-M3-003, RULE-M3-001,
 * RULE-M3-006).
 *
 * Hermetic: node:test + node:assert/strict only. Imports the components
 * through the package public entry (index.js), reads muse.manifest.json from
 * disk. No network, no secrets, no services.
 *
 * ASSUMPTION C1 (composition traversal): a tier-2 descriptor is a
 * `{ type, props }` record whose nested tier-1 descriptors appear as plain
 * objects (or arrays of them) somewhere under `props` — e.g. `content`,
 * `children`, `anchor`, or `stateLayer`. The test collects every string
 * value held under a `type` key in that graph; the outer type must be the
 * component's own name and at least one nested type must be a tier-1
 * primitive name — proving real composition with no new native primitive
 * registered or instantiated.
 * ASSUMPTION C2 (variants are prop values): Button/Card/Chip document their
 * variants through a `variant` prop whose values match the manifest schema;
 * the descriptor echoes the requested variant. No `ButtonGiant`-style
 * component name is exported.
 * ASSUMPTION C3 (Tooltip layering): Tooltip renders via OverlayHost, so the
 * string "OverlayHost" appears in the JSON form of a Tooltip descriptor.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { components } = require('../index.js');
const MANIFEST_URL = new URL('../muse.manifest.json', import.meta.url);

const TIER_1 = new Set([
  'Surface',
  'Text',
  'Icon',
  'Image',
  'Ripple',
  'Divider',
  'Scroll',
  'Stack',
  'TextFieldEditCore',
  'OverlayHost',
]);

const TIER_2 = [
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

// Minimal representative props per component (keys must exist in the manifest).
const REPRESENTATIVE_PROPS = {
  Button: { variant: 'filled', label: 'OK' },
  IconButton: { icon: 'star' },
  Fab: { icon: 'add' },
  ExtendedFab: { label: 'Create' },
  Card: { variant: 'elevated', content: 'body' },
  Chip: { variant: 'assist', label: 'Tag' },
  Checkbox: { checked: true },
  Radio: { selected: false },
  Switch: { checked: true },
  Slider: { value: 0.5 },
  Badge: { label: '3' },
  LinearProgress: { progress: 0.25 },
  CircularProgress: { progress: 0.75 },
  ListItem: { headline: 'Title' },
  Avatar: { label: 'AB' },
  Tooltip: { text: 'Help' },
};

function loadManifest() {
  return JSON.parse(readFileSync(MANIFEST_URL, 'utf8'));
}

// Collect every string held under a `type` key in a descriptor graph.
function collectTypes(node, into = []) {
  if (Array.isArray(node)) {
    for (const item of node) collectTypes(item, into);
  } else if (node !== null && typeof node === 'object') {
    for (const [key, value] of Object.entries(node)) {
      if (key === 'type' && typeof value === 'string') into.push(value);
      else collectTypes(value, into);
    }
  }
  return into;
}

// ---------------------------------------------------------------------------
// AC-M3-003: all 16 tier-2 components construct with documented variants
// ---------------------------------------------------------------------------

test('all 16 tier-2 components are exported', () => {
  for (const name of TIER_2) {
    assert.equal(typeof components[name], 'function', `component not exported: ${name}`);
  }
});

for (const name of TIER_2) {
  test(`${name} constructs a descriptor from representative props`, () => {
    const descriptor = components[name](REPRESENTATIVE_PROPS[name]);
    assert.equal(descriptor.type, name, `${name} descriptor type must equal its name`);
    assert.deepEqual(descriptor.props, REPRESENTATIVE_PROPS[name]);
  });
}

test('all 16 tier-2 components construct with no props (props default to {})', () => {
  for (const name of TIER_2) {
    const descriptor = components[name]();
    assert.equal(descriptor.type, name);
    assert.deepEqual(descriptor.props, {});
  }
});

for (const variant of ['filled', 'tonal', 'outlined', 'text', 'elevated']) {
  test(`Button constructs with variant "${variant}"`, () => {
    const descriptor = components.Button({ variant, label: 'OK' });
    assert.equal(descriptor.type, 'Button');
    assert.equal(descriptor.props.variant, variant);
  });
}

for (const variant of ['elevated', 'filled', 'outlined']) {
  test(`Card constructs with variant "${variant}"`, () => {
    const descriptor = components.Card({ variant, content: 'body' });
    assert.equal(descriptor.type, 'Card');
    assert.equal(descriptor.props.variant, variant);
  });
}

for (const variant of ['assist', 'filter', 'input', 'suggestion']) {
  test(`Chip constructs with variant "${variant}"`, () => {
    const descriptor = components.Chip({ variant, label: 'Tag' });
    assert.equal(descriptor.type, 'Chip');
    assert.equal(descriptor.props.variant, variant);
  });
}

test('tier-2 representative prop keys exist in the manifest schemas', () => {
  const manifest = loadManifest();
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  for (const name of TIER_2) {
    assert.ok(byName.has(name), `manifest entry missing for tier-2 component: ${name}`);
    const schemaKeys = new Set(Object.keys(byName.get(name).props ?? {}));
    for (const key of Object.keys(REPRESENTATIVE_PROPS[name])) {
      assert.ok(schemaKeys.has(key), `${name}.${key} is not in the manifest schema`);
    }
  }
});

// ---------------------------------------------------------------------------
// RULE-M3-001: composition over tier-1 primitives only (no new primitive)
// ---------------------------------------------------------------------------

for (const name of TIER_2) {
  test(`${name} composes tier-1 descriptors only (no new native primitive)`, () => {
    const descriptor = components[name](REPRESENTATIVE_PROPS[name]);
    const types = collectTypes(descriptor);
    assert.ok(types.length > 0, `${name} descriptor must carry a type`);
    assert.equal(types[0], name, `${name} outer descriptor type must equal its name`);
    const nested = types.filter((type) => type !== name);
    assert.ok(
      nested.length > 0,
      `${name} descriptor must nest at least one tier-1 descriptor (composition over tier-1)`,
    );
    for (const type of nested) {
      assert.ok(
        TIER_1.has(type),
        `${name} instantiates "${type}", which is not a tier-1 primitive (new native primitive registered)`,
      );
    }
  });
}

// ---------------------------------------------------------------------------
// RULE-M3-006: overlay members layer via OverlayHost
// ---------------------------------------------------------------------------

test('Tooltip layers via OverlayHost', () => {
  const descriptor = components.Tooltip({ text: 'Help', anchor: 'target' });
  assert.equal(descriptor.type, 'Tooltip');
  assert.ok(
    JSON.stringify(descriptor).includes('OverlayHost'),
    'Tooltip descriptor must layer via OverlayHost',
  );
});

// ---------------------------------------------------------------------------
// Boundary/error: unknown variant-style names are not exported
// ---------------------------------------------------------------------------

test('unknown variant-style component names are not exported', () => {
  assert.equal(components.ButtonGiant, undefined);
  assert.equal(components.GiantButton, undefined);
  assert.equal(components.CardGiant, undefined);
  assert.equal(components.NotAComponent, undefined);
});

test('accessing an unknown tier-2 component cannot construct a descriptor', () => {
  assert.throws(() => {
    components.NotAComponent({});
  }, TypeError);
});
