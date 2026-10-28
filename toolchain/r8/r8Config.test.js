'use strict';
// T7 — R8 full mode + code/resource shrinking flags (NFR-BUILD-007 / AC-BUILD-011).
// Hermetic: node:test + node:assert/strict (+ node:fs/node:path only to read the
// tester-authored release.properties fixture beside this file). No network, no
// secrets, no live services.
// Red state: ./r8Config.js does not exist yet, so every validator case below
// fails (MODULE_NOT_FOUND via loadImpl) until T7 lands.
//
// Interface under test (plan verbatim):
//   r8ConfigValid(props: Record<string,string>): {ok:boolean, violations:string[]}
// ok:true only when android.enableR8.fullMode=true, code shrinking on,
// shrinkResources=true; violations name each missing/wrong key; module header
// asserts debug builds are never used for size/startup claims.
// ASSUMPTION: the plan names the fullMode and shrinkResources keys literally
// but leaves the code-shrinking key implicit; this contract pins it to
// `android.enableMinify` (value 'true').

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

function loadImpl() {
  return require('./r8Config.js');
}

function goodProps() {
  return {
    'android.enableR8.fullMode': 'true',
    'android.enableMinify': 'true',
    'android.shrinkResources': 'true',
  };
}

describe('T7 — contract shape (AC-BUILD-011)', () => {
  it('returns { ok: boolean, violations: string[] } for correct flags', () => {
    const { r8ConfigValid } = loadImpl();
    const result = r8ConfigValid(goodProps());
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T7 — correct flags pass (AC-BUILD-011)', () => {
  it('fullMode + minify + shrinkResources all true passes', () => {
    const { r8ConfigValid } = loadImpl();
    const result = r8ConfigValid(goodProps());
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });

  it('release.properties fixture contains the literal fullMode line', () => {
    const text = fs.readFileSync(path.join(__dirname, 'release.properties'), 'utf8');
    assert.ok(
      text.includes('android.enableR8.fullMode=true'),
      'fixture must carry the literal android.enableR8.fullMode=true line'
    );
  });

  it('parsed fixture content passes the validator', () => {
    // ASSUMPTION: investors parse release.properties as flat string key/values.
    const { r8ConfigValid } = loadImpl();
    const text = fs.readFileSync(path.join(__dirname, 'release.properties'), 'utf8');
    const props = {};
    for (const line of text.split('\n')) {
      const trimmed = line.trim();
      if (!trimmed || trimmed.startsWith('#')) continue;
      const eq = trimmed.indexOf('=');
      if (eq === -1) continue;
      props[trimmed.slice(0, eq).trim()] = trimmed.slice(eq + 1).trim();
    }
    const result = r8ConfigValid(props);
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T7 — each flag off/missing fails naming the key (AC-BUILD-011)', () => {
  it('fullMode=false fails naming android.enableR8.fullMode', () => {
    const { r8ConfigValid } = loadImpl();
    const result = r8ConfigValid({ ...goodProps(), 'android.enableR8.fullMode': 'false' });
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('android.enableR8.fullMode')));
  });

  it('missing fullMode fails naming android.enableR8.fullMode', () => {
    const { r8ConfigValid } = loadImpl();
    const props = goodProps();
    delete props['android.enableR8.fullMode'];
    const result = r8ConfigValid(props);
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('android.enableR8.fullMode')));
  });

  it('minify off fails naming the code-shrinking key', () => {
    const { r8ConfigValid } = loadImpl();
    const result = r8ConfigValid({ ...goodProps(), 'android.enableMinify': 'false' });
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('android.enableMinify')));
  });

  it('missing minify fails naming the code-shrinking key', () => {
    const { r8ConfigValid } = loadImpl();
    const props = goodProps();
    delete props['android.enableMinify'];
    const result = r8ConfigValid(props);
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('android.enableMinify')));
  });

  it('shrinkResources off fails naming android.shrinkResources', () => {
    const { r8ConfigValid } = loadImpl();
    const result = r8ConfigValid({ ...goodProps(), 'android.shrinkResources': 'false' });
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('android.shrinkResources')));
  });

  it('missing shrinkResources fails naming android.shrinkResources', () => {
    const { r8ConfigValid } = loadImpl();
    const props = goodProps();
    delete props['android.shrinkResources'];
    const result = r8ConfigValid(props);
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('android.shrinkResources')));
  });

  it('empty props fail with all three keys named', () => {
    const { r8ConfigValid } = loadImpl();
    const result = r8ConfigValid({});
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('android.enableR8.fullMode')));
    assert.ok(result.violations.some((v) => v.includes('android.enableMinify')));
    assert.ok(result.violations.some((v) => v.includes('android.shrinkResources')));
  });

  it('non-object input fails closed', () => {
    // ASSUMPTION: malformed input cannot prove a release R8 config.
    const { r8ConfigValid } = loadImpl();
    const result = r8ConfigValid(null);
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0);
  });
});
