'use strict';
// T8 — universal APK fallback gate (NFR-BUILD-010 / AC-BUILD-026).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Builds tmp fixture packaging configs under os.tmpdir(); no network, no
// secrets, no live services, never a real APK binary (dist/ binaries are
// untracked by design per plan A3).
// Red state: ./universalApkGate.js does not exist yet, so every case below
// fails (MODULE_NOT_FOUND via loadImpl) until T8 lands.
//
// Interface under test (plan verbatim):
//   checkUniversalFallback(bundleConfigPath: string): { ok: boolean,
//   violations: string[] }
// Verifies the packaging config declares a bundletool-produced universal APK
// fallback functionally equivalent to the AAB (same splits declared in
// T5/T6). Hermetic substitute for TC-BUILD-026 (`dist/universal-release.apk`
// file check). Reads toolchain/packaging/bundle-config.json from T5.
//
// ASSUMPTION (fallback declaration shape — the coder owns bundle-config.json
// and this gate; the tests pin the shape so the gate is implementable):
//   config.universalApk: { enabled: true, mode: "bundletool",
//                          equivalentSplits: true }
// A config without a `universalApk` declaration, with enabled:false, with a
// non-bundletool mode, or without equivalentSplits:true fails with a named
// violation. A missing config file is a violation, never a throw.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function loadImpl() {
  return require('./universalApkGate.js');
}

const PACKAGING_DIR = __dirname;
const T5_CONFIG = path.join(PACKAGING_DIR, 'bundle-config.json');

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

function writeConfig(config) {
  const dir = mkTmp('universal-apk-');
  const file = path.join(dir, 'bundle-config.json');
  fs.writeFileSync(file, JSON.stringify(config, null, 2));
  return file;
}

// Minimal T5-shaped base every fixture builds on.
function baseConfig(overrides = {}) {
  return {
    artifact: 'aab',
    languageSplits: { enabled: true, locales: ['en-US'] },
    densitySplits: true,
    abiSplits: ['arm64-v8a'],
    universalApk: {
      enabled: true,
      mode: 'bundletool',
      equivalentSplits: true,
    },
    ...overrides,
  };
}

describe('T8 — checkUniversalFallback contract shape (AC-BUILD-026)', () => {
  it('returns { ok: boolean, violations: string[] } for a declared fallback', () => {
    const { checkUniversalFallback } = loadImpl();
    const result = checkUniversalFallback(writeConfig(baseConfig()));
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T8 — declared bundletool fallback passes (AC-BUILD-026)', () => {
  it('a config with a bundletool universal APK fallback passes', () => {
    const { checkUniversalFallback } = loadImpl();
    const result = checkUniversalFallback(writeConfig(baseConfig()));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });

  it('the checked-in T5 bundle-config.json declares the fallback', () => {
    // T8 reads T5's config: the fallback declaration lives with the splits.
    const { checkUniversalFallback } = loadImpl();
    const result = checkUniversalFallback(T5_CONFIG);
    assert.equal(
      result.ok,
      true,
      `T5 bundle-config.json must declare the universal APK fallback, got: ${JSON.stringify(result.violations)}`
    );
    assert.deepEqual(result.violations, []);
  });
});

describe('T8 — missing fallback declarations fail (TC-BUILD-026)', () => {
  it('a config without a fallback declaration fails with a named violation', () => {
    const { checkUniversalFallback } = loadImpl();
    const { universalApk, ...withoutFallback } = baseConfig();
    void universalApk;
    const result = checkUniversalFallback(writeConfig(withoutFallback));
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
    const named = result.violations.some((v) =>
      /universal/i.test(v)
    );
    assert.ok(
      named,
      `a violation must name the missing universal APK declaration, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a config with the fallback disabled fails', () => {
    const { checkUniversalFallback } = loadImpl();
    const result = checkUniversalFallback(
      writeConfig(
        baseConfig({ universalApk: { enabled: false, mode: 'bundletool', equivalentSplits: true } })
      )
    );
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
  });

  it('a config whose fallback is not bundletool-produced fails', () => {
    const { checkUniversalFallback } = loadImpl();
    const result = checkUniversalFallback(
      writeConfig(
        baseConfig({ universalApk: { enabled: true, mode: 'hand-built', equivalentSplits: true } })
      )
    );
    assert.equal(result.ok, false);
    const named = result.violations.some((v) => /bundletool/i.test(v));
    assert.ok(
      named,
      `a violation must name the bundletool requirement, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a config whose fallback is not split-equivalent to the AAB fails', () => {
    const { checkUniversalFallback } = loadImpl();
    const result = checkUniversalFallback(
      writeConfig(
        baseConfig({ universalApk: { enabled: true, mode: 'bundletool', equivalentSplits: false } })
      )
    );
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
  });

  it('a missing config file is a violation, never a throw', () => {
    // ASSUMPTION: a gate that cannot read the packaging config fails closed.
    const { checkUniversalFallback } = loadImpl();
    const missing = path.join(os.tmpdir(), `bundle-config-absent-${Date.now()}.json`);
    const result = checkUniversalFallback(missing);
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
  });
});
