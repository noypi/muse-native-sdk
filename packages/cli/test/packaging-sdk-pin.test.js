'use strict';
// Batch 2 / T4 (FR-PACKAGING-009 required slice, AC-PACKAGING-026).
// Scope: examples/m3-gallery/muse-native.yaml carries an exact `sdk: <VER>`
// pin equal to build/VERSION line 1 (== sdk.yaml version); exact match, no
// ranges. T3 reads this pin as its expected SDK version.
// Interface (plan T4 verbatim):
//   `muse-native.yaml` data contract — top-level key `sdk: <VER>`
//   exact-match string equal to `build/VERSION` line 1;
//   `none — data file, read by T3 check`
// Hermetic: node:test + node:assert/strict + stdlib only. Assertions read
// repo text files only; zero tool invocations, no network, no secrets,
// no .env, no live service. RED until the coder adds the `sdk:` pin
// (missing key fails setup); negative controls run on in-memory fixtures
// so malformed pins always fail even when the repo files are correct.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

function repoRoot() {
  let dir = __dirname;
  for (let i = 0; i < 8; i += 1) {
    if (fs.existsSync(path.join(dir, 'scripts', 'build-sample.sh'))) return dir;
    dir = path.dirname(dir);
  }
  throw new Error('test setup: cannot locate repo root from ' + __dirname);
}

const ROOT = repoRoot();
const SDK_YAML = path.join(ROOT, 'sdk.yaml');
const GALLERY_YAML = path.join(ROOT, 'examples', 'm3-gallery', 'muse-native.yaml');

function read(p) {
  assert.ok(fs.existsSync(p), `expected file to exist: ${path.relative(ROOT, p)}`);
  return fs.readFileSync(p, 'utf8');
}

// Strict top-level `key: MAJOR.MINOR.PATCH` line; leading whitespace allowed,
// ranges/caret/tilde/quoted values do not match.
function pinLine(text, key) {
  const m = new RegExp(`^\\s*${key}:\\s*([0-9]+\\.[0-9]+\\.[0-9]+)\\s*$`, 'm').exec(text);
  return m ? m[1] : null;
}

describe('T4 gallery sdk: pin (AC-PACKAGING-026)', () => {
  it('muse-native.yaml carries a top-level exact sdk: pin', () => {
    const text = read(GALLERY_YAML);
    const pin = pinLine(text, 'sdk');
    assert.ok(pin, 'muse-native.yaml must contain a top-level `sdk: MAJOR.MINOR.PATCH` line');
  });

  it('sdk.yaml version source is strict SemVer', () => {
    const text = read(SDK_YAML);
    const ver = pinLine(text, 'version');
    assert.ok(ver, 'sdk.yaml must contain `version: MAJOR.MINOR.PATCH`');
  });

  it('sdk: pin exactly equals the sdk.yaml version (== build/VERSION line 1)', () => {
    const pin = pinLine(read(GALLERY_YAML), 'sdk');
    const ver = pinLine(read(SDK_YAML), 'version');
    assert.ok(pin && ver, 'both pins must parse before comparison');
    assert.equal(pin, ver, `sdk: pin (${pin}) must exactly equal sdk.yaml version (${ver})`);
  });

  it('sdk: pin has no range operator (exact match, no ^ ~ >=)', () => {
    const text = read(GALLERY_YAML);
    const line = text.split('\n').find((l) => /^\s*sdk:/.test(l));
    assert.ok(line, 'sdk: line must exist');
    assert.ok(!/[\^~*<>|]/.test(line), `sdk: pin must be exact, found range syntax: ${line.trim()}`);
  });

  it('negative control: malformed pins do not parse', () => {
    for (const bad of [
      'sdk: latest\n',
      'sdk: 0.1\n',
      'sdk: ^0.1.0\n',
      'sdk: ~0.1.0\n',
      'sdk: "0.1.0"\n',
      '# sdk: 0.1.0\n',
      'name: gallery\n',
    ]) {
      assert.equal(pinLine(bad, 'sdk'), null, `must reject pin ${JSON.stringify(bad)}`);
    }
  });

  it('negative control: mismatch is detected (unequal pins fail)', () => {
    assert.notEqual(
      pinLine('sdk: 0.1.0\n', 'sdk'),
      pinLine('version: 0.2.0\n', 'version'),
      'a stale pin must compare unequal to the current version',
    );
  });
});
