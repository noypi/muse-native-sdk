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
import { readFileSync, mkdtempSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

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

// ---------------------------------------------------------------------------
// TC-M3-024 / AC-M3-023: selection-control tap-to-activate (FR-M3-002)
// Hermetic: node:test + node:assert/strict only. Selection handles are loaded
// lazily inside each test via createRequire so the TC-M3-003 gate above stays
// green when selection.js is absent (each new test then fails in isolation).
// ---------------------------------------------------------------------------

function loadSelection() {
  return require('../selection.js');
}

// --- T1: enabled Checkbox tap flips `checked` through the selection binding ---

test('AC-M3-023 T1: checkbox tap flips unchecked to checked with one-frame repaint', () => {
  const { createCheckbox } = loadSelection();
  const handle = createCheckbox({ checked: false });
  const record = handle.tap();
  assert.equal(record.checked, true);
  assert.equal(record.repainted, 1);
  assert.equal(record.reinstantiated, 0);
  assert.equal(record.frames, 1);
  assert.equal(handle.checked, true);
});

test('AC-M3-023 T1: checkbox second tap flips back to unchecked', () => {
  const { createCheckbox } = loadSelection();
  const handle = createCheckbox({ checked: false });
  handle.tap();
  const record = handle.tap();
  assert.equal(record.checked, false);
  assert.equal(record.repainted, 1);
  assert.equal(record.reinstantiated, 0);
  assert.equal(record.frames, 1);
  assert.equal(handle.checked, false);
});

test('AC-M3-023 T1: checkbox tap writes through binding.set', () => {
  const { createCheckbox } = loadSelection();
  const seen = [];
  const handle = createCheckbox({ checked: false, binding: { set: (v) => seen.push(v) } });
  handle.tap();
  assert.deepEqual(seen, [true]);
});

test('AC-M3-023 T1: checkbox tap assigns binding.value when no set function', () => {
  const { createCheckbox } = loadSelection();
  const binding = { value: false };
  const handle = createCheckbox({ checked: false, binding });
  handle.tap();
  assert.equal(binding.value, true);
});

test('AC-M3-023 T1: checkbox tap notifies onCheckedChange', () => {
  const { createCheckbox } = loadSelection();
  const seen = [];
  const handle = createCheckbox({ checked: false, onCheckedChange: (v) => seen.push(v) });
  handle.tap();
  assert.deepEqual(seen, [true]);
});

test('AC-M3-023 T1 ASSUMPTION: checkbox read-only binding never breaks the tap path', () => {
  const { createCheckbox } = loadSelection();
  const binding = {};
  Object.defineProperty(binding, 'value', { enumerable: true, get: () => false, set: () => { throw new Error('read-only'); } });
  const handle = createCheckbox({ checked: false, binding });
  const record = handle.tap();
  assert.equal(record.checked, true);
  assert.equal(handle.checked, true);
});

test('AC-M3-023 T1 ASSUMPTION: checkbox throwing listener never breaks the tap path', () => {
  const { createCheckbox } = loadSelection();
  const handle = createCheckbox({ checked: false, onCheckedChange: () => { throw new Error('listener boom'); } });
  const record = handle.tap();
  assert.equal(record.checked, true);
  assert.equal(handle.checked, true);
});

// --- T2: enabled Radio tap sets `selected` (idempotent) through the binding ---

test('AC-M3-023 T2: radio tap sets unselected to selected with one-frame repaint', () => {
  const { createRadio } = loadSelection();
  const handle = createRadio({ selected: false });
  const record = handle.tap();
  assert.equal(record.selected, true);
  assert.equal(record.repainted, 1);
  assert.equal(record.reinstantiated, 0);
  assert.equal(record.frames, 1);
  assert.equal(handle.selected, true);
});

test('AC-M3-023 T2: radio tap on already-selected stays selected', () => {
  const { createRadio } = loadSelection();
  const handle = createRadio({ selected: true });
  const record = handle.tap();
  assert.equal(record.selected, true);
  assert.equal(record.repainted, 1);
  assert.equal(record.reinstantiated, 0);
  assert.equal(record.frames, 1);
  assert.equal(handle.selected, true);
});

test('AC-M3-023 T2: radio tap writes through binding.set', () => {
  const { createRadio } = loadSelection();
  const seen = [];
  const handle = createRadio({ selected: false, binding: { set: (v) => seen.push(v) } });
  handle.tap();
  assert.deepEqual(seen, [true]);
});

test('AC-M3-023 T2: radio tap notifies onSelectedChange', () => {
  const { createRadio } = loadSelection();
  const seen = [];
  const handle = createRadio({ selected: false, onSelectedChange: (v) => seen.push(v) });
  handle.tap();
  assert.deepEqual(seen, [true]);
});

test('AC-M3-023 T2 ASSUMPTION: radio throwing listener never breaks the tap path', () => {
  const { createRadio } = loadSelection();
  const handle = createRadio({ selected: false, onSelectedChange: () => { throw new Error('listener boom'); } });
  const record = handle.tap();
  assert.equal(record.selected, true);
  assert.equal(handle.selected, true);
});

// --- T3: disabled Checkbox/Radio taps change nothing ---

test('AC-M3-023 T3: disabled checkbox tap keeps unchecked with no repaint', () => {
  const { createCheckbox } = loadSelection();
  const handle = createCheckbox({ checked: false, enabled: false });
  const record = handle.tap();
  assert.equal(record.checked, false);
  assert.equal(record.repainted, 0);
  assert.equal(record.reinstantiated, 0);
  assert.equal(record.frames, 1);
  assert.equal(handle.checked, false);
});

test('AC-M3-023 T3: disabled checkbox tap keeps checked with no repaint', () => {
  const { createCheckbox } = loadSelection();
  const handle = createCheckbox({ checked: true, enabled: false });
  const record = handle.tap();
  assert.equal(record.checked, true);
  assert.equal(record.repainted, 0);
  assert.equal(handle.checked, true);
});

test('AC-M3-023 T3: disabled checkbox tap touches neither binding nor listener', () => {
  const { createCheckbox } = loadSelection();
  let writes = 0;
  let calls = 0;
  const handle = createCheckbox({
    checked: false,
    enabled: false,
    binding: { set: () => { writes += 1; } },
    onCheckedChange: () => { calls += 1; },
  });
  handle.tap();
  assert.equal(writes, 0);
  assert.equal(calls, 0);
  assert.equal(handle.checked, false);
});

test('AC-M3-023 T3: disabled radio tap keeps unselected with no repaint', () => {
  const { createRadio } = loadSelection();
  const handle = createRadio({ selected: false, enabled: false });
  const record = handle.tap();
  assert.equal(record.selected, false);
  assert.equal(record.repainted, 0);
  assert.equal(record.reinstantiated, 0);
  assert.equal(record.frames, 1);
  assert.equal(handle.selected, false);
});

// ---------------------------------------------------------------------------
// BUG-M3-FIDELITY-010 T2 — behavioral proof over the T1 matrix (AC-M3-003).
// Hermetic: node:test + node:assert/strict only. Imports the components and
// driveState through the package public entry (index.js); reads the T1
// matrix (engine/layout/m3_matrix.json) from disk. No network, no secrets,
// no services.
//
// ASSUMPTION (opacity seam): driveState layer.props.opacity carries the
// matrix row's measured opacity for the driven state, compared as
// String(row.opacity[stateKey]) so token-role and numeric forms both match.
// ---------------------------------------------------------------------------

const { components: behavioralComponents, driveState } = require('../index.js');
const MATRIX_URL = new URL('../../../engine/layout/m3_matrix.json', import.meta.url);

const OPACITY_STATE_KEYS = { hover: 'hover', focus: 'focus', pressed: 'press' };

function loadMatrixRows() {
  return JSON.parse(readFileSync(MATRIX_URL, 'utf8')).components;
}

function matrixRow(name) {
  const row = loadMatrixRows().find((entry) => entry && entry.name === name);
  if (row === undefined) {
    throw new Error(`matrix: ${name} missing row`);
  }
  return row;
}

// Test helper per the T2 Interface line: one boolean per behavioral facet.
function behavioralRow(name) {
  const row = matrixRow(name);
  for (const field of ['container', 'label', 'opacity', 'shape', 'elevation', 'typescale', 'keyboard', 'motion', 'role']) {
    if (row[field] === undefined) {
      throw new Error(`matrix: ${name} missing ${field}`);
    }
  }
  const descriptor = behavioralComponents[name](REPRESENTATIVE_PROPS[name]);
  const serialized = JSON.stringify(descriptor);
  const tokenOk =
    typeof row.container === 'string' && typeof row.label === 'string' && !/#[0-9a-fA-F]{3,8}/.test(serialized);
  let opacityOk = true;
  for (const [state, key] of Object.entries(OPACITY_STATE_KEYS)) {
    const driven = driveState(name, state);
    if (String(driven.layer.props.opacity) !== String(row.opacity[key])) {
      opacityOk = false;
    }
  }
  const shapeOk = typeof row.shape === 'string' && row.shape.length > 0;
  const elevationOk = typeof row.elevation === 'string' && row.elevation.length > 0;
  const typescaleOk = typeof row.typescale === 'string' && row.typescale.length > 0;
  const keyboardOk = typeof row.keyboard === 'string' && row.keyboard.length > 0;
  const motionOk = typeof row.motion.token === 'string' && typeof row.motion.durationMs === 'number';
  const roleOk = typeof row.role === 'string' && row.role.length > 0;
  return { tokenOk, opacityOk, shapeOk, elevationOk, typescaleOk, keyboardOk, motionOk, roleOk };
}

// Structural composition: only documented direct members (content/layer/
// anchor) are scanned — never an anywhere-`type` walk.
function directMembers(descriptor) {
  const members = [];
  for (const slot of ['content', 'layer', 'anchor']) {
    const value = descriptor[slot];
    if (value !== undefined) {
      members.push(value);
    }
  }
  return members;
}

function assertStructuralComposition(name) {
  const descriptor = behavioralComponents[name](REPRESENTATIVE_PROPS[name]);
  const members = directMembers(descriptor);
  assert.ok(members.length > 0, `${name} descriptor must expose a documented slot (content/layer/anchor)`);
  const types = collectTypes(members);
  assert.ok(types.length > 0, `${name} documented slot must nest at least one tier-1 descriptor`);
  for (const type of types) {
    assert.ok(TIER_1.has(type), `${name} slot instantiates "${type}", which is not a tier-1 primitive`);
  }
}

test('driveState rejects a non-string state with TypeError', () => {
  assert.throws(() => driveState('Button', 42), TypeError);
  assert.throws(() => driveState('Button', undefined), TypeError);
});

test('driveState rejects an unknown state with RangeError', () => {
  assert.throws(() => driveState('Button', 'dragged_into_space'), RangeError);
});

test('driveState returns a Ripple layer for hover/focus/pressed/disabled', () => {
  for (const state of ['hover', 'focus', 'pressed', 'disabled']) {
    const record = driveState('Button', state);
    assert.equal(record.target, 'Button');
    assert.equal(record.state, state);
    assert.equal(record.layer.type, 'Ripple');
    assert.equal(typeof record.layer.props.color, 'string');
  }
});

for (const name of TIER_2) {
  test(`${name} structural composition lives on documented slots only`, () => {
    assertStructuralComposition(name);
  });
}

for (const name of TIER_2) {
  test(`${name} behavioral row is fully measured (token/opacity/shape/elevation/typescale/keyboard/motion/role)`, () => {
    const row = behavioralRow(name);
    assert.deepEqual(row, {
      tokenOk: true,
      opacityOk: true,
      shapeOk: true,
      elevationOk: true,
      typescaleOk: true,
      keyboardOk: true,
      motionOk: true,
      roleOk: true,
    });
  });
}

test('Tooltip layers structurally via layer.type === OverlayHost', () => {
  const descriptor = behavioralComponents.Tooltip({ text: 'Help', anchor: 'target' });
  assert.equal(descriptor.type, 'Tooltip');
  assert.ok(descriptor.layer !== undefined, 'Tooltip descriptor must expose a layer slot');
  assert.equal(descriptor.layer.type, 'OverlayHost');
});

test('behavioral proof fails when the matrix row is absent', () => {
  assert.throws(() => matrixRow('NotAComponent'), /matrix: NotAComponent missing row/);
  assert.throws(() => behavioralRow('NotAComponent'), /matrix: NotAComponent missing row/);
});

test('structural composition rejects a hardcoded literal with no tier-1 member', () => {
  const literal = { type: 'Button', props: { variant: 'filled', label: 'OK' } };
  assert.throws(() => {
    const types = collectTypes(directMembers(literal));
    assert.ok(types.length > 0, 'Button documented slot must nest at least one tier-1 descriptor');
  });
});

// ---------------------------------------------------------------------------
// TC-M3-004 / AC-M3-004: typed tier-2 props with build-time rejection of
// unsupported variants (FR-M3-002).
//
// Hermetic: node:test + node:assert/strict only. Reads muse.manifest.json and
// index.d.ts from disk, drives scripts/check-props.js as a child process
// against tmp-dir fixtures (ASSUMPTION P3: MANIFEST_PATH / DTS_PATH env
// overrides). No network, no secrets, no services, no tsc shell-out here —
// the tsc reject half lives in test-d/negative-variant.ts (@ts-expect-error)
// executed via `npm run check:types -w @muse/native`.
//
// ASSUMPTION A1 (runtime echo vs typed rejection): the JS descriptor
// factories echo props without validation, so "unsupported variant/prop
// fails" is asserted at the contract layer — the manifest `values` lists and
// the index.d.ts literal unions never admit the value, and check-props.js
// exits 1 naming `component.prop` on any drift.
// ---------------------------------------------------------------------------

const CHECK_PROPS_SCRIPT = fileURLToPath(new URL('../scripts/check-props.js', import.meta.url));
const INDEX_DTS_URL = new URL('../index.d.ts', import.meta.url);

function runCheckProps(env = {}) {
  return spawnSync(process.execPath, [CHECK_PROPS_SCRIPT], {
    encoding: 'utf8',
    env: { ...process.env, ...env },
  });
}

function fixtureManifestPath(mutatedManifest) {
  const dir = mkdtempSync(join(tmpdir(), 'muse-tc-m3-004-'));
  const fixtureManifest = join(dir, 'manifest.json');
  writeFileSync(fixtureManifest, JSON.stringify(mutatedManifest));
  return fixtureManifest;
}

test('TC-M3-004: every manifest variant value constructs and echoes (Button/Card/Chip)', () => {
  const manifest = loadManifest();
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  for (const name of ['Button', 'Card', 'Chip']) {
    const values = byName.get(name).props.variant.values;
    assert.ok(Array.isArray(values) && values.length > 0, `${name}.variant must list values`);
    for (const variant of values) {
      const descriptor = components[name]({ variant, label: 'x' });
      assert.equal(descriptor.type, name);
      assert.equal(descriptor.props.variant, variant);
    }
  }
});

test('TC-M3-004: accepted shared keys construct (enabled/selected/error/icon)', () => {
  assert.equal(components.Button({ variant: 'filled', enabled: true }).props.enabled, true);
  assert.equal(components.Chip({ variant: 'assist', selected: true }).props.selected, true);
  assert.equal(components.Checkbox({ checked: true, error: 'bad' }).props.error, 'bad');
  assert.equal(components.Button({ variant: 'filled', icon: 'star' }).props.icon, 'star');
});

test('TC-M3-004: unsupported variant "giant" is absent from every manifest values list', () => {
  const manifest = loadManifest();
  for (const entry of manifest.components) {
    for (const [prop, schema] of Object.entries(entry.props ?? {})) {
      if (schema && Array.isArray(schema.values)) {
        assert.ok(!schema.values.includes('giant'), `${entry.name}.${prop} must not admit "giant"`);
      }
    }
  }
});

test('TC-M3-004: index.d.ts literal unions never admit "giant"', () => {
  const dts = readFileSync(INDEX_DTS_URL, 'utf8');
  assert.ok(!dts.includes("'giant'") && !dts.includes('"giant"'),
    'no d.ts union may admit a "giant" variant');
  const buttonProps = dts.slice(dts.indexOf('ButtonProps'));
  assert.ok(!/variant\?:\s*string\s*[;}]/.test(buttonProps),
    'ButtonProps.variant must be a literal union, never plain string');
});

