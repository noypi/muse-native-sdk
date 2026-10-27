'use strict';
// T3 — release checklist generator + synthetic expired-API build gate
// (NFR-BUILD-009 / AC-BUILD-024, AC-BUILD-025).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Builds tmp fixture catalogs under os.tmpdir(); no network, no secrets,
// no live services.
// Red state: ../generateReleaseChecklist.js, ../complianceGate.js and
// ../../dist/release-checklist.md do not exist yet, so every case below
// fails (MODULE_NOT_FOUND / ENOENT) until T2+T3 land.
//
// Interfaces under test (plan verbatim):
//   generateReleaseChecklist(catalogPath: string, outPath: string): void
// Renders the policy table (level, deadline, owning change) from the T1
// catalog; throws `Error('catalog missing <key>: <path>')` naming the
// absent key. Reads target/min SDK + NDK pin from T1's catalog so the
// checklist cannot drift. The synthetic test pins an expired target API in
// a temp catalog copy and asserts `complianceGate` fails before any upload
// step while the current level passes (TC-BUILD-025 intent);
// `dist/release-checklist.md` is the generated output (TC-BUILD-024 intent).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const CI_DIR = __dirname;
const REPO_ROOT = path.resolve(CI_DIR, '..', '..');
const DIST_CHECKLIST = path.join(REPO_ROOT, 'dist', 'release-checklist.md');

const CURRENT_TARGET_SDK = 35;
const EXPIRED_TARGET_SDK = 34;

function loadGenerator() {
  return require('./generateReleaseChecklist.js');
}

