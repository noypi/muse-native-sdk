/**
 * TC-M3-007 (gate half) + TC-M3-008 — manifest completeness gate and
 * schema-single-source contract (FR-M3-004, AC-M3-007, AC-M3-008).
 *
 * Hermetic: node:test + node:assert/strict only. Reads the manifest and
 * package.json from disk, drives packages/native/scripts/check-manifest.js
 * as a child process. No network, no secrets, no services.
 *
 * ASSUMPTION G1 (testability overrides): check-manifest.js resolves its
 * inputs from MANIFEST_PATH / COMPONENTS_PATH env vars when set, defaulting
 * to packages/native/muse.manifest.json + packages/native/index.js. This
 * lets the missing/orphan cases run against tmp fixtures without touching
 * production files.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync, mkdtempSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const NATIVE_DIR = join(HERE, '..');
const CHECK_SCRIPT = join(NATIVE_DIR, 'scripts', 'check-manifest.js');
const MANIFEST_PATH = join(NATIVE_DIR, 'muse.manifest.json');
const PACKAGE_JSON = join(NATIVE_DIR, 'package.json');
const INDEX_JS = join(NATIVE_DIR, 'index.js');

// ---------------------------------------------------------------------------
// T2 gate wiring: script exists + npm script wiring
// ---------------------------------------------------------------------------

test('check-manifest.js gate script exists', () => {
  assert.ok(existsSync(CHECK_SCRIPT), `missing gate script: ${CHECK_SCRIPT}`);
});

test('package.json wires the gate as a check/prebuild script', () => {
  const pkg = JSON.parse(readFileSync(PACKAGE_JSON, 'utf8'));
  const scripts = pkg.scripts ?? {};
  const wired = Object.entries(scripts).some(
    ([name, cmd]) =>
      typeof cmd === 'string' &&
      cmd.includes('check-manifest.js') &&
      /^(check|prebuild|prepublish)/.test(name),
  );
  assert.ok(wired, 'package.json must wire check-manifest.js as a check/prebuild script');
});

function runCheck(env = {}) {
  return spawnSync(process.execPath, [CHECK_SCRIPT], {
    encoding: 'utf8',
    env: { ...process.env, ...env },
  });
}

// ---------------------------------------------------------------------------
// T2 acceptance: exit 0 on exact match
// ---------------------------------------------------------------------------

test('check-manifest.js exits 0 on exact manifest/export match', () => {
  const res = runCheck();
  assert.equal(res.error, undefined, `gate failed to spawn: ${res.error}`);
  assert.equal(res.status, 0, `gate must exit 0 on exact match; stderr: ${res.stderr}`);
});

// ---------------------------------------------------------------------------
// T2 acceptance: missing component export fails naming the entry
// ---------------------------------------------------------------------------

function fixtureDirWith({ removeExport, addOrphan }) {
  const dir = mkdtempSync(join(tmpdir(), 'muse-manifest-gate-'));
  const manifest = JSON.parse(readFileSync(MANIFEST_PATH, 'utf8'));
  if (addOrphan) {
    manifest.components = [
      ...manifest.components,
      { name: addOrphan, tier: 1, native: 'MuseOrphan', props: {} },
    ];
  }
  const fixtureManifest = join(dir, 'manifest.json');
  writeFileSync(fixtureManifest, JSON.stringify(manifest));
  let indexSrc = readFileSync(INDEX_JS, 'utf8');
  if (removeExport) {
    // Fixture components module exposing every name except the removed one.
    const names = manifest.components.map((c) => c.name).filter((n) => n !== removeExport);
    indexSrc = `'use strict';\nconst components = {};\nfor (const n of ${JSON.stringify(names)}) components[n] = (p) => ({ type: n, props: p ?? {} });\nmodule.exports = { components };\n`;
  }
  const fixtureComponents = join(dir, 'components.js');
  writeFileSync(fixtureComponents, indexSrc);
  return { dir, fixtureManifest, fixtureComponents };
}

test('deleting a component export exits non-zero naming the missing component', () => {
  const { fixtureManifest, fixtureComponents } = fixtureDirWith({ removeExport: 'Surface' });
  const res = runCheck({ MANIFEST_PATH: fixtureManifest, COMPONENTS_PATH: fixtureComponents });
  assert.notEqual(res.status, 0, 'gate must fail when a component export is deleted');
  const out = `${res.stdout ?? ''}${res.stderr ?? ''}`;
  assert.ok(out.includes('Surface'), `gate output must name the missing component; got: ${out}`);
});

// ---------------------------------------------------------------------------
// T2 acceptance: orphan manifest entry fails naming the entry
// ---------------------------------------------------------------------------

test('adding an unimplemented manifest entry exits non-zero naming the orphan', () => {
  const { fixtureManifest, fixtureComponents } = fixtureDirWith({ addOrphan: 'GhostWidget' });
  const res = runCheck({ MANIFEST_PATH: fixtureManifest, COMPONENTS_PATH: fixtureComponents });
  assert.notEqual(res.status, 0, 'gate must fail on an orphan manifest entry');
  const out = `${res.stdout ?? ''}${res.stderr ?? ''}`;
  assert.ok(out.includes('GhostWidget'), `gate output must name the orphan entry; got: ${out}`);
});

// ---------------------------------------------------------------------------
// TC-M3-008: manifest is the single source for prop schemas
// ---------------------------------------------------------------------------

const VALID_PROP_TYPES = new Set(['string', 'boolean', 'number', 'token', 'slot', 'object', 'function']);

test('every manifest prop schema uses a valid single-source type', () => {
  const manifest = JSON.parse(readFileSync(MANIFEST_PATH, 'utf8'));
  for (const entry of manifest.components) {
    for (const [prop, schema] of Object.entries(entry.props ?? {})) {
      assert.ok(
        schema && VALID_PROP_TYPES.has(schema.type),
        `${entry.name}.${prop} must use a single-source schema type`,
      );
    }
  }
});

test('manifest values lists are non-empty string arrays (types cannot drift)', () => {
  const manifest = JSON.parse(readFileSync(MANIFEST_PATH, 'utf8'));
  let sawValues = false;
  for (const entry of manifest.components) {
    for (const [prop, schema] of Object.entries(entry.props ?? {})) {
      if (schema.values !== undefined) {
        sawValues = true;
        assert.ok(Array.isArray(schema.values) && schema.values.length > 0,
          `${entry.name}.${prop} values must be a non-empty array`);
        for (const v of schema.values) assert.equal(typeof v, 'string');
      }
    }
  }
  assert.ok(sawValues, 'manifest must declare at least one constrained values list');
});

test('known variant unions stay in the manifest single source', () => {
  const manifest = JSON.parse(readFileSync(MANIFEST_PATH, 'utf8'));
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  assert.deepEqual(byName.get('Button').props.variant.values,
    ['filled', 'tonal', 'outlined', 'text', 'elevated']);
  assert.deepEqual(byName.get('Card').props.variant.values, ['elevated', 'filled', 'outlined']);
  assert.deepEqual(byName.get('Chip').props.variant.values, ['assist', 'filter', 'input', 'suggestion']);
});

// ---------------------------------------------------------------------------
// T6 / TC-M3-004 + TC-M3-008 (half) — tier-2 typed prop schemas; unsupported
// variant/prop fails type check and build (FR-M3-002, AC-M3-004; also
// FR-M3-004, AC-M3-008 schema single-source).
//
// ASSUMPTION P3 (gate testability): `scripts/check-props.js` resolves its
// inputs from MANIFEST_PATH / DTS_PATH env vars when set, defaulting to
// packages/native/muse.manifest.json + packages/native/index.d.ts (mirrors
// ASSUMPTION G1 of check-manifest.js). It exits 0 when every manifest
// `values` list equals the matching index.d.ts union and every manifest prop
// key is declared in index.d.ts; otherwise it exits 1 naming
// `component.prop`.
// ASSUMPTION P4 (tsc execution is CI-owned): the `typescript` compiler is not
// vendored in this checkout, so no test shells out to `tsc`. These tests pin
// the type-surface contract that makes `tsc --noEmit` rejection hold — named
// literal unions (never `string`) generated from the manifest `values` lists
// — plus the package.json wiring that runs `tsc --noEmit` in the build gate
// INDIRECTLY: `check:types` invokes `scripts/check-types.js` (commit 6f2328f
// moved the literal `tsc --noEmit` out of package.json for the Windows
// `tsc.cmd` spawn fix + the missing-tsc SKIP guard), and that wrapper runs
// the local compiler with `tsc --noEmit -p tsconfig.json`. A
// `variant="giant"` assignment is unrepresentable in the pinned unions, so
// the wired compiler must reject it once it runs.
// ---------------------------------------------------------------------------

const CHECK_PROPS_SCRIPT = join(NATIVE_DIR, 'scripts', 'check-props.js');
const INDEX_DTS = join(NATIVE_DIR, 'index.d.ts');

test('check-props.js gate script exists', () => {
  assert.ok(existsSync(CHECK_PROPS_SCRIPT), `missing gate script: ${CHECK_PROPS_SCRIPT}`);
});

test('package.json wires check-props.js as a check/prebuild script', () => {
  const pkg = JSON.parse(readFileSync(PACKAGE_JSON, 'utf8'));
  const scripts = pkg.scripts ?? {};
  const wired = Object.entries(scripts).some(
    ([name, cmd]) =>
      typeof cmd === 'string' &&
      cmd.includes('check-props.js') &&
      /^(check|prebuild|prepublish)/.test(name),
  );
  assert.ok(wired, 'package.json must wire check-props.js as a check/prebuild script');
});

test('build gate runs tsc --noEmit via check-types.js alongside check-props.js', () => {
  const pkg = JSON.parse(readFileSync(PACKAGE_JSON, 'utf8'));
  const scripts = pkg.scripts ?? {};
  // (1) check:types must invoke the check-types.js wrapper — the literal
  // `tsc --noEmit` in package.json was deliberately moved here (6f2328f:
  // Windows tsc.cmd spawn fix + missing-tsc SKIP guard). Inlining the
  // literal would regress that fix.
  const checkTypes = scripts['check:types'];
  assert.ok(
    typeof checkTypes === 'string' && checkTypes.includes('check-types.js'),
    'check:types must invoke scripts/check-types.js (local-compiler wrapper with SKIP guard)',
  );
  // (2) the wrapper must run the local compiler with tsc --noEmit.
  const wrapper = readFileSync(join(NATIVE_DIR, 'scripts', 'check-types.js'), 'utf8');
  assert.ok(
    wrapper.includes('--noEmit') && wrapper.includes('tsc'),
    'check-types.js must run tsc --noEmit via the local compiler',
  );
  // (3) teeth: the test gate must chain check:types so type-checking cannot
  // be silently dropped from the gate.
  assert.ok(
    typeof scripts.test === 'string' && scripts.test.includes('check:types'),
    'test gate must chain check:types (dropping type-checking must fail this test)',
  );
});

function readDts() {
  return readFileSync(INDEX_DTS, 'utf8');
}

test('index.d.ts declares the ButtonVariant union (generated from the manifest)', () => {
  const dts = readDts();
  assert.ok(
    /type\s+ButtonVariant\s*=/.test(dts),
    'index.d.ts must declare a named ButtonVariant union type',
  );
  for (const member of ['filled', 'tonal', 'outlined', 'text', 'elevated']) {
    assert.ok(
      dts.includes(`'${member}'`) || dts.includes(`"${member}"`),
      `ButtonVariant must include "${member}"`,
    );
  }
});

test('index.d.ts declares the CardVariant and ChipVariant unions', () => {
  const dts = readDts();
  assert.ok(/type\s+CardVariant\s*=/.test(dts), 'index.d.ts must declare CardVariant');
  assert.ok(/type\s+ChipVariant\s*=/.test(dts), 'index.d.ts must declare ChipVariant');
  for (const member of ['elevated', 'filled', 'outlined']) {
    assert.ok(dts.includes(`'${member}'`), `CardVariant must include "${member}"`);
  }
  for (const member of ['assist', 'filter', 'input', 'suggestion']) {
    assert.ok(dts.includes(`'${member}'`), `ChipVariant must include "${member}"`);
  }
});

test('index.d.ts variant unions never widen to string (variant="giant" is unrepresentable)', () => {
  const dts = readDts();
  const buttonProps = dts.slice(dts.indexOf('ButtonProps'));
  assert.ok(
    !/variant\?:\s*string\s*[;}]/.test(buttonProps),
    'ButtonProps.variant must be a literal union, never plain string (or tsc would accept "giant")',
  );
  assert.ok(
    !dts.includes('"giant"') && !dts.includes("'giant'"),
    'index.d.ts must not admit a "giant" variant in any union',
  );
});

test('index.d.ts variant unions equal the manifest values lists (no drift)', () => {
  const manifest = JSON.parse(readFileSync(MANIFEST_PATH, 'utf8'));
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  const dts = readDts();
  for (const [typeName, component] of [
    ['ButtonVariant', 'Button'],
    ['CardVariant', 'Card'],
    ['ChipVariant', 'Chip'],
  ]) {
    const values = byName.get(component).props.variant.values;
    const decl = dts.slice(dts.indexOf(`type ${typeName}`));
    const union = decl.slice(0, decl.indexOf(';'));
    for (const v of values) {
      assert.ok(
        union.includes(`'${v}'`) || union.includes(`"${v}"`),
        `${typeName} must include manifest value "${v}" from ${component}.variant`,
      );
    }
  }
});

test('shared enabled/selected/error/icon slots stay in manifest schemas and index.d.ts', () => {
  const manifest = JSON.parse(readFileSync(MANIFEST_PATH, 'utf8'));
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  const dts = readDts();
  const expectations = [
    ['Button', 'enabled'],
    ['Button', 'icon'],
    ['Card', 'enabled'],
    ['Chip', 'enabled'],
    ['Chip', 'selected'],
    ['Checkbox', 'enabled'],
    ['Checkbox', 'error'],
    ['Radio', 'selected'],
    ['Radio', 'error'],
    ['Switch', 'enabled'],
    ['Switch', 'icon'],
  ];
  for (const [component, prop] of expectations) {
    assert.ok(
      byName.get(component).props[prop] !== undefined,
      `manifest schema ${component}.${prop} must exist (shared slot)`,
    );
    const propsType = dts.slice(dts.indexOf(`${component}Props`));
    const body = propsType.slice(0, propsType.indexOf('};'));
    assert.ok(
      new RegExp(`\\b${prop}\\??:`).test(body),
      `index.d.ts ${component}Props must declare the shared ${prop} slot`,
    );
  }
});

function runPropsCheck(env = {}) {
  return spawnSync(process.execPath, [CHECK_PROPS_SCRIPT], {
    encoding: 'utf8',
    env: { ...process.env, ...env },
  });
}

test('check-props.js exits 0 when types match the manifest single source', () => {
  const res = runPropsCheck();
  assert.equal(res.error, undefined, `gate failed to spawn: ${res.error}`);
  assert.equal(res.status, 0, `gate must exit 0 when types match the manifest; stderr: ${res.stderr}`);
});

function fixtureManifestDir(mutatedManifest) {
  const dir = mkdtempSync(join(tmpdir(), 'muse-props-gate-'));
  const fixtureManifest = join(dir, 'manifest.json');
  writeFileSync(fixtureManifest, JSON.stringify(mutatedManifest));
  return { dir, fixtureManifest };
}

test('an unsupported variant value in the manifest fails the gate naming component.prop', () => {
  const manifest = JSON.parse(readFileSync(MANIFEST_PATH, 'utf8'));
  const drifted = {
    ...manifest,
    components: manifest.components.map((c) =>
      c.name === 'Button'
        ? { ...c, props: { ...c.props, variant: { ...c.props.variant, values: [...c.props.variant.values, 'giant'] } } }
        : c,
    ),
  };
  const { fixtureManifest } = fixtureManifestDir(drifted);
  const res = runPropsCheck({ MANIFEST_PATH: fixtureManifest, DTS_PATH: INDEX_DTS });
  assert.notEqual(res.status, 0, 'gate must fail when a variant value drifts from the typed union');
  assert.equal(res.status, 1, 'gate must exit 1 on variant drift');
  const out = `${res.stdout ?? ''}${res.stderr ?? ''}`;
  assert.ok(
    out.includes('Button.variant'),
    `gate output must name the drifting component.prop; got: ${out}`,
  );
});

test('removing a prop from a schema fails the gate naming component.prop (TC-M3-008 half)', () => {
  const manifest = JSON.parse(readFileSync(MANIFEST_PATH, 'utf8'));
  const pruned = {
    ...manifest,
    components: manifest.components.map((c) => {
      if (c.name !== 'Button') return c;
      const { icon, ...rest } = c.props;
      return { ...c, props: rest };
    }),
  };
  const { fixtureManifest } = fixtureManifestDir(pruned);
  const res = runPropsCheck({ MANIFEST_PATH: fixtureManifest, DTS_PATH: INDEX_DTS });
  assert.notEqual(res.status, 0, 'gate must fail when a schema prop is removed but usages/types remain');
  assert.equal(res.status, 1, 'gate must exit 1 on prop removal');
  const out = `${res.stdout ?? ''}${res.stderr ?? ''}`;
  assert.ok(
    out.includes('Button.icon'),
    `gate output must name the removed component.prop; got: ${out}`,
  );
});

