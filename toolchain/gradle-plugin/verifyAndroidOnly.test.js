'use strict';
// T1 — Android-only artifact floor (NFR-BUILD-001 / AC-BUILD-001).
// Hermetic: node:test + node:assert + stdlib (fs/os/path) only.
// Builds tmp fixture trees under os.tmpdir(); no network, no secrets,
// no live services, no Gradle wrapper.
// Red state: ../verifyAndroidOnly.js and ../android-only.json do not exist
// yet, so every case below fails (MODULE_NOT_FOUND / ENOENT) until T1 lands.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const PLUGIN_DIR = __dirname;
const POLICY_FILE = path.join(PLUGIN_DIR, 'android-only.json');

function loadImpl() {
  return require('./verifyAndroidOnly.js');
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

function write(rel, content, root) {
  const full = path.join(root, rel);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, content);
  return full;
}

describe('T1 — verifyAndroidOnly contract shape', () => {
  it('returns { ok: boolean, violations: string[] } on a clean tree', () => {
    const { verifyAndroidOnly } = loadImpl();
    const root = mkTmp('android-only-clean-');
    write('gradle.properties', 'android.minSdk=29\n', root);
    const result = verifyAndroidOnly(root);
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });

  it('an empty tree is clean (ok true, no violations)', () => {
    const { verifyAndroidOnly } = loadImpl();
    const root = mkTmp('android-only-empty-');
    const result = verifyAndroidOnly(root);
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T1 — Apple toolchain references are violations', () => {
  it('a fixture containing an xcrun invocation fails naming the file and pattern', () => {
    const { verifyAndroidOnly } = loadImpl();
    const root = mkTmp('android-only-xcrun-');
    const rel = 'build-steps.sh';
    write(rel, '#!/bin/sh\nxcrun --sdk iphoneos clang -c foo.c\n', root);
    const result = verifyAndroidOnly(root);
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
    const named = result.violations.some((v) => v.includes(rel) && /xcrun/i.test(v));
    assert.ok(named, `a violation must name the offending file and pattern, got: ${JSON.stringify(result.violations)}`);
  });

  it('flags an xcode reference in build inputs', () => {
    const { verifyAndroidOnly } = loadImpl();
    const root = mkTmp('android-only-xcode-');
    const rel = path.join('gradle', 'ios.gradle');
    write(rel, '// xcode build step\n', root);
    const result = verifyAndroidOnly(root);
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('ios.gradle') && /xcode/i.test(v)));
  });

  it('flags a pbxproj reference in build inputs', () => {
    const { verifyAndroidOnly } = loadImpl();
    const root = mkTmp('android-only-pbxproj-');
    const rel = 'notes.txt';
    write(rel, 'see App.pbxproj for the iOS target\n', root);
    const result = verifyAndroidOnly(root);
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes(rel) && /pbxproj/i.test(v)));
  });

  it('flags an ios reference in build inputs', () => {
    const { verifyAndroidOnly } = loadImpl();
    const root = mkTmp('android-only-ios-');
    const rel = 'build.yaml';
    write(rel, 'targets:\n  - ios\n', root);
    const result = verifyAndroidOnly(root);
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes(rel) && /ios/i.test(v)));
  });

  it('flags a darwin-only tool invocation in build inputs', () => {
    const { verifyAndroidOnly } = loadImpl();
    const root = mkTmp('android-only-darwin-');
    const rel = 'scripts/build.sh';
    write(rel, '#!/bin/sh\n# darwin-only tool\n', root);
    const result = verifyAndroidOnly(root);
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('build.sh') && /darwin/i.test(v)));
  });
});

describe('T1 — android-only.json policy pins', () => {
  it('policy declares primaryAbi arm64-v8a, emulatorAbi x86_64, minSdk 29', () => {
    const policy = JSON.parse(fs.readFileSync(POLICY_FILE, 'utf8'));
    assert.equal(policy.primaryAbi, 'arm64-v8a');
    assert.equal(policy.emulatorAbi, 'x86_64');
    assert.equal(policy.minSdk, 29);
  });
});