function loadGate() {
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

function compliantManifest() {
  return (
    '<?xml version="1.0" encoding="utf-8"?>\n' +
    '<manifest xmlns:android="http://schemas.android.com/apk/res/android">\n' +
    '  <application android:enableOnBackInvokedCallback="true">\n' +
    '    <activity android:name=".MainActivity" android:exported="true" android:windowLayoutInDisplayCutoutMode="shortEdges" />\n' +
    '    <service android:name=".PlaybackService" android:foregroundServiceType="mediaPlayback" />\n' +
    '  </application>\n' +
    '</manifest>\n'
  );
}

// T3 fixture repo mirrors the T2 layout assumption: the gate resolves the
// manifest at <root>/app/src/main/AndroidManifest.xml from the catalog at
// <root>/gradle/libs.versions.toml.
function mkRepo({ catalog } = {}) {
  const root = mkTmp('release-checklist-');
  const catalogPath = writeFile(root, path.join('gradle', 'libs.versions.toml'), catalogContent(catalog));
  writeFile(root, path.join('app', 'src', 'main', 'AndroidManifest.xml'), compliantManifest());
  return { root, catalogPath };
}

describe('T3 — generateReleaseChecklist renders the policy table from the catalog', () => {
  it('renders target/min SDK + NDK pin into a policy table (level, deadline, owning change)', () => {
    const { generateReleaseChecklist } = loadGenerator();
    const { catalogPath } = mkRepo();
    const outPath = path.join(mkTmp('release-checklist-out-'), 'release-checklist.md');
    generateReleaseChecklist(catalogPath, outPath);
    const md = fs.readFileSync(outPath, 'utf8');
    // Values must come from the catalog so the checklist cannot drift.
    assert.ok(md.includes('35'), 'checklist must render the catalog targetSdk');
    assert.ok(md.includes('29'), 'checklist must render the catalog minSdk');
    assert.ok(md.includes('27.2.12479018'), 'checklist must render the catalog NDK pin');
    // Policy-table columns (ASSUMPTION: exact headings owned by the
    // generator; the plan pins level, deadline, owning change).
    assert.match(md, /level/i, 'checklist must have a level column');
    assert.match(md, /deadline/i, 'checklist must have a deadline column');
    assert.match(md, /change|owner/i, 'checklist must name the owning change');
  });

  it('reflects the catalog values instead of hardcoded levels', () => {
    const { generateReleaseChecklist } = loadGenerator();
    const { catalogPath } = mkRepo({ catalog: { targetSdk: 36, minSdk: 30 } });
    const outPath = path.join(mkTmp('release-checklist-drift-'), 'release-checklist.md');
    generateReleaseChecklist(catalogPath, outPath);
    const md = fs.readFileSync(outPath, 'utf8');
    assert.ok(md.includes('36'), 'checklist must follow the catalog targetSdk, not a hardcoded 35');
    assert.ok(md.includes('30'), 'checklist must follow the catalog minSdk');
  });
});

describe('T3 — generateReleaseChecklist throws on a missing catalog key', () => {
  it("throws Error('catalog missing targetSdk: <path>') when targetSdk is absent", () => {
    const { generateReleaseChecklist } = loadGenerator();
    const root = mkTmp('release-checklist-no-target-');
    const catalogPath = writeFile(
      root,
      path.join('gradle', 'libs.versions.toml'),
      '[versions]\ncompileSdk = 35\nminSdk = 29\nndkVersion = "27.2.12479018"\n'
    );
    const outPath = path.join(root, 'out.md');
    assert.throws(
      () => generateReleaseChecklist(catalogPath, outPath),
      (err) => err instanceof Error && err.message === `catalog missing targetSdk: ${catalogPath}`
    );
  });

  it("throws Error('catalog missing minSdk: <path>') when minSdk is absent", () => {
    const { generateReleaseChecklist } = loadGenerator();
    const root = mkTmp('release-checklist-no-min-');
    const catalogPath = writeFile(
      root,
      path.join('gradle', 'libs.versions.toml'),
      '[versions]\ncompileSdk = 35\ntargetSdk = 35\nndkVersion = "27.2.12479018"\n'
    );
    const outPath = path.join(root, 'out.md');
    assert.throws(
      () => generateReleaseChecklist(catalogPath, outPath),
      (err) => err instanceof Error && err.message === `catalog missing minSdk: ${catalogPath}`
    );
  });

  it("throws Error('catalog missing ndkVersion: <path>') when ndkVersion is absent", () => {
    const { generateReleaseChecklist } = loadGenerator();
    const root = mkTmp('release-checklist-no-ndk-');
    const catalogPath = writeFile(
      root,
      path.join('gradle', 'libs.versions.toml'),
      '[versions]\ncompileSdk = 35\ntargetSdk = 35\nminSdk = 29\n'
    );
    const outPath = path.join(root, 'out.md');
    assert.throws(
      () => generateReleaseChecklist(catalogPath, outPath),
      (err) => err instanceof Error && err.message === `catalog missing ndkVersion: ${catalogPath}`
    );
  });
});

describe('T3 — synthetic expired-API build gate (TC-BUILD-025 intent)', () => {
  // Models "before any upload step": the upload callback runs only when the
  // gate passes.
  function maybeUpload(catalogPath, upload) {
    const { complianceGate } = loadGate();
    const gate = complianceGate(catalogPath);
    if (!gate.ok) {
      return gate;
    }
    upload();
    return gate;
  }

  it('an expired target API in a temp catalog copy fails the gate so no upload runs', () => {
    const { catalogPath } = mkRepo({ catalog: { targetSdk: EXPIRED_TARGET_SDK } });
    let uploads = 0;
    const gate = maybeUpload(catalogPath, () => {
      uploads += 1;
    });
    assert.equal(gate.ok, false);
    assert.equal(uploads, 0, 'the upload step must not run when the gate fails');
  });

  it('the current target level passes the gate so the upload step runs', () => {
    const { catalogPath } = mkRepo({ catalog: { targetSdk: CURRENT_TARGET_SDK } });
    let uploads = 0;
    const gate = maybeUpload(catalogPath, () => {
      uploads += 1;
    });
    assert.equal(gate.ok, true);
    assert.equal(uploads, 1, 'the upload step runs once the gate passes');
  });
});

describe('T3 — generated dist output (TC-BUILD-024 intent)', () => {
  it('dist/release-checklist.md exists and carries the current target level', () => {
    const md = fs.readFileSync(DIST_CHECKLIST, 'utf8');
    assert.ok(md.includes(String(CURRENT_TARGET_SDK)), 'generated checklist must carry the current level');
    assert.match(md, /level/i, 'generated checklist must render the policy table');
  });
});
