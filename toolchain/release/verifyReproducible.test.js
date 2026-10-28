'use strict';

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const { verifyRelease } = require('./verifyReproducible.js');
const { emitSbom } = require('./sbom.js');
const { emitProvenance } = require('./provenance.js');

const BUILD_META = {
  builder: 'muse-release-builder-test',
  commit: 'abc123def456test',
  ndkVersion: 'r27c-test',
  lockedInputsDigest: 'd'.repeat(64),
};

function sha256Hex(bytes) {
  return crypto.createHash('sha256').update(bytes).digest('hex');
}

function lockedDeps() {
  return [
    {
      name: 'lib-a-test',
      version: '1.2.3',
      integrity: 'sha512-aaaa-test-not-real',
      ecosystem: 'npm-test',
    },
  ];
}

// Creates two byte-identical fixture dirs plus valid SBOM/provenance files
// in a fresh tmpdir. Returns { root, dirA, dirB, sbomPath, provPath }.
// Caller mutates copies to build failure cases; cleanup via rmSync.
function makeMatchingFixture() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'verify-repro-test-'));
  const dirA = path.join(root, 'build-a');
  const dirB = path.join(root, 'build-b');
  fs.mkdirSync(dirA, { recursive: true });
  fs.mkdirSync(dirB, { recursive: true });
  const files = {
    'app.aab': Buffer.from('fake-aab-bytes-001'),
    'app.apk': Buffer.from('fake-apk-bytes-002'),
  };
  for (const [name, bytes] of Object.entries(files)) {
    fs.writeFileSync(path.join(dirA, name), bytes);
    fs.writeFileSync(path.join(dirB, name), bytes);
  }
  const subjects = Object.entries(files).map(([name, bytes]) => ({
    path: name,
    sha256: sha256Hex(bytes),
  }));
  const sbomPath = path.join(root, 'sbom.json');
  const provPath = path.join(root, 'provenance.json');
  fs.writeFileSync(
    sbomPath,
    emitSbom(lockedDeps(), { ndkVersion: BUILD_META.ndkVersion, commit: BUILD_META.commit })
  );
  fs.writeFileSync(provPath, emitProvenance(subjects, BUILD_META));
  return { root, dirA, dirB, sbomPath, provPath };
}

const tmpRoots = [];
function track(fixture) {
  tmpRoots.push(fixture.root);
  return fixture;
}

afterEach(() => {
  while (tmpRoots.length > 0) {
    const root = tmpRoots.pop();
    fs.rmSync(root, { recursive: true, force: true });
  }
});

describe('verifyRelease', () => {
  it('returns ok:true with empty failures for byte-identical dirs with valid SBOM and provenance', () => {
    const f = track(makeMatchingFixture());
    assert.deepEqual(
      verifyRelease(f.dirA, f.dirB, { expectedSbomPath: f.sbomPath, expectedProvenancePath: f.provPath }),
      { ok: true, failures: [] }
    );
  });

  it('returns ok:false naming the file on a one-byte content difference', () => {
    const f = track(makeMatchingFixture());
    fs.writeFileSync(path.join(f.dirB, 'app.apk'), Buffer.from('fake-apk-bytes-003'));
    const res = verifyRelease(f.dirA, f.dirB, {
      expectedSbomPath: f.sbomPath,
      expectedProvenancePath: f.provPath,
    });
    assert.equal(res.ok, false);
    assert.ok(
      res.failures.some((msg) => msg.includes('app.apk')),
      `names differing file, got: ${JSON.stringify(res.failures)}`
    );
  });

  it('returns ok:false naming the extra file present in only one dir', () => {
    const f = track(makeMatchingFixture());
    fs.writeFileSync(path.join(f.dirB, 'extra.so'), Buffer.from('extra-bytes'));
    const res = verifyRelease(f.dirA, f.dirB, {
      expectedSbomPath: f.sbomPath,
      expectedProvenancePath: f.provPath,
    });
    assert.equal(res.ok, false);
    assert.ok(
      res.failures.some((msg) => msg.includes('extra.so')),
      `names extra file, got: ${JSON.stringify(res.failures)}`
    );
  });

  it('returns ok:false naming the file missing from the second dir', () => {
    const f = track(makeMatchingFixture());
    fs.rmSync(path.join(f.dirB, 'app.aab'));
    const res = verifyRelease(f.dirA, f.dirB, {
      expectedSbomPath: f.sbomPath,
      expectedProvenancePath: f.provPath,
    });
    assert.equal(res.ok, false);
    assert.ok(
      res.failures.some((msg) => msg.includes('app.aab')),
      `names missing file, got: ${JSON.stringify(res.failures)}`
    );
  });

  it('returns ok:false when the SBOM file is missing', () => {
    const f = track(makeMatchingFixture());
    const res = verifyRelease(f.dirA, f.dirB, {
      expectedSbomPath: path.join(f.root, 'no-sbom.json'),
      expectedProvenancePath: f.provPath,
    });
    assert.equal(res.ok, false);
    assert.ok(res.failures.length > 0);
  });

  it('returns ok:false when the SBOM does not parse (corrupt JSON)', () => {
    const f = track(makeMatchingFixture());
    fs.writeFileSync(f.sbomPath, 'not json{{{');
    const res = verifyRelease(f.dirA, f.dirB, {
      expectedSbomPath: f.sbomPath,
      expectedProvenancePath: f.provPath,
    });
    assert.equal(res.ok, false);
    assert.ok(res.failures.length > 0);
  });

  it('returns ok:false naming the tampered artifact on provenance mismatch', () => {
    const f = track(makeMatchingFixture());
    // Tamper second dir AND re-point provenance at the original digests is
    // already the case: provenance covers original bytes, dirB differs.
    fs.writeFileSync(path.join(f.dirB, 'app.aab'), Buffer.from('tampered-aab-bytes'));
    const res = verifyRelease(f.dirA, f.dirB, {
      expectedSbomPath: f.sbomPath,
      expectedProvenancePath: f.provPath,
    });
    assert.equal(res.ok, false);
    assert.ok(
      res.failures.some((msg) => msg.includes('app.aab')),
      `names tampered artifact, got: ${JSON.stringify(res.failures)}`
    );
  });

  it('returns ok:false when the provenance file is missing', () => {
    const f = track(makeMatchingFixture());
    const res = verifyRelease(f.dirA, f.dirB, {
      expectedSbomPath: f.sbomPath,
      expectedProvenancePath: path.join(f.root, 'no-provenance.json'),
    });
    assert.equal(res.ok, false);
    assert.ok(res.failures.length > 0);
  });

  it('never throws on missing dirs — reports them as failures', () => {
    const f = track(makeMatchingFixture());
    const missingA = path.join(f.root, 'does-not-exist-a');
    const missingB = path.join(f.root, 'does-not-exist-b');
    let res;
    assert.doesNotThrow(() => {
      res = verifyRelease(missingA, missingB, {
        expectedSbomPath: f.sbomPath,
        expectedProvenancePath: f.provPath,
      });
    });
    assert.equal(res.ok, false);
    assert.ok(res.failures.length > 0);
  });

  it('returns ok:false with failures (no throw) when the second dir is missing', () => {
    const f = track(makeMatchingFixture());
    let res;
    assert.doesNotThrow(() => {
      res = verifyRelease(f.dirA, path.join(f.root, 'does-not-exist-b'), {
        expectedSbomPath: f.sbomPath,
        expectedProvenancePath: f.provPath,
      });
    });
    assert.equal(res.ok, false);
    assert.ok(res.failures.length > 0);
  });
});
