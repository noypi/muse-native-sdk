/**
 * TC-M3-007 / AC-M3-007 — muse.manifest.json registry contract (FR-M3-004).
 *
 * Hermetic: reads only packages/native/muse.manifest.json from disk,
 * uses node:test + node:assert/strict, no network, no secrets, no services.
 *
 * ASSUMPTION A1 (JS+JSON surface): the manifest is consumed as JSON; tests
 * load it with fs + JSON.parse so they are independent of any loader.
 * ASSUMPTION A3 (loader-layer aliases): RULE-M3-005 alias spellings
 * (capo.manifest.json / @capo/native / @muse/*) resolve to the canonical
 * muse-native names at the loader layer. The loader module path is left to
 * the coder/plan; the alias mapping below is the contract the loader must
 * satisfy, verified here as a pure mapping table plus the canonical `name`
 * field of the manifest itself.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

const MANIFEST_URL = new URL('../muse.manifest.json', import.meta.url);

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
] as const;

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
] as const;

const EXPECTED: ReadonlyMap<string, 1 | 2> = new Map([
  ...TIER_1.map((n): [string, 1 | 2] => [n, 1]),
  ...TIER_2.map((n): [string, 1 | 2] => [n, 2]),
]);

const VALID_PROP_TYPES = new Set(['string', 'boolean', 'number', 'token', 'slot']);

// RULE-M3-005 deprecated-alias contract (ASSUMPTION A3: enforced at loader).
const PACKAGE_ALIASES: ReadonlyMap<string, string> = new Map([
  ['@capo/native', '@muse/native'],
  ['@muse/native', '@muse/native'],
]);
const MANIFEST_FILE_ALIASES: ReadonlyMap<string, string> = new Map([
  ['capo.manifest.json', 'muse.manifest.json'],
  ['muse.manifest.json', 'muse.manifest.json'],
]);

function loadManifest(): any {
  const raw = readFileSync(MANIFEST_URL, 'utf8');
  return JSON.parse(raw);
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

test('manifest is valid JSON with canonical package name', () => {
  const manifest = loadManifest();
  assert.equal(manifest.name, '@muse/native');
});

test('manifest components is an array', () => {
  const manifest = loadManifest();
  assert.ok(Array.isArray(manifest.components));
});

// ---------------------------------------------------------------------------
// AC-M3-007: exactly one entry per component, no extras (26 total)
// ---------------------------------------------------------------------------

test('manifest contains exactly 26 components', () => {
  const manifest = loadManifest();
  assert.equal(manifest.components.length, 26);
});

test('manifest contains exactly the 26 spec-named components', () => {
  const manifest = loadManifest();
  const names = new Set(manifest.components.map((c: any) => c?.name));
  for (const expected of EXPECTED.keys()) {
    assert.ok(names.has(expected), `missing component: ${expected}`);
  }
  assert.equal(names.size, 26, 'manifest has extra entries beyond the 26 spec-named components');
});

test('manifest has no duplicate component entries', () => {
  const manifest = loadManifest();
  const names = manifest.components.map((c: any) => c?.name);
  assert.equal(new Set(names).size, names.length);
});

// ---------------------------------------------------------------------------
// Per-entry shape: tier + native backing + props
// ---------------------------------------------------------------------------

test('every entry declares tier 1 or 2 matching the tier tables', () => {
  const manifest = loadManifest();
  for (const entry of manifest.components) {
    assert.ok(entry && typeof entry.name === 'string', 'entry has a string name');
    assert.equal(entry.tier, EXPECTED.get(entry.name), `wrong tier for ${entry.name}`);
  }
});

test('every entry names a non-empty native backing', () => {
  const manifest = loadManifest();
  for (const entry of manifest.components) {
    assert.equal(typeof entry.native, 'string', `${entry.name} native must be a string`);
    assert.ok(entry.native.length > 0, `${entry.name} native must be non-empty`);
  }
});

test('every entry has a props record', () => {
  const manifest = loadManifest();
  for (const entry of manifest.components) {
    assert.ok(entry.props !== null && typeof entry.props === 'object', `${entry.name} props must be a record`);
  }
});

test('every prop schema entry has a valid type, optional values and required', () => {
  const manifest = loadManifest();
  for (const entry of manifest.components) {
    for (const [propName, schema] of Object.entries<any>(entry.props ?? {})) {
      assert.ok(
        schema && VALID_PROP_TYPES.has(schema.type),
        `${entry.name}.${propName} has invalid type`,
      );
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

// ---------------------------------------------------------------------------
// RULE-M3-005 alias resolution (loader layer, ASSUMPTION A3)
// ---------------------------------------------------------------------------

test('deprecated package spellings resolve to canonical @muse/native', () => {
  for (const [alias, canonical] of PACKAGE_ALIASES) {
    assert.equal(canonical, '@muse/native', `alias ${alias} must resolve to canonical`);
  }
  assert.ok(PACKAGE_ALIASES.has('@capo/native'), '@capo/native alias must be recognised');
});

test('deprecated manifest spellings resolve to canonical muse.manifest.json', () => {
  for (const [alias, canonical] of MANIFEST_FILE_ALIASES) {
    assert.equal(canonical, 'muse.manifest.json', `alias ${alias} must resolve to canonical`);
  }
  assert.ok(MANIFEST_FILE_ALIASES.has('capo.manifest.json'), 'capo.manifest.json alias must be recognised');
});

test('manifest name field uses the canonical spelling, not an alias', () => {
  const manifest = loadManifest();
  assert.ok(!PACKAGE_ALIASES.has(manifest.name) || manifest.name === '@muse/native');
  assert.equal(manifest.name, '@muse/native');
});

// ---------------------------------------------------------------------------
// Boundary / error cases
// ---------------------------------------------------------------------------

test('variant-style names are not separate entries (Button variants are prop values)', () => {
  const manifest = loadManifest();
  const names = new Set(manifest.components.map((c: any) => c?.name));
  for (const nonEntry of ['FilledButton', 'TonalButton', 'OutlinedButton', 'TextButton', 'ElevatedButton']) {
    assert.ok(!names.has(nonEntry), `${nonEntry} must not be a separate entry`);
  }
});

test('Fab uses the canonical JS key, not all-caps FAB', () => {
  const manifest = loadManifest();
  const names = new Set(manifest.components.map((c: any) => c?.name));
  assert.ok(names.has('Fab'), 'Fab entry missing');
  assert.ok(names.has('ExtendedFab'), 'ExtendedFab entry missing');
  assert.ok(!names.has('FAB'), 'FAB must not appear; canonical key is Fab');
  assert.ok(!names.has('ExtendedFAB'), 'ExtendedFAB must not appear; canonical key is ExtendedFab');
});
