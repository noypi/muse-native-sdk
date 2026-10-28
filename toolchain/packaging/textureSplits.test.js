'use strict';
// T6 — texture splits per device capability (NFR-BUILD-010 / AC-BUILD-028).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Reads the checked-in declarative config ./texture-splits.json plus tmpdir
// fixture copies; no network, no secrets, no live services.
// Red state: ./texture-splits.json does not exist yet, so every case below
// fails (ENOENT via loadConfig) until T6's config lands.
//
// Interface under test (plan verbatim):
//   none — declarative config; test asserts schema: { tiers: [{ format:
//   "astc" | "etc2", fallback: boolean }] } with astc primary and etc2
//   fallback tier
// Hermetic equivalent of TC-BUILD-028's `astc` grep. GFX-TEX
// (specs-graphics-rendering.md) is read-only context for the format names:
// ASTC primary, ETC2/EAC guaranteed baseline.
//
// ASSUMPTION (validator lives in-test): the config is declarative — there is
// no production gate to call — so this file defines assertTextureSplits(obj,
// source) and every case loads the checked-in file (or a tmpdir copy of it)
// first, keeping red uniform. The coder owns the JSON file, not a function.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const PACKAGING_DIR = __dirname;
const CONFIG_FILE = path.join(PACKAGING_DIR, 'texture-splits.json');

const KNOWN_FORMATS = new Set(['astc', 'etc2']);

function loadConfig(file = CONFIG_FILE) {
  const raw = fs.readFileSync(file, 'utf8');
  return { config: JSON.parse(raw), source: file };
}

// In-test schema assertion: throws on any shape violation.
function assertTextureSplits(config, source) {
  const where = source || '<config>';
  assert.ok(
    Array.isArray(config.tiers),
    `${where}: tiers must be an array`
  );
  assert.ok(
    config.tiers.length > 0,
    `${where}: tiers must be non-empty — shipping every variant to every device (no tiers) is rejected (AC-BUILD-028)`
  );
  for (const tier of config.tiers) {
    assert.ok(
      tier && typeof tier === 'object',
      `${where}: every tier must be an object, got ${JSON.stringify(tier)}`
    );
    assert.ok(
      KNOWN_FORMATS.has(tier.format),
      `${where}: tier format must be one of ${[...KNOWN_FORMATS].join(', ')}, got ${JSON.stringify(tier.format)}`
    );
    assert.equal(
      typeof tier.fallback,
      'boolean',
      `${where}: tier fallback must be a boolean, got ${JSON.stringify(tier.fallback)}`
    );
  }
  const formats = config.tiers.map((t) => t.format);
  assert.ok(
    formats.includes('astc'),
    `${where}: an astc primary tier is required (TC-BUILD-028 astc grep), got ${JSON.stringify(formats)}`
  );
  const fallbacks = config.tiers.filter((t) => t.fallback === true);
  assert.ok(
    fallbacks.length > 0,
    `${where}: at least one tier must be marked fallback`
  );
  assert.ok(
    fallbacks.some((t) => t.format === 'etc2'),
    `${where}: the etc2 guaranteed-baseline tier must be a fallback, got ${JSON.stringify(config.tiers)}`
  );
  const primaries = config.tiers.filter((t) => t.fallback === false);
  assert.ok(
    primaries.some((t) => t.format === 'astc'),
    `${where}: the astc tier must be primary (fallback:false), got ${JSON.stringify(config.tiers)}`
  );
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
  const dir = mkTmp('texture-splits-');
  const file = path.join(dir, 'texture-splits.json');
  fs.writeFileSync(file, JSON.stringify(copy, null, 2));
  return file;
}

describe('T6 — texture splits schema (AC-BUILD-028)', () => {
  it('declares a non-empty tier list (per-device splits, not every variant everywhere)', () => {
    const { config, source } = loadConfig();
    assertTextureSplits(config, source);
    assert.ok(config.tiers.length > 0);
  });

  it('ships an astc primary tier (TC-BUILD-028 astc presence)', () => {
    const { config, source } = loadConfig();
    assertTextureSplits(config, source);
    assert.ok(
      config.tiers.some((t) => t.format === 'astc' && t.fallback === false),
      `${source}: expected an astc primary tier`
    );
  });

  it('ships an etc2 fallback tier (GFX-TEX guaranteed baseline)', () => {
    const { config, source } = loadConfig();
    assertTextureSplits(config, source);
    assert.ok(
      config.tiers.some((t) => t.format === 'etc2' && t.fallback === true),
      `${source}: expected an etc2 fallback tier`
    );
  });

  it('a config with no tiers fails validation (every variant to every device)', () => {
    const file = writeConfigCopy((c) => {
      c.tiers = [];
    });
    const raw = fs.readFileSync(file, 'utf8');
    assert.throws(
      () => assertTextureSplits(JSON.parse(raw), file),
      /tiers must be non-empty/
    );
  });

  it('a config with no astc tier fails validation', () => {
    const file = writeConfigCopy((c) => {
      c.tiers = c.tiers.filter((t) => t.format !== 'astc');
    });
    const raw = fs.readFileSync(file, 'utf8');
    assert.throws(
      () => assertTextureSplits(JSON.parse(raw), file),
      /astc primary tier is required/
    );
  });

  it('a config with no etc2 fallback fails validation', () => {
    const file = writeConfigCopy((c) => {
      for (const t of c.tiers) {
        if (t.format === 'etc2') t.fallback = false;
      }
    });
    const raw = fs.readFileSync(file, 'utf8');
    assert.throws(
      () => assertTextureSplits(JSON.parse(raw), file),
      /fallback/
    );
  });

  it('a config with an unknown texture format fails validation', () => {
    const file = writeConfigCopy((c) => {
      c.tiers.push({ format: 'pvrtc', fallback: false });
    });
    const raw = fs.readFileSync(file, 'utf8');
    assert.throws(
      () => assertTextureSplits(JSON.parse(raw), file),
      /must be one of astc, etc2/
    );
  });
});
