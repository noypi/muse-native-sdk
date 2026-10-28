/**
 * TC-M3-005 / TC-M3-006 — the 14 tier-3 composites (FR-M3-003, AC-M3-005/006,
 * RULE-M3-001/003/006) plus the T1 manifest-gate coverage for the 14 tier-3
 * entries (FR-M3-004, AC-M3-007, RULE-M3-003).
 *
 * Hermetic: node:test + node:assert/strict only. Imports the components
 * through the package public entry (index.js), reads muse.manifest.json and
 * composites/*.tsx source text from disk. No network, no secrets, no services.
 *
 * ASSUMPTION C1 (composition traversal): a tier-3 descriptor is a
 * `{ type, props }` record whose nested tier-1/2 descriptors appear as plain
 * objects (or arrays of them) somewhere under `props` or sibling keys —
 * e.g. `content`, `layer`, `anchor`. The test collects every string value
 * held under a `type` key in that graph; the outer type must be the
 * component's own name and every nested type must be a tier-1 or tier-2
 * name — proving real composition with no new native primitive.
 * ASSUMPTION C2 (variants are prop values): TopAppBar documents its sizes
 * through a `size` prop whose values match the manifest schema
 * (small|medium|large); the descriptor echoes the requested size. No
 * `SmallTopAppBar`-style component name is exported.
 * ASSUMPTION C3 (overlay layering): Dialog, BottomSheet, Menu and
 * SnackbarHost render via OverlayHost, so the string "OverlayHost" appears
 * in the JSON form of each overlay member's descriptor.
 * ASSUMPTION G1 (manifest gate parity): check-manifest.js compares manifest
 * entry names against the `components` export keys in both directions; the
 * manifest/export parity tests below encode the same contract without
 * spawning the script.
 * ASSUMPTION G2 (import closure): tier-3 composites may import only
 * `@muse/core` and `@muse/native`; the source-text scan below encodes the
 * same contract the T3 build gate enforces.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync, readdirSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { components } = require('../index.js');
const MANIFEST_URL = new URL('../muse.manifest.json', import.meta.url);
const COMPOSITES_URL = new URL('../composites/', import.meta.url);

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

const TIER_2 = new Set([
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
  'TextInput',
]);

const TIER_3 = [
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

const OVERLAY_MEMBERS = ['Dialog', 'BottomSheet', 'Menu', 'SnackbarHost'];

// Minimal representative props per composite (keys must exist in the manifest).
const REPRESENTATIVE_PROPS = {
  TopAppBar: { size: 'small', title: 'Title' },
  NavigationBar: { selected: 'home' },
  NavigationRail: { selected: 'home' },
  NavigationDrawer: { selected: 'home' },
  SegmentedButton: { selected: 'one' },
  Dialog: { open: true, title: 'Confirm' },
  BottomSheet: { open: true },
  Menu: { open: true, anchor: 'target' },
  SnackbarHost: { message: 'Saved' },
  SearchBar: { value: 'query' },
  Carousel: { index: 0 },
  BottomAppBar: { title: 'Bar' },
  ButtonGroup: { selected: 'one' },
  SplitButton: { label: 'Save' },
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
// T1 manifest gate (FR-M3-004, AC-M3-007, RULE-M3-003): 41 entries, tier-3
// shape, manifest<->export parity
// ---------------------------------------------------------------------------

test('manifest is valid JSON with canonical package name', () => {
  const manifest = loadManifest();
  assert.equal(manifest.name, '@muse/native');
});

test('manifest contains exactly 41 components', () => {
  const manifest = loadManifest();
  assert.equal(manifest.components.length, 41);
});

test('manifest contains all 14 tier-3 composites', () => {
  const manifest = loadManifest();
  const names = new Set(manifest.components.map((c) => c?.name));
  for (const name of TIER_3) {
    assert.ok(names.has(name), `manifest entry missing for tier-3 composite: ${name}`);
  }
});

test('manifest has no duplicate component entries', () => {
  const manifest = loadManifest();
  const names = manifest.components.map((c) => c?.name);
  assert.equal(new Set(names).size, names.length);
});

for (const name of TIER_3) {
  test(`${name} manifest entry declares tier 3 with a non-empty base and props record`, () => {
    const manifest = loadManifest();
    const entry = manifest.components.find((c) => c?.name === name);
    assert.ok(entry, `manifest entry missing: ${name}`);
    assert.equal(entry.tier, 3, `${name} tier must be 3`);
    assert.equal(typeof entry.base, 'string', `${name} base must be a string`);
    assert.ok(entry.base.length > 0, `${name} base must be non-empty`);
    assert.ok(entry.props !== null && typeof entry.props === 'object', `${name} props must be a record`);
  });
}

test('tier-3 base names compose the tier-1/2 surface, never a new native primitive', () => {
  const manifest = loadManifest();
  const allowed = new Set([...TIER_1, ...TIER_2]);
  for (const entry of manifest.components.filter((c) => c?.tier === 3)) {
    assert.ok(allowed.has(entry.base), `${entry.name} base "${entry.base}" is not a tier-1/2 name (new native primitive)`);
    assert.notEqual(entry.base, entry.name, `${entry.name} base must not be itself`);
  }
});

test('TopAppBar manifest schema documents size small|medium|large', () => {
  const manifest = loadManifest();
  const entry = manifest.components.find((c) => c?.name === 'TopAppBar');
  assert.ok(entry, 'manifest entry missing: TopAppBar');
  assert.deepEqual(entry.props.size.values, ['small', 'medium', 'large']);
});

test('tier-3 prop schemas use valid types with optional values and required', () => {
  const manifest = loadManifest();
  const valid = new Set(['string', 'boolean', 'number', 'token', 'slot', 'object', 'function']);
  for (const entry of manifest.components.filter((c) => c?.tier === 3)) {
    for (const [propName, schema] of Object.entries(entry.props ?? {})) {
      assert.ok(schema && valid.has(schema.type), `${entry.name}.${propName} has invalid type`);
      if (schema.values !== undefined) {
        assert.ok(Array.isArray(schema.values), `${entry.name}.${propName} values must be an array`);
        for (const v of schema.values) assert.equal(typeof v, 'string');
      }
      if (schema.required !== undefined) {
        assert.equal(typeof schema.required, 'boolean', `${entry.name}.${propName} required must be boolean`);
      }
    }
  }
});

test('overlay tier-3 members expose an OverlayHost-backed slot in the manifest schema', () => {
  const manifest = loadManifest();
  for (const name of OVERLAY_MEMBERS) {
    const entry = manifest.components.find((c) => c?.name === name);
    assert.ok(entry, `manifest entry missing: ${name}`);
    const slots = Object.entries(entry.props ?? {}).filter(([, s]) => s?.type === 'slot');
    assert.ok(slots.length > 0, `${name} manifest schema must expose a slot prop for OverlayHost layering`);
  }
});

test('tier-3 representative prop keys exist in the manifest schemas', () => {
  const manifest = loadManifest();
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  for (const name of TIER_3) {
    assert.ok(byName.has(name), `manifest entry missing for tier-3 composite: ${name}`);
    const schemaKeys = new Set(Object.keys(byName.get(name).props ?? {}));
    for (const key of Object.keys(REPRESENTATIVE_PROPS[name])) {
      assert.ok(schemaKeys.has(key), `${name}.${key} is not in the manifest schema`);
    }
  }
});

test('manifest entries match component exports in both directions (check-manifest.js parity)', () => {
  const manifest = loadManifest();
  const manifestNames = new Set(manifest.components.map((c) => c?.name));
  const exportNames = new Set(Object.keys(components));
  const missing = [...manifestNames].filter((n) => !exportNames.has(n));
  const orphans = [...exportNames].filter((n) => !manifestNames.has(n));
  assert.deepEqual(missing, [], `missing component export for manifest entries: ${missing.join(', ')}`);
  assert.deepEqual(orphans, [], `orphan component export with no manifest entry: ${orphans.join(', ')}`);
});

// ---------------------------------------------------------------------------
// AC-M3-005: all 14 tier-3 composites construct over tier-1/2 only
// ---------------------------------------------------------------------------

test('all 14 tier-3 composites are exported', () => {
  for (const name of TIER_3) {
    assert.equal(typeof components[name], 'function', `composite not exported: ${name}`);
  }
});

for (const name of TIER_3) {
  test(`${name} constructs a descriptor from representative props`, () => {
    const descriptor = components[name](REPRESENTATIVE_PROPS[name]);
    assert.equal(descriptor.type, name, `${name} descriptor type must equal its name`);
    assert.deepEqual(descriptor.props, REPRESENTATIVE_PROPS[name]);
  });
}

test('all 14 tier-3 composites construct with no props (props default to {})', () => {
  for (const name of TIER_3) {
    const descriptor = components[name]();
    assert.equal(descriptor.type, name);
    assert.deepEqual(descriptor.props, {});
  }
});

for (const size of ['small', 'medium', 'large']) {
  test(`TopAppBar constructs with size "${size}"`, () => {
    const descriptor = components.TopAppBar({ size, title: 'Title' });
    assert.equal(descriptor.type, 'TopAppBar');
    assert.equal(descriptor.props.size, size);
  });
}

for (const name of TIER_3) {
  test(`${name} composes tier-1/2 descriptors only (no new native primitive)`, () => {
    const descriptor = components[name](REPRESENTATIVE_PROPS[name]);
    const types = collectTypes(descriptor);
    assert.ok(types.length > 0, `${name} descriptor must carry a type`);
    assert.equal(types[0], name, `${name} outer descriptor type must equal its name`);
    const nested = types.filter((type) => type !== name);
    assert.ok(
      nested.length > 0,
      `${name} descriptor must nest at least one tier-1/2 descriptor (composition over tier-1/2)`,
    );
    for (const type of nested) {
      assert.ok(
        TIER_1.has(type) || TIER_2.has(type),
        `${name} instantiates "${type}", which is not a tier-1/2 name (new native primitive registered)`,
      );
    }
  });
}

// ---------------------------------------------------------------------------
// AC-M3-006: documented configurations render; variant/state props forward
// ---------------------------------------------------------------------------

test('variant and state props forward untouched to the descriptor', () => {
  const bar = components.NavigationBar({ selected: 'search', enabled: false });
  assert.equal(bar.props.selected, 'search');
  assert.equal(bar.props.enabled, false);
  const segmented = components.SegmentedButton({ selected: 'two', enabled: true });
  assert.equal(segmented.props.selected, 'two');
  assert.equal(segmented.props.enabled, true);
  const sheet = components.BottomSheet({ open: false });
  assert.equal(sheet.props.open, false);
  const search = components.SearchBar({ value: 'hello', enabled: true });
  assert.equal(search.props.value, 'hello');
  assert.equal(search.props.enabled, true);
});

test('TopAppBar renders all three documented sizes distinctly', () => {
  const seen = new Set();
  for (const size of ['small', 'medium', 'large']) {
    const descriptor = components.TopAppBar({ size, title: 'Title' });
    assert.equal(descriptor.props.size, size);
    seen.add(JSON.stringify(descriptor));
  }
  assert.equal(seen.size, 3, 'TopAppBar must render three distinct configurations');
});

test('Dialog renders open and closed configurations', () => {
  const open = components.Dialog({ open: true, title: 'Confirm' });
  const closed = components.Dialog({ open: false, title: 'Confirm' });
  assert.equal(open.props.open, true);
  assert.equal(closed.props.open, false);
  assert.notDeepEqual(open, closed);
});

// ---------------------------------------------------------------------------
// RULE-M3-006: overlay members layer via OverlayHost
// ---------------------------------------------------------------------------

for (const name of OVERLAY_MEMBERS) {
  test(`${name} layers via OverlayHost`, () => {
    const descriptor = components[name](REPRESENTATIVE_PROPS[name]);
    assert.equal(descriptor.type, name);
    assert.ok(
      JSON.stringify(descriptor).includes('OverlayHost'),
      `${name} descriptor must layer via OverlayHost`,
    );
  });
}

// ---------------------------------------------------------------------------
// RULE-M3-001 import closure: composites import only @muse/core and
// @muse/native, and register no native primitive
// ---------------------------------------------------------------------------

test('composites directory holds one TSX file per composite', () => {
  for (const name of TIER_3) {
    const url = new URL(`./${name}.tsx`, COMPOSITES_URL);
    assert.ok(existsSync(url), `composite source missing: composites/${name}.tsx`);
  }
});

test('tier-3 composite sources import only @muse/core and @muse/native', () => {
  const allowed = new Set(['@muse/core', '@muse/native']);
  const files = readdirSync(COMPOSITES_URL).filter((f) => f.endsWith('.tsx') || f.endsWith('.ts'));
  assert.ok(files.length >= 14, 'composites directory must hold at least 14 sources');
  for (const file of files) {
    const source = readFileSync(new URL(`./${file}`, COMPOSITES_URL), 'utf8');
    const imports = [...source.matchAll(/(?:import|require)\s*[(]?\s*['"]([^'"]+)['"]/g)].map((m) => m[1]);
    const relative = imports.filter((s) => s.startsWith('.') || s.startsWith('/'));
    assert.deepEqual(relative, [], `${file} must not use relative imports outside the package`);
    for (const spec of imports) {
      if (spec.startsWith('.') || spec.startsWith('/') || spec.startsWith('node:')) continue;
      assert.ok(allowed.has(spec), `${file} imports "${spec}" outside @muse/core and @muse/native`);
    }
    assert.ok(
      !/registerNative|registerPrimitive|NativeModules\s*\./.test(source),
      `${file} must not register a native primitive`,
    );
  }
});

// ---------------------------------------------------------------------------
// Boundary/error: unknown variant-style names are not exported
// ---------------------------------------------------------------------------

test('unknown variant-style composite names are not exported', () => {
  assert.equal(components.SmallTopAppBar, undefined);
  assert.equal(components.TopAppBarSmall, undefined);
  assert.equal(components.ModalDialog, undefined);
  assert.equal(components.NotAComponent, undefined);
});

test('accessing an unknown tier-3 composite cannot construct a descriptor', () => {
  assert.throws(() => {
    components.NotAComponent({});
  }, TypeError);
});

// ---------------------------------------------------------------------------
// T3 build gates: typed prop layer + scripts (FR-M3-003 AC-M3-005,
// FR-M3-004 AC-M3-008, FR-M3-005 AC-M3-009).
//
// ASSUMPTION P3 (type-layer check, mirrors tier1/tier2 ASSUMPTION P2):
// index.d.ts declares one `<Name>Props` type per tier-3 composite generated
// from the T1 manifest schemas (token→string, slot→unknown,
// values→string-literal union), a `Tier3Name` union over all 14 names, and
// factory declarations for the 14 composites. The check-props.js spawn test
// below encodes the same manifest↔d.ts contract the T3 gate enforces.
// ASSUMPTION G3 (gate testability, mirrors tier1 ASSUMPTION G3):
// scripts/check-tokens.js resolves the scanned library root from LIB_PATH
// when set; fixtures use tmp-dir copies so production files are never
// mutated. The gate must scan composites/*.tsx — a smuggled literal there
// fails the gate naming file:line.
// ---------------------------------------------------------------------------

const DTS_URL = new URL('../index.d.ts', import.meta.url);
const PKG_URL = new URL('../package.json', import.meta.url);

function loadDts() {
  return readFileSync(DTS_URL, 'utf8');
}

function propsBody(dts, component) {
  const re = new RegExp(`export\\s+type\\s+${component}Props\\s*=\\s*\\{([^}]*)\\}`, 's');
  const m = re.exec(dts);
  return m ? m[1] : null;
}

test('index.d.ts declares one Props type per tier-3 composite', () => {
  const dts = loadDts();
  for (const name of TIER_3) {
    assert.ok(
      dts.includes(`${name}Props`),
      `index.d.ts must declare per-component prop types generated from the manifest (${name}Props missing)`,
    );
  }
});

test('index.d.ts declares the Tier3Name union over all 14 composites', () => {
  const dts = loadDts();
  assert.ok(dts.includes('Tier3Name'), 'index.d.ts must declare a Tier3Name union for tier-3');
  for (const name of TIER_3) {
    assert.ok(
      dts.includes(`'${name}'`),
      `Tier3Name union must include '${name}'`,
    );
  }
});

test('index.d.ts components record covers the Tier3Name union', () => {
  const dts = loadDts();
  const m = /export\s+declare\s+const\s+components\s*:\s*Record<([^;]+)>/.exec(dts);
  assert.ok(m, 'index.d.ts must declare the components record');
  assert.ok(
    m[1].includes('Tier3Name'),
    'components record must cover Tier3Name so the 14 composites are typed factories',
  );
});

test('TopAppBar d.ts size union equals the manifest size values list', () => {
  const manifest = loadManifest();
  const entry = manifest.components.find((c) => c?.name === 'TopAppBar');
  assert.ok(entry, 'manifest entry missing: TopAppBar');
  const body = propsBody(loadDts(), 'TopAppBar');
  assert.ok(body, 'index.d.ts must declare a TopAppBarProps type');
  for (const value of entry.props.size.values) {
    assert.ok(
      body.includes(`'${value}'`),
      `TopAppBarProps size union must include '${value}' from the manifest values list`,
    );
  }
});

test('tier-3 d.ts values unions cover every manifest values entry', () => {
  const manifest = loadManifest();
  const dts = loadDts();
  for (const entry of manifest.components.filter((c) => c?.tier === 3)) {
    const body = propsBody(dts, entry.name);
    assert.ok(body, `index.d.ts must declare a ${entry.name}Props type`);
    for (const [propName, schema] of Object.entries(entry.props ?? {})) {
      if (schema && Array.isArray(schema.values)) {
        for (const value of schema.values) {
          assert.ok(
            body.includes(`'${value}'`),
            `${entry.name}Props ${propName} union must include '${value}' from the manifest values list`,
          );
        }
      }
    }
  }
});

test('check-props.js exits 0 on the real tree (manifest↔d.ts match incl. tier-3)', () => {
  const { execFileSync } = require('node:child_process');
  const { fileURLToPath } = require('node:url');
  const scriptPath = fileURLToPath(new URL('../scripts/check-props.js', import.meta.url));
  const out = execFileSync(process.execPath, [scriptPath], { encoding: 'utf8' });
  assert.ok(out.includes('OK'), 'prop gate must report OK when d.ts matches the manifest incl. tier-3');
});

test('check-manifest.js exits 0 on the real tree (exact match incl. tier-3)', () => {
  const { execFileSync } = require('node:child_process');
  const { fileURLToPath } = require('node:url');
  const scriptPath = fileURLToPath(new URL('../scripts/check-manifest.js', import.meta.url));
  const out = execFileSync(process.execPath, [scriptPath], { encoding: 'utf8' });
  assert.ok(out.includes('OK'), 'manifest gate must report OK on the exact manifest↔export match incl. tier-3');
});

test('check-tokens.js --scope lib exits 0 on the clean library tree', () => {
  const { execFileSync } = require('node:child_process');
  const { fileURLToPath } = require('node:url');
  const scriptPath = fileURLToPath(new URL('../scripts/check-tokens.js', import.meta.url));
  const out = execFileSync(process.execPath, [scriptPath, '--scope', 'lib'], {
    encoding: 'utf8',
  });
  assert.ok(out.includes('OK'), 'token gate must report OK on the clean tree');
});

test('check-tokens.js --scope lib exits 1 naming file:line for a smuggled literal in a .tsx composite', () => {
  const { execFileSync } = require('node:child_process');
  const { fileURLToPath } = require('node:url');
  const fs = require('node:fs');
  const os = require('node:os');
  const path = require('node:path');
  const scriptPath = fileURLToPath(new URL('../scripts/check-tokens.js', import.meta.url));
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-lib-'));
  try {
    fs.writeFileSync(
      path.join(dir, 'Smuggled.tsx'),
      'export const Smuggled = { color: "#ff0000", margin: "8px" };\n',
    );
    let err = null;
    try {
      execFileSync(process.execPath, [scriptPath, '--scope', 'lib'], {
        encoding: 'utf8',
        env: { ...process.env, LIB_PATH: dir },
      });
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'token gate must fail when a literal is smuggled into a .tsx composite');
    assert.equal(err.status, 1, 'token gate must exit 1 on a smuggled literal');
    assert.ok(
      /Smuggled\.tsx:\d+/.test(String(err.stdout) + String(err.stderr)),
      'token gate must list file:line of the smuggled literal',
    );
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('package.json declares the test:tier3 script running node --test test/tier3.test.ts', () => {
  const pkg = JSON.parse(readFileSync(PKG_URL, 'utf8'));
  const script = pkg.scripts && pkg.scripts['test:tier3'];
  assert.ok(script, 'package.json must declare a test:tier3 script (T3 creates it)');
  assert.ok(
    script.includes('test/tier3.test.ts'),
    'test:tier3 must run node --test test/tier3.test.ts',
  );
});

// ---------------------------------------------------------------------------
// Review MINOR (Stage 3 fix): composites/*.tsx ↔ index.js tier-3 "mirror" is
// enforced — drift fails the suite.
//
// The .tsx sources return raw `{ type, props }` literals while the index.js
// factories call the tier-1/2 factories (whose withStateLayer injects a
// `stateLayer` key only when a nested prop named `state` is a string — no
// tier-3 representative prop set passes one, so the comparison below is
// unaffected). Exact deep-equality between the two graphs would couple the
// test to that mechanism difference, so source-text comparison is asserted
// only where the contract is mechanism-independent, and the rest is asserted
// on runtime behavior:
//
//  1. the .tsx source declares `export function <Name>` with outer
//     `type: '<Name>'` and echoes caller props via `props: next` (same
//     contract the runtime echo tests assert on index.js);
//  2. every `type: '…'` literal in the .tsx source is the composite's own
//     name or a tier-1/2 name (no new native primitive on the .tsx side);
//  3. the .tsx nested-type SET equals the index.js descriptor's DIRECT
//     composition set (one entry per composed member, not descending into a
//     nested tier-2 member's own tier-1 expansion — which is why the .tsx
//     side, written as one literal per member, matches exactly). Adding OR
//     removing a nested member on either side without the other fails;
//  4. OverlayHost presence agrees on both sides (and matches overlay
//     membership), so an overlay member that stops layering — on either
//     side — fails.
//
// ASSUMPTION M1 (mirror traversal): .tsx nested descriptors are written as
// `type: '<Name>'` literals; the regex below collects them. A future .tsx
// that computes a type string dynamically would need this collector
// extended — the runtime-behavior assertions (outer type, echo, tier-1/2
// closure, OverlayHost) still hold regardless.
// ---------------------------------------------------------------------------

function tsxTypeLiterals(source) {
  const out = [];
  const re = /type:\s*['"]([A-Za-z][A-Za-z0-9]*)['"]/g;
  let m;
  while ((m = re.exec(source)) !== null) out.push(m[1]);
  return out;
}

// Direct composition members of a runtime descriptor: the `type` of each
// nested descriptor found under sibling keys beside `props`, without
// descending into a composed descriptor's own expansion (so a nested
// tier-2 member counts once as itself, matching how the .tsx source writes
// one literal per composed member).
function directNestedTypes(descriptor) {
  const out = [];
  const visit = (node) => {
    if (Array.isArray(node)) {
      for (const item of node) visit(item);
      return;
    }
    if (node !== null && typeof node === 'object') {
      if (typeof node.type === 'string') return void out.push(node.type);
      for (const value of Object.values(node)) visit(value);
    }
  };
  for (const [key, value] of Object.entries(descriptor)) {
    if (key === 'props') continue;
    visit(value);
  }
  return out;
}

for (const name of TIER_3) {
  test(`${name} mirror: tsx source and index.js factory agree (outer type, echo, nested tier-1/2 set, OverlayHost)`, () => {
    const source = readFileSync(new URL(`./${name}.tsx`, COMPOSITES_URL), 'utf8');
    assert.ok(
      source.includes(`export function ${name}`),
      `composites/${name}.tsx must declare export function ${name}`,
    );
    assert.ok(
      source.includes(`type: '${name}'`),
      `composites/${name}.tsx outer type must equal its name`,
    );
    assert.ok(
      /props:\s*next\b/.test(source),
      `composites/${name}.tsx must echo caller props via props: next`,
    );
    const literals = tsxTypeLiterals(source);
    assert.ok(literals.length > 0, `composites/${name}.tsx must carry a type literal`);
    assert.ok(
      literals.includes(name),
      `composites/${name}.tsx must hold its own outer type literal`,
    );
    for (const type of literals) {
      assert.ok(
        type === name || TIER_1.has(type) || TIER_2.has(type),
        `composites/${name}.tsx nests "${type}", which is not a tier-1/2 name (new native primitive)`,
      );
    }
    const descriptor = components[name](REPRESENTATIVE_PROPS[name]);
    assert.equal(descriptor.type, name, `${name} runtime outer type must equal its name`);
    assert.deepEqual(descriptor.props, REPRESENTATIVE_PROPS[name]);
    assert.deepEqual(
      [...new Set(directNestedTypes(descriptor))].sort(),
      [...new Set(literals.filter((type) => type !== name))].sort(),
      `${name} mirror drift: composites/${name}.tsx nested type set differs from the index.js direct composition set`,
    );
    const tsxHasOverlay = source.includes('OverlayHost');
    const runtimeHasOverlay = JSON.stringify(descriptor).includes('OverlayHost');
    assert.equal(
      runtimeHasOverlay,
      tsxHasOverlay,
      `${name} mirror drift: OverlayHost presence disagrees (tsx=${tsxHasOverlay}, index.js=${runtimeHasOverlay})`,
    );
    assert.equal(
      runtimeHasOverlay,
      OVERLAY_MEMBERS.includes(name),
      `${name} OverlayHost layering must match overlay membership`,
    );
  });
}

// ---------------------------------------------------------------------------
// T2 build gate probes (check-composites.js, MAJOR fix): import closure and
// no-native-registration now fail the build, not just this suite. The spawn
// probes below follow this file's conventions (execFileSync, tmp-dir
// fixtures via COMPOSITES_DIR override, production files never mutated).
// ---------------------------------------------------------------------------

test('check-composites.js exits 0 on the real tree (import-closed, no native registration)', () => {
  const { execFileSync } = require('node:child_process');
  const { fileURLToPath } = require('node:url');
  const scriptPath = fileURLToPath(new URL('../scripts/check-composites.js', import.meta.url));
  const out = execFileSync(process.execPath, [scriptPath], { encoding: 'utf8' });
  assert.ok(out.includes('OK'), 'composites gate must report OK on the clean tree');
});

test('check-composites.js exits 1 naming file:line for a smuggled third-package import (tmp fixture)', () => {
  const { execFileSync } = require('node:child_process');
  const { fileURLToPath } = require('node:url');
  const fs = require('node:fs');
  const os = require('node:os');
  const path = require('node:path');
  const scriptPath = fileURLToPath(new URL('../scripts/check-composites.js', import.meta.url));
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-composites-'));
  try {
    fs.writeFileSync(
      path.join(dir, 'Smuggled.tsx'),
      'import \'evil-pkg\';\nexport const Smuggled = { type: \'Smuggled\', props: {} };\n',
    );
    let err = null;
    try {
      execFileSync(process.execPath, [scriptPath], {
        encoding: 'utf8',
        env: { ...process.env, COMPOSITES_DIR: dir },
      });
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'composites gate must fail when a third-package import is smuggled in');
    assert.equal(err.status, 1, 'composites gate must exit 1 on a smuggled import');
    assert.ok(
      /Smuggled\.tsx:\d+/.test(String(err.stdout) + String(err.stderr)),
      'composites gate must list file:line of the smuggled import',
    );
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('check-composites.js exits 1 naming file:line for a smuggled native registration (tmp fixture)', () => {
  const { execFileSync } = require('node:child_process');
  const { fileURLToPath } = require('node:url');
  const fs = require('node:fs');
  const os = require('node:os');
  const path = require('node:path');
  const scriptPath = fileURLToPath(new URL('../scripts/check-composites.js', import.meta.url));
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-composites-'));
  try {
    fs.writeFileSync(
      path.join(dir, 'Smuggled.tsx'),
      'export const Smuggled = { type: \'Smuggled\', props: {} };\nregisterNative(\'Smuggled\');\n',
    );
    let err = null;
    try {
      execFileSync(process.execPath, [scriptPath], {
        encoding: 'utf8',
        env: { ...process.env, COMPOSITES_DIR: dir },
      });
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'composites gate must fail when a native registration is smuggled in');
    assert.equal(err.status, 1, 'composites gate must exit 1 on a smuggled native registration');
    assert.ok(
      /Smuggled\.tsx:\d+/.test(String(err.stdout) + String(err.stderr)),
      'composites gate must list file:line of the smuggled native registration',
    );
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

// ---------------------------------------------------------------------------
// Stage 4 follow-up part 2 (hasOwnProperty allowlist hardening): TopAppBar
// size lookup falls back to 'title-large' for non-allowlisted keys.
// Finds the nested Text variant via directNestedTypes-adjacent traversal
// (first Text descriptor under sibling keys beside `props`).
// ---------------------------------------------------------------------------

function topAppBarTitleVariant(descriptor) {
  const visit = (node) => {
    if (Array.isArray(node)) {
      for (const item of node) {
        const found = visit(item);
        if (found !== undefined) return found;
      }
      return undefined;
    }
    if (node !== null && typeof node === 'object') {
      if (node.type === 'Text' && node.props !== null && typeof node.props === 'object') {
        return node.props.variant;
      }
      for (const value of Object.values(node)) {
        const found = visit(value);
        if (found !== undefined) return found;
      }
    }
    return undefined;
  };
  for (const [key, value] of Object.entries(descriptor)) {
    if (key === 'props') continue;
    const found = visit(value);
    if (found !== undefined) return found;
  }
  return undefined;
}

test('TopAppBar size "__proto__" falls back to the title-large string variant', () => {
  const descriptor = components.TopAppBar({ size: '__proto__', title: 'Title' });
  const variant = topAppBarTitleVariant(descriptor);
  assert.equal(variant, 'title-large');
  assert.equal(typeof variant, 'string');
});

test('TopAppBar size "giant" falls back to the title-large string variant', () => {
  const descriptor = components.TopAppBar({ size: 'giant', title: 'Title' });
  const variant = topAppBarTitleVariant(descriptor);
  assert.equal(variant, 'title-large');
  assert.equal(typeof variant, 'string');
});

test('TopAppBar title variant is always a string in the documented set', () => {
  const allowed = new Set(['title-large', 'headline-small', 'headline-medium']);
  for (const size of ['small', 'medium', 'large', '__proto__', 'constructor', 'giant', '']) {
    const descriptor = components.TopAppBar({ size, title: 'Title' });
    const variant = topAppBarTitleVariant(descriptor);
    assert.equal(typeof variant, 'string', `size "${size}" variant must be a string`);
    assert.ok(allowed.has(variant), `size "${size}" variant "${variant}" must be in the documented set`);
  }
});

test('TopAppBar prototype lookups leave Object.prototype unpolluted', () => {
  components.TopAppBar({ size: '__proto__', title: 'Title' });
  components.TopAppBar({ size: 'constructor', title: 'Title' });
  components.TopAppBar({ size: 'giant', title: 'Title' });
  assert.equal(Object.prototype.titleLarge, undefined);
  assert.equal({}.titleLarge, undefined);
  assert.equal(Object.prototype.variant, undefined);
});