test('TC-M3-004: unsupported prop key is absent from the manifest schema (Button.giantProp)', () => {
  const manifest = loadManifest();
  const button = manifest.components.find((c) => c.name === 'Button');
  assert.ok(button.props.giantProp === undefined, 'Button.giantProp must not exist in the schema');
});

test('TC-M3-004: check-props.js exits 1 naming Button.variant on variant drift', () => {
  const manifest = loadManifest();
  const drifted = {
    ...manifest,
    components: manifest.components.map((c) =>
      c.name === 'Button'
        ? { ...c, props: { ...c.props, variant: { ...c.props.variant, values: [...c.props.variant.values, 'giant'] } } }
        : c,
    ),
  };
  const res = runCheckProps({
    MANIFEST_PATH: fixtureManifestPath(drifted),
    DTS_PATH: fileURLToPath(INDEX_DTS_URL),
  });
  assert.equal(res.error, undefined, `gate failed to spawn: ${res.error}`);
  assert.equal(res.status, 1, `gate must exit 1 on variant drift; stderr: ${res.stderr}`);
  assert.ok(`${res.stdout ?? ''}${res.stderr ?? ''}`.includes('Button.variant'),
    'gate output must name the drifting component.prop');
});

test('TC-M3-004: check-props.js exits 1 naming Button.giantProp on added-prop drift', () => {
  const manifest = loadManifest();
  const drifted = {
    ...manifest,
    components: manifest.components.map((c) =>
      c.name === 'Button'
        ? { ...c, props: { ...c.props, giantProp: { type: 'string' } } }
        : c,
    ),
  };
  const res = runCheckProps({
    MANIFEST_PATH: fixtureManifestPath(drifted),
    DTS_PATH: fileURLToPath(INDEX_DTS_URL),
  });
  assert.equal(res.error, undefined, `gate failed to spawn: ${res.error}`);
  assert.notEqual(res.status, 0, 'gate must fail when a schema prop drifts from the typed surface');
  assert.ok(`${res.stdout ?? ''}${res.stderr ?? ''}`.includes('Button.giantProp'),
    'gate output must name the added component.prop');
});

