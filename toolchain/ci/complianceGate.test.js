'use strict';
// T2 — SDK policy compliance gate (NFR-BUILD-009 / AC-BUILD-023, AC-BUILD-025).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Builds tmp fixture repo trees (gradle/libs.versions.toml + app manifest)
// under os.tmpdir(); no network, no secrets, no live services.
// Red state: ../complianceGate.js and ../sdk-policy.json do not exist yet,
// so every case below fails (MODULE_NOT_FOUND / ENOENT) until T2 lands.
//
// Interface under test (plan verbatim):
//   complianceGate(catalogPath: string): { ok: boolean, violations: string[] }
// Reads the T1 catalog plus the app manifest; fails when targetSdk is below
// the enforced Play level (35) or the predictive-back opt-in /
// foreground-service-type / edge-to-edge declarations are missing;
// violations name the file and missing declaration.
//
// ASSUMPTION (fixture layout): the app manifest is resolved at
// `<repoRoot>/app/src/main/AndroidManifest.xml` where `<repoRoot>` is the
// parent of the `gradle/` directory owning `catalogPath`. The coder owns the
// exact match strings; fixtures use the canonical manifest attributes below.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const CI_DIR = __dirname;
const POLICY_FILE = path.join(CI_DIR, 'sdk-policy.json');

const ENFORCED_TARGET_SDK = 35;
const EXPIRED_TARGET_SDK = 34;

function loadImpl() {
  return require('./complianceGate.js');
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

function writeFile(root, rel, content) {
  const full = path.join(root, rel);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, content);
  return full;
}

function catalogContent({ targetSdk = 35, minSdk = 29, ndkVersion = '27.2.12479018' } = {}) {
  return (
    '[versions]\n' +
    `compileSdk = ${targetSdk}\n` +
    `targetSdk = ${targetSdk}\n` +
    `minSdk = ${minSdk}\n` +
    `ndkVersion = "${ndkVersion}"\n`
  );
}

// Canonical manifest markers the gate checks (see ASSUMPTION above):
// - predictive-back opt-in: android:enableOnBackInvokedCallback="true"
// - foreground-service-type: android:foregroundServiceType="..."
// - edge-to-edge: android:windowLayoutInDisplayCutoutMode="shortEdges"
function manifestContent({ predictiveBack = true, foregroundServiceType = true, edgeToEdge = true } = {}) {
  const appAttrs = predictiveBack ? ' android:enableOnBackInvokedCallback="true"' : '';
  const service = foregroundServiceType
    ? '    <service android:name=".PlaybackService" android:foregroundServiceType="mediaPlayback" />\n'
    : '    <service android:name=".PlaybackService" />\n';
  const activityAttrs = edgeToEdge ? ' android:windowLayoutInDisplayCutoutMode="shortEdges"' : '';
  return (
    '<?xml version="1.0" encoding="utf-8"?>\n' +
    '<manifest xmlns:android="http://schemas.android.com/apk/res/android">\n' +
    `  <application${appAttrs}>\n` +
    `    <activity android:name=".MainActivity" android:exported="true"${activityAttrs} />\n` +
    service +
    '  </application>\n' +
    '</manifest>\n'
  );
}

// Builds a tmp repo tree: <root>/gradle/libs.versions.toml and
// <root>/app/src/main/AndroidManifest.xml. Returns { root, catalogPath }.
function mkRepo({ catalog = {}, manifest = {} } = {}) {
  const root = mkTmp('compliance-gate-');
  const catalogPath = writeFile(root, path.join('gradle', 'libs.versions.toml'), catalogContent(catalog));
  writeFile(root, path.join('app', 'src', 'main', 'AndroidManifest.xml'), manifestContent(manifest));
  return { root, catalogPath };
}

