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

const VALID_PROP_TYPES = new Set(['string', 'boolean', 'number', 'token', 'slot']);

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