test('TC-M3-004: check-props.js exits 0 when manifest and d.ts agree', () => {
  const res = runCheckProps();
  assert.equal(res.error, undefined, `gate failed to spawn: ${res.error}`);
  assert.equal(res.status, 0, `gate must exit 0 on agreement; stderr: ${res.stderr}`);
});

// ---------------------------------------------------------------------------
// TC-M3-024 / AC-M3-023 (T5): onSelectTap pure selection transition (FR-M3-002)
// Hermetic: node:test + node:assert/strict only. `onSelectTap` is loaded
// lazily inside each test via createRequire so the earlier slices stay green
// when index.js does not export it yet (each new test then fails in
// isolation). Tap mechanics themselves are consumed from FR-INPUT-011
// (AC-INPUT-020 tap-fires-once, AC-INPUT-021 one-frame write-through repaint),
// not reimplemented here.
// ---------------------------------------------------------------------------

function loadOnSelectTap() {
  return require('../index.js').onSelectTap;
}

function selectTap(control, prev) {
  const fn = loadOnSelectTap();
  assert.equal(typeof fn, 'function', 'onSelectTap must be exported from the public entry (index.js)');
  return fn(control, prev);
}

test('AC-M3-023 T5: enabled checkbox tap flips unchecked to checked', () => {
  const next = selectTap('Checkbox', { checked: false });
  assert.equal(next.checked, true);
});

