'use strict';
// T5 — bundle config with language splits over shipped locales
// (NFR-BUILD-010 / AC-BUILD-026, AC-BUILD-027).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Reads the checked-in declarative config ./bundle-config.json plus tmpdir
// fixture copies; no network, no secrets, no live services.
// Red state: ./bundle-config.json does not exist yet, so every case below
// fails (ENOENT via loadConfig) until T5's config lands.
//
// Interface under test (plan verbatim):
//   none — declarative config; test asserts schema: { artifact: "aab",
//   languageSplits: { enabled: true, locales: string[] },
//   densitySplits: boolean, abiSplits: string[] }
// Config declares AAB primary + Play language splits enabled; `locales`
// mirrors the shipped set from specs-localization.md (read-only reference,
// not copied as spec). Hermetic equivalent of TC-BUILD-027's grep check
// (`language-splits in toolchain/packaging/bundle-config.json`).
//
// ASSUMPTION (validator lives in-test): the config is declarative — there is
// no production gate to call — so this file defines assertBundleConfig(obj,
// source) and every case loads the checked-in file (or a tmpdir copy of it)
// first, keeping red uniform. The coder owns the JSON file, not a function.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const PACKAGING_DIR = __dirname;
const CONFIG_FILE = path.join(PACKAGING_DIR, 'bundle-config.json');

function loadConfig(file = CONFIG_FILE) {
  const raw = fs.readFileSync(file, 'utf8');
  return { config: JSON.parse(raw), source: file };
}

// In-test schema assertion: throws (never returns ok:false) on any shape
// violation so a bad config fails the suite loudly.
function assertBundleConfig(config, source) {
  const where = source || '<config>';
  assert.equal(
    config.artifact,
    'aab',
    `${where}: artifact must be "aab" (AC-BUILD-026 AAB primary), got ${JSON.stringify(config.artifact)}`
  );
  assert.ok(
    config.languageSplits && typeof config.languageSplits === 'object',
    `${where}: languageSplits must be an object`
  );
  assert.equal(
    config.languageSplits.enabled,
    true,
    `${where}: languageSplits.enabled must be true (Play language splits enabled, AC-BUILD-027)`
  );
  assert.ok(
    Array.isArray(config.languageSplits.locales),
    `${where}: languageSplits.locales must be an array`
  );
  assert.ok(
    config.languageSplits.locales.length > 0,
    `${where}: languageSplits.locales must be non-empty (TC-BUILD-027 language-splits presence)`
  );
  for (const locale of config.languageSplits.locales) {
    assert.equal(
      typeof locale,
      'string',
      `${where}: every locale must be a string, got ${JSON.stringify(locale)}`
    );
    assert.ok(
      locale.length > 0,
      `${where}: locales must not contain empty strings`
    );
  }
  assert.equal(
    typeof config.densitySplits,
    'boolean',
    `${where}: densitySplits must be a boolean, got ${JSON.stringify(config.densitySplits)}`
  );
  assert.ok(
    Array.isArray(config.abiSplits),
    `${where}: abiSplits must be an array`
  );
  for (const abi of config.abiSplits) {
    assert.equal(
      typeof abi,
      'string',
      `${where}: every abiSplit must be a string, got ${JSON.stringify(abi)}`
    );
  }
}

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
});

function writeConfigCopy(mutator) {
  const { config } = loadConfig();
  const copy = JSON.parse(JSON.stringify(config));
  mutator(copy);
  const dir = mkTmp('bundle-config-');
  const file = path.join(dir, 'bundle-config.json');
  fs.writeFileSync(file, JSON.stringify(copy, null, 2));
  return file;
}

describe('T5 — bundle config schema (AC-BUILD-026, AC-BUILD-027)', () => {
  it('declares the AAB primary artifact', () => {
    const { config, source } = loadConfig();
    assertBundleConfig(config, source);
    assert.equal(config.artifact, 'aab');
  });

  it('enables Play language splits with a non-empty shipped locale set', () => {
    const { config, source } = loadConfig();
    assertBundleConfig(config, source);
    assert.equal(config.languageSplits.enabled, true);
    assert.ok(config.languageSplits.locales.length > 0);
  });

  it('covers the default locale (en) from the shipped set', () => {
    // ASSUMPTION: specs-localization ships US English as the default locale;
    // the full shipped set is owned by that spec (read-only ref) and the
    // coder mirrors it. The test pins only the default entry.
    const { config, source } = loadConfig();
    assertBundleConfig(config, source);
    const hasDefault = config.languageSplits.locales.some((l) =>
      /^en([-_]|$)/i.test(l)
    );
    assert.ok(
      hasDefault,
      `${source}: locales must include the default English locale, got ${JSON.stringify(config.languageSplits.locales)}`
    );
  });

  it('declares density and ABI split shapes', () => {
    const { config, source } = loadConfig();
    assertBundleConfig(config, source);
    assert.equal(typeof config.densitySplits, 'boolean');
    assert.ok(Array.isArray(config.abiSplits));
  });

  it('a config with language splits disabled fails validation', () => {
    const file = writeConfigCopy((c) => {
      c.languageSplits.enabled = false;
    });
    const raw = fs.readFileSync(file, 'utf8');
    assert.throws(
      () => assertBundleConfig(JSON.parse(raw), file),
      /languageSplits\.enabled must be true/
    );
  });

  it('a config with an empty locale list fails validation (TC-BUILD-027 missing)', () => {
    const file = writeConfigCopy((c) => {
      c.languageSplits.locales = [];
    });
    const raw = fs.readFileSync(file, 'utf8');
    assert.throws(
      () => assertBundleConfig(JSON.parse(raw), file),
      /locales must be non-empty/
    );
  });

  it('a config declaring a non-AAB artifact fails validation (AC-BUILD-026)', () => {
    const file = writeConfigCopy((c) => {
      c.artifact = 'apk';
    });
    const raw = fs.readFileSync(file, 'utf8');
    assert.throws(
      () => assertBundleConfig(JSON.parse(raw), file),
      /artifact must be "aab"/
    );
  });
});
