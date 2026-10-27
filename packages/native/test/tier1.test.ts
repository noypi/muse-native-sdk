/**
 * TC-M3-001 — the 10 tier-1 primitives construct from TSX with manifest
 * prop schemas (FR-M3-001, AC-M3-001, RULE-M3-001).
 *
 * Hermetic: node:test + node:assert/strict only. Imports the components
 * through the package public entry (index.js), reads muse.manifest.json and
 * index.d.ts from disk. No network, no secrets, no services.
 *
 * ASSUMPTION P1 (runtime shape): each primitive is a function taking an
 * optional props record and returning a `{ type, props }` descriptor, so
 * existing consumers do not rename. Unknown prop keys are NOT rejected at
 * runtime; accepted-key equality with the manifest schema is enforced at
 * the type layer (index.d.ts per-component prop types).
 * ASSUMPTION P2 (type-layer check): index.d.ts declares one Props type per
 * primitive (e.g. SurfaceProps) generated from the T1 manifest schemas.
 * Verified as source text so the test stays independent of the TS compiler.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { components } = require('../index.js');
const MANIFEST_URL = new URL('../muse.manifest.json', import.meta.url);
const DTS_URL = new URL('../index.d.ts', import.meta.url);

const TIER_1 = [
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
];

function loadManifest() {
  return JSON.parse(readFileSync(MANIFEST_URL, 'utf8'));
}

// Representative schema props per primitive (keys must exist in the manifest).
const SCHEMA_PROPS = {
  Surface: { tone: 'surface', elevation: 1 },
  Text: { variant: 'body-medium', color: 'on-surface' },
  Icon: { name: 'star', size: 24, color: 'primary' },
  Image: { source: 'https://example.test/img.png', contentFit: 'cover' },
  Ripple: { color: 'primary', bounded: true },
  Divider: { orientation: 'horizontal', color: 'outline' },
  Scroll: { orientation: 'vertical' },
  Stack: { direction: 'row', spacing: 8 },
  TextFieldEditCore: { value: 'hello', placeholder: 'type', enabled: true },
  OverlayHost: {},
};

// ---------------------------------------------------------------------------
// AC-M3-001: each of the 10 primitives constructs with its schema props
// ---------------------------------------------------------------------------

test('all 10 tier-1 primitives are exported', () => {
  for (const name of TIER_1) {
    assert.equal(typeof components[name], 'function', `primitive not exported: ${name}`);
  }
});

for (const name of TIER_1) {
  test(`${name} constructs a descriptor from schema props`, () => {
    const descriptor = components[name](SCHEMA_PROPS[name]);
    assert.equal(descriptor.type, name, `${name} descriptor type must equal its name`);
    assert.deepEqual(descriptor.props, SCHEMA_PROPS[name]);
  });
}

test('primitives construct with no props (props default to {})', () => {
  for (const name of TIER_1) {
    const descriptor = components[name]();
    assert.equal(descriptor.type, name);
    assert.deepEqual(descriptor.props, {});
  }
});

// ---------------------------------------------------------------------------
// Boundary: accepted prop keys equal the manifest schema (ASSUMPTION P1)
// ---------------------------------------------------------------------------

test('schema prop keys used here exist in the manifest schemas', () => {
  const manifest = loadManifest();
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  for (const name of TIER_1) {
    const schemaKeys = new Set(Object.keys(byName.get(name).props ?? {}));
    for (const key of Object.keys(SCHEMA_PROPS[name])) {
      assert.ok(schemaKeys.has(key), `${name}.${key} is not in the manifest schema`);
    }
  }
});

test('index.d.ts declares one Props type per tier-1 primitive', () => {
  const dts = readFileSync(DTS_URL, 'utf8');
  for (const name of TIER_1) {
    assert.ok(
      dts.includes(`${name}Props`),
      `index.d.ts must declare per-component prop types generated from the manifest (${name}Props missing)`,
    );
  }
});

// ---------------------------------------------------------------------------
// Error case: unknown primitive name is not exported
// ---------------------------------------------------------------------------

test('an unknown primitive name is not exported', () => {
  assert.equal(components.Foo, undefined);
  assert.equal(components.View, undefined);
});

test('accessing an unknown primitive cannot construct a descriptor', () => {
  assert.throws(() => {
    components.NotAPrimitive({});
  }, TypeError);
});