test('AC-M3-023 T5: enabled checkbox tap flips checked back to unchecked', () => {
  const next = selectTap('Checkbox', { checked: true });
  assert.equal(next.checked, false);
});

test('AC-M3-023 T5: checkbox defaults to enabled when enabled is absent', () => {
  const next = selectTap('Checkbox', { checked: false });
  assert.equal(next.checked, true);
});

test('AC-M3-023 T5: checkbox result carries no selected key', () => {
  const next = selectTap('Checkbox', { checked: false });
  assert.equal(next.selected, undefined);
});

test('AC-M3-023 T5: enabled radio tap sets unselected to selected', () => {
  const next = selectTap('Radio', { selected: false });
  assert.equal(next.selected, true);
});

test('AC-M3-023 T5: enabled radio tap on already-selected stays selected', () => {
  const next = selectTap('Radio', { selected: true });
  assert.equal(next.selected, true);
});

test('AC-M3-023 T5: radio result carries no checked key', () => {
  const next = selectTap('Radio', { selected: false });
  assert.equal(next.checked, undefined);
});

test('AC-M3-023 T5: disabled checkbox tap returns state unchanged', () => {
  const next = selectTap('Checkbox', { checked: false, enabled: false });
  assert.equal(next.checked, false);
  assert.equal(next.selected, undefined);
});