describe('T2 — complianceGate contract shape', () => {
  it('returns { ok: boolean, violations: string[] } on a compliant tree', () => {
    const { complianceGate } = loadImpl();
    const { catalogPath } = mkRepo();
    const result = complianceGate(catalogPath);
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T2 — compliant manifest passes', () => {
  it('a compliant manifest at the current target level passes with no violations', () => {
    const { complianceGate } = loadImpl();
    const { catalogPath } = mkRepo();
    const result = complianceGate(catalogPath);
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });

  it('a targetSdk above the enforced level stays passing (forward-compatible)', () => {
    // ASSUMPTION: the gate fails only when targetSdk is *below* the enforced
    // level; a newer level still satisfies the Play floor.
    const { complianceGate } = loadImpl();
    const { catalogPath } = mkRepo({ catalog: { targetSdk: 36 } });
    const result = complianceGate(catalogPath);
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T2 — expired target API fails (TC-BUILD-023)', () => {
  it('a catalog pinned to an expired target API fails naming the catalog and targetSdk', () => {
    const { complianceGate } = loadImpl();
    const { catalogPath } = mkRepo({ catalog: { targetSdk: EXPIRED_TARGET_SDK } });
    const result = complianceGate(catalogPath);
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
    const named = result.violations.some(
      (v) => v.includes('libs.versions.toml') && /targetSdk|35/i.test(v)
    );
    assert.ok(
      named,
      `a violation must name the catalog file and the targetSdk shortfall, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a catalog with no targetSdk key fails naming the missing key and file', () => {
    // ASSUMPTION: a gate that cannot establish the target level fails closed.
    const { complianceGate } = loadImpl();
    const root = mkTmp('compliance-gate-no-target-');
    const catalogPath = writeFile(
      root,
      path.join('gradle', 'libs.versions.toml'),
      '[versions]\ncompileSdk = 35\nminSdk = 29\nndkVersion = "27.2.12479018"\n'
    );
    writeFile(root, path.join('app', 'src', 'main', 'AndroidManifest.xml'), manifestContent());
    const result = complianceGate(catalogPath);
    assert.equal(result.ok, false);
    const named = result.violations.some(
      (v) => v.includes('libs.versions.toml') && /targetSdk/i.test(v)
    );
    assert.ok(
      named,
      `a violation must name the missing targetSdk key and file, got: ${JSON.stringify(result.violations)}`
    );
  });
});

describe('T2 — missing manifest declarations fail', () => {
  it('a manifest without the predictive-back opt-in fails naming the file and declaration', () => {
    const { complianceGate } = loadImpl();
    const { catalogPath } = mkRepo({ manifest: { predictiveBack: false } });
    const result = complianceGate(catalogPath);
    assert.equal(result.ok, false);
    const named = result.violations.some(
      (v) => v.includes('AndroidManifest.xml') && /predictive|back/i.test(v)
    );
    assert.ok(
      named,
      `a violation must name the manifest and the predictive-back declaration, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a manifest without a foreground-service-type fails naming the file and declaration', () => {
    const { complianceGate } = loadImpl();
    const { catalogPath } = mkRepo({ manifest: { foregroundServiceType: false } });
    const result = complianceGate(catalogPath);
    assert.equal(result.ok, false);
    const named = result.violations.some(
      (v) => v.includes('AndroidManifest.xml') && /foreground/i.test(v)
    );
    assert.ok(
      named,
      `a violation must name the manifest and the foreground-service-type declaration, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a manifest without the edge-to-edge declaration fails naming the file and declaration', () => {
    const { complianceGate } = loadImpl();
    const { catalogPath } = mkRepo({ manifest: { edgeToEdge: false } });
    const result = complianceGate(catalogPath);
    assert.equal(result.ok, false);
    const named = result.violations.some(
      (v) => v.includes('AndroidManifest.xml') && /edge/i.test(v)
    );
    assert.ok(
      named,
      `a violation must name the manifest and the edge-to-edge declaration, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a missing manifest fails closed naming the expected manifest path', () => {
    // ASSUMPTION: a gate that cannot read the manifest fails closed.
    const { complianceGate } = loadImpl();
    const root = mkTmp('compliance-gate-no-manifest-');
    const catalogPath = writeFile(root, path.join('gradle', 'libs.versions.toml'), catalogContent());
    const result = complianceGate(catalogPath);
    assert.equal(result.ok, false);
    const named = result.violations.some((v) => v.includes('AndroidManifest.xml'));
    assert.ok(
      named,
      `a violation must name the missing manifest, got: ${JSON.stringify(result.violations)}`
    );
  });
});

describe('T2 — sdk-policy.json pins', () => {
  it('policy pins enforcedTargetSdk 35 and covers all three manifest declarations', () => {
    const policy = JSON.parse(fs.readFileSync(POLICY_FILE, 'utf8'));
    assert.equal(policy.enforcedTargetSdk, ENFORCED_TARGET_SDK);
    const blob = JSON.stringify(policy);
    assert.match(blob, /predictive|back/i, 'policy must cover the predictive-back declaration');
    assert.match(blob, /foreground/i, 'policy must cover the foreground-service-type declaration');
    assert.match(blob, /edge/i, 'policy must cover the edge-to-edge declaration');
  });
});
