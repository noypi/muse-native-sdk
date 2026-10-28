/**
 * TC-M3-007 / AC-M3-007 — muse.manifest.json registry contract (FR-M3-004).
 *
 * Hermetic: reads only packages/native/muse.manifest.json from disk,
 * uses node:test + node:assert/strict, no network, no secrets, no services.
 *
 * ASSUMPTION A1 (JS+JSON surface): the manifest is consumed as JSON; tests
 * load it with fs + JSON.parse so they are independent of any loader.
 * ASSUMPTION A3 (v1.16 rule): per SPEC-M3-NATIVE v1.16 RULE-M3-005, no
 * `capo` spelling is resolved, shipped, or aliased; `muse` is the single
 * supported alias, enforced at the loader layer elsewhere — not by any
 * table in this file. This file asserts the canonical contract plus the
 * fail-closed negation pole (T3): the manifest carries no `aliases` key,
 * its source contains no `capo` spelling, and the resolution surface
 * rejects `capo` spellings by throwing that names the offending spelling.
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
  'TextInput',
] as const;

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
] as const;

const EXPECTED: ReadonlyMap<string, 1 | 2 | 3> = new Map([
  ...TIER_1.map((n): [string, 1 | 2 | 3] => [n, 1]),
  ...TIER_2.map((n): [string, 1 | 2 | 3] => [n, 2]),
  ...TIER_3.map((n): [string, 1 | 2 | 3] => [n, 3]),
]);

const VALID_PROP_TYPES = new Set(['string', 'boolean', 'number', 'token', 'slot', 'object', 'function']);

function loadManifest(): any {
  const raw = readFileSync(MANIFEST_URL, 'utf8');
  return JSON.parse(raw);
}

// ---------------------------------------------------------------------------
// Fail-closed resolution surface (T3 contract mirror of the loader rule).
// Only the canonical spellings resolve; every other input — in particular
// the `capo` spellings — throws naming the offending spelling.
// ---------------------------------------------------------------------------

function resolvePackageAlias(name: string): '@muse/native' {
  if (name === '@muse/native') return '@muse/native';
  throw new Error(`unknown package '${name}'`);
}

function resolveManifestFile(name: string): 'muse.manifest.json' {
  if (name === 'muse.manifest.json') return 'muse.manifest.json';
  throw new Error(`manifest not found: '${name}'`);
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

test('manifest is valid JSON with canonical package name', () => {
  const manifest = loadManifest();
  assert.equal(manifest.name, '@muse/native');
});

// ---------------------------------------------------------------------------
// AC-M3-007: exactly one entry per component, no extras (41 total:
// 10 tier-1 + 17 tier-2 + 14 tier-3)
// ---------------------------------------------------------------------------

test('manifest contains exactly 41 components', () => {
  const manifest = loadManifest();
  assert.equal(manifest.components.length, 41);
});

test('manifest contains exactly the 41 spec-named components', () => {
  const manifest = loadManifest();
  const names = new Set(manifest.components.map((c: any) => c?.name));
  for (const expected of EXPECTED.keys()) {
    assert.ok(names.has(expected), `missing component: ${expected}`);
  }
  assert.equal(names.size, 41, 'manifest has extra entries beyond the 41 spec-named components');
});

test('manifest has no duplicate component entries', () => {
  const manifest = loadManifest();
  const names = manifest.components.map((c: any) => c?.name);
  assert.equal(new Set(names).size, names.length);
});

// ---------------------------------------------------------------------------
// Per-entry shape: tier + native backing + props
// ---------------------------------------------------------------------------

test('every entry declares tier 1, 2 or 3 matching the tier tables', () => {
  const manifest = loadManifest();
  for (const entry of manifest.components) {
    assert.ok(entry && typeof entry.name === 'string', 'entry has a string name');
    assert.equal(entry.tier, EXPECTED.get(entry.name), `wrong tier for ${entry.name}`);
  }
});

test('every entry names a non-empty native backing or composite base', () => {
  const manifest = loadManifest();
  const tier12Names = new Set([...TIER_1, ...TIER_2]);
  for (const entry of manifest.components) {
    if (entry.tier === 3) {
      assert.equal(typeof entry.base, 'string', `${entry.name} base must be a string`);
      assert.ok(entry.base.length > 0, `${entry.name} base must be non-empty`);
      assert.ok(
        tier12Names.has(entry.base),
        `${entry.name} base must be a tier-1/2 name, got ${entry.base}`,
      );
    } else {
      assert.equal(typeof entry.native, 'string', `${entry.name} native must be a string`);
      assert.ok(entry.native.length > 0, `${entry.name} native must be non-empty`);
    }
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
// RULE-M3-005 canonical name (T2, tightened: direct equality, no alias
// carve-out). T3 fail-closed tests below are the negation pole.
// ---------------------------------------------------------------------------

test('manifest name field uses the canonical spelling, not an alias', () => {
  const manifest = loadManifest();
  assert.equal(manifest.name, '@muse/native');
});

// ---------------------------------------------------------------------------
// RULE-M3-005 fail-closed negation pole (T3): no `capo` in manifest data,
// `capo` spellings rejected naming the spelling.
// ---------------------------------------------------------------------------

test('manifest carries no aliases key', () => {
  const manifest = loadManifest();
  assert.ok(!('aliases' in manifest), 'manifest must not carry an aliases key');
  assert.equal(manifest.aliases, undefined);
});

test('manifest source contains no capo spelling', () => {
  const raw = readFileSync(MANIFEST_URL, 'utf8');
  assert.ok(!raw.toLowerCase().includes('capo'), 'manifest source must not contain a capo spelling');
  assert.ok(!JSON.stringify(loadManifest()).toLowerCase().includes('capo'));
});

test('capo package spelling is rejected naming the spelling', () => {
  assert.equal(resolvePackageAlias('@muse/native'), '@muse/native');
  assert.throws(() => resolvePackageAlias('@capo/native'), /unknown package '@capo\/native'/);
});

test('capo manifest spelling is rejected naming the spelling', () => {
  assert.equal(resolveManifestFile('muse.manifest.json'), 'muse.manifest.json');
  assert.throws(() => resolveManifestFile('capo.manifest.json'), /manifest not found: 'capo\.manifest\.json'/);
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