test('AC-M3-023 T5: disabled checked checkbox tap returns state unchanged', () => {
  const next = selectTap('Checkbox', { checked: true, enabled: false });
  assert.equal(next.checked, true);
});

test('AC-M3-023 T5: disabled radio tap returns state unchanged', () => {
  assert.equal(selectTap('Radio', { selected: false, enabled: false }).selected, false);
  assert.equal(selectTap('Radio', { selected: true, enabled: false }).selected, true);
});

test('AC-M3-023 T5: onSelectTap never mutates its prev argument', () => {
  const prev = Object.freeze({ checked: false, enabled: false });
  const next = selectTap('Checkbox', prev);
  assert.equal(next.checked, false);
  assert.deepEqual({ ...prev }, { checked: false, enabled: false });
});

test('AC-M3-023 T5: onSelectTap agrees with the selection binding (checkbox)', () => {
  const seen = [];
  const handle = loadSelection().createCheckbox({ checked: false, binding: { set: (v) => seen.push(v) } });
  handle.tap();
  const next = selectTap('Checkbox', { checked: false });
  assert.deepEqual(seen, [next.checked]);
});

test('AC-M3-023 T5: onSelectTap agrees with the selection binding (radio)', () => {
  const seen = [];
  const handle = loadSelection().createRadio({ selected: false, binding: { set: (v) => seen.push(v) } });
  handle.tap();
  const next = selectTap('Radio', { selected: false });
  assert.deepEqual(seen, [next.selected]);
});

test('AC-M3-023 T5: chained taps keep one handle with one-frame repaint each step', () => {
  const { createCheckbox } = loadSelection();
  const handle = createCheckbox({ checked: false });
  let prev = { checked: false };
  for (const step of [true, false]) {
    const next = selectTap('Checkbox', prev);
    assert.equal(next.checked, step);
    const record = handle.tap();
    assert.equal(record.checked, step);
    assert.equal(record.frames, 1);
    assert.equal(record.reinstantiated, 0);
    prev = { checked: next.checked };
  }
  assert.equal(handle.checked, false);
});

test('AC-M3-023 T5 ASSUMPTION: unknown control rejects with TypeError', () => {
  assert.throws(() => selectTap('Switch', { checked: false }), TypeError);
});

// ---------------------------------------------------------------------------
// TC-M3-025 / AC-M3-024 (T6): onSliderScrub press-drag scrub (FR-M3-002)
// Hermetic: node:test + node:assert/strict only. Loaded lazily like
// onSelectTap above. The gesture carries no coordinate/delta, so movement is
// observed as claim + clamp: an on-axis non-tap gesture claims the control
// and echoes a clamped value; tap never claims; off-axis-past-slop yields.
// AC-INPUT-020 drag-never-fires-tap and AC-INPUT-021 one-frame repaint are
// consumed from FR-INPUT-011, not reimplemented.
// ---------------------------------------------------------------------------

function loadOnSliderScrub() {
  return require('../index.js').onSliderScrub;
}

function scrub(prev, gesture) {
  const fn = loadOnSliderScrub();
  assert.equal(typeof fn, 'function', 'onSliderScrub must be exported from the public entry (index.js)');
  return fn(prev, gesture);
}

const SCRUB_PRESS = { axis: 'control', slopPast: false, isTap: false };

test('AC-M3-024 T6: on-axis press claims the control with value unchanged', () => {
  const next = scrub({ value: 0.5, min: 0, max: 1 }, SCRUB_PRESS);
  assert.equal(next.claimsGesture, true);
  assert.equal(next.value, 0.5);
});

test('AC-M3-024 T6: in-range value echoes exactly (no bound/position desync)', () => {
  for (const value of [0, 0.25, 1]) {
    const next = scrub({ value, min: 0, max: 1 }, SCRUB_PRESS);
    assert.equal(next.value, value);
    assert.equal(next.claimsGesture, true);
  }
});

test('AC-M3-024 T6: value above max clamps to max', () => {
  const next = scrub({ value: 5, min: 0, max: 1 }, SCRUB_PRESS);
  assert.equal(next.value, 1);
  assert.equal(next.claimsGesture, true);
});

test('AC-M3-024 T6: value below min clamps to min', () => {
  const next = scrub({ value: -2, min: 0, max: 1 }, SCRUB_PRESS);
  assert.equal(next.value, 0);
  assert.equal(next.claimsGesture, true);
});

test('AC-M3-024 T6: degenerate [min,max] range pins value to min', () => {
  const next = scrub({ value: 0.9, min: 0.5, max: 0.5 }, SCRUB_PRESS);
  assert.equal(next.value, 0.5);
});

test('AC-M3-024 T6: tap never claims and changes nothing', () => {
  const next = scrub(
    { value: 0.5, min: 0, max: 1 },
    { axis: 'control', slopPast: false, isTap: true },
  );
  assert.equal(next.claimsGesture, false);
  assert.equal(next.value, 0.5);
});

test('AC-M3-024 T6: tap above max still clamps without claiming', () => {
  const next = scrub(
    { value: 5, min: 0, max: 1 },
    { axis: 'control', slopPast: false, isTap: true },
  );
  assert.equal(next.claimsGesture, false);
  assert.equal(next.value, 1);
});

test('AC-M3-024 T6: off-axis drag past slop yields to scroll', () => {
  const next = scrub(
    { value: 0.5, min: 0, max: 1 },
    { axis: 'off-axis', slopPast: true, isTap: false },
  );
  assert.equal(next.claimsGesture, false);
  assert.equal(next.value, 0.5);
});

test('AC-M3-024 T6 ASSUMPTION: off-axis within slop still claims the control', () => {
  const next = scrub(
    { value: 0.5, min: 0, max: 1 },
    { axis: 'off-axis', slopPast: false, isTap: false },
  );
  assert.equal(next.claimsGesture, true);
  assert.equal(next.value, 0.5);
});

test('AC-M3-024 T6 ASSUMPTION: on-axis past slop is still a control action', () => {
  const next = scrub(
    { value: 0.5, min: 0, max: 1 },
    { axis: 'control', slopPast: true, isTap: false },
  );
  assert.equal(next.claimsGesture, true);
  assert.equal(next.value, 0.5);
});

test('AC-M3-024 T6: disabled slider never changes and never claims', () => {
  const next = scrub({ value: 0.5, min: 0, max: 1, enabled: false }, SCRUB_PRESS);
  assert.equal(next.value, 0.5);
  assert.equal(next.claimsGesture, false);
});

test('AC-M3-024 T6: disabled tap changes nothing and never claims', () => {
  const next = scrub(
    { value: 0.5, min: 0, max: 1, enabled: false },
    { axis: 'control', slopPast: false, isTap: true },
  );
  assert.equal(next.value, 0.5);
  assert.equal(next.claimsGesture, false);
});

test('AC-M3-024 T6: onSliderScrub never mutates its prev argument', () => {
  const prev = Object.freeze({ value: 0.5, min: 0, max: 1 });
  const next = scrub(prev, SCRUB_PRESS);
  assert.equal(next.value, 0.5);
  assert.deepEqual({ ...prev }, { value: 0.5, min: 0, max: 1 });
});

test('AC-M3-024 T6 ASSUMPTION: scrub resolves synchronously (one-frame path, no promise)', () => {
  const next = scrub({ value: 0.5, min: 0, max: 1 }, SCRUB_PRESS);
  assert.equal(typeof next.then, 'undefined');
  assert.equal(typeof next.value, 'number');
});

test('AC-M3-024 T6 ASSUMPTION: missing prev or gesture rejects with TypeError', () => {
  assert.throws(() => scrub(undefined, SCRUB_PRESS), TypeError);
  assert.throws(() => scrub({ value: 0.5, min: 0, max: 1 }, undefined), TypeError);
});

test('AC-M3-024 T6 ASSUMPTION: unknown axis rejects with TypeError', () => {
  assert.throws(
    () => scrub({ value: 0.5, min: 0, max: 1 }, { axis: 'vertical', slopPast: false, isTap: false }),
    TypeError,
  );
});
