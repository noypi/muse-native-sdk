'use strict';
// T7b — license audit: denylist gate plus committed SBOM-adjacent report
// (AC-049 / TC-049).
// Hermetic: node:test + node:assert/strict + stdlib (fs/path) only. All
// audit inputs are inline literals; the only filesystem read is the
// checked-in fixture ../../dist/license-report.json, which the coder commits
// as the output of auditLicenses. No network, no secrets, no live services.
// Red state: ./licenseAudit.js does not exist yet (and the dist fixture is
// not committed yet), so every case below fails — MODULE_NOT_FOUND for the
// audit cases, ENOENT for the fixture cases — until the coder lands T7.
//
// Interface under test (plan verbatim):
//   auditLicenses(pkgs:{name,version,license}[], denylist:string[]): {ok, blocked:string[], report:{generatedAt, packages, blocked}}
//
// ASSUMPTIONs (contract is silent here; coder must satisfy these):
//   D1. License matching is case-insensitive on trimmed values
//       ("GPL-3.0" matches "gpl-3.0 ").
//   D2. A missing/empty license is fail-closed: the package is blocked and
//       named (reported as UNKNOWN upstream, but `blocked` carries the
//       package identity, never an empty string).
//   D3. Non-array `pkgs`/`denylist` inputs throw TypeError.
//   D4. `ok` is true iff `blocked` is empty; `report.blocked` mirrors
//       `blocked`; `report.packages` preserves every input entry.
//   D5. `report.generatedAt` is an ISO-8601 UTC string (asserted by shape
//       only — the suite never freezes or depends on the wall clock).
//   D6. The committed dist/license-report.json is the JSON-serialized
//       `report` of auditLicenses over the repo's real locked set; the suite
//       asserts its shape (generatedAt/packages/blocked), not exact package
//       contents, so lockfile churn does not break the suite.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { auditLicenses } = require('./licenseAudit.js');

const FIXTURE_FILE = path.join(__dirname, '..', '..', 'dist', 'license-report.json');

function pkg(overrides = {}) {
  return { name: 'left-pad', version: '1.3.0', license: 'MIT', ...overrides };
}

describe('auditLicenses — happy path', () => {
  it('returns ok:true with empty blocked for allowlisted licenses', () => {
    const out = auditLicenses([pkg(), pkg({ name: 'other', license: 'Apache-2.0' })], ['GPL-3.0']);
    assert.equal(out.ok, true);
    assert.deepEqual(out.blocked, []);
  });

  it('returns ok:true for empty pkgs and empty denylist', () => {
    const out = auditLicenses([], []);
    assert.equal(out.ok, true);
    assert.deepEqual(out.blocked, []);
    assert.deepEqual(out.report.packages, []);
    assert.deepEqual(out.report.blocked, []);
  });

  it('report carries generatedAt, packages, and blocked (D4/D5)', () => {
    const pkgs = [pkg()];
    const out = auditLicenses(pkgs, ['GPL-3.0']);
    assert.equal(typeof out.report.generatedAt, 'string');
    assert.ok(!Number.isNaN(Date.parse(out.report.generatedAt)), `ISO-8601 timestamp, got: ${out.report.generatedAt}`);
    assert.deepEqual(out.report.packages, pkgs);
    assert.deepEqual(out.report.blocked, out.blocked);
  });
});

describe('auditLicenses — denylist gate blocks naming the package', () => {
  it('blocks naming the package on an exact denylist hit', () => {
    const out = auditLicenses([pkg({ name: 'gpl-lib', license: 'GPL-3.0' }), pkg()], ['GPL-3.0']);
    assert.equal(out.ok, false);
    assert.ok(out.blocked.some((b) => b.includes('gpl-lib')), `names package, got: ${JSON.stringify(out.blocked)}`);
    assert.ok(!out.blocked.some((b) => b.includes('left-pad') && !b.includes('gpl')), 'clean package is not blocked');
  });

  it('matches denylist entries case-insensitively on trimmed values (D1)', () => {
    const out = auditLicenses([pkg({ name: 'ci-lib', license: '  gpl-3.0 ' })], ['GPL-3.0']);
    assert.equal(out.ok, false);
    assert.ok(out.blocked.some((b) => b.includes('ci-lib')), `names package, got: ${JSON.stringify(out.blocked)}`);
  });

  it('is fail-closed on missing/empty license: blocked and named (D2)', () => {
    for (const [tag, license] of [['missing', undefined], ['empty', ''], ['blank', '   ']]) {
      const out = auditLicenses([pkg({ name: `no-license-${tag}`, license })], []);
      assert.equal(out.ok, false, `license ${tag} must be blocked`);
      assert.ok(out.blocked.some((b) => b.includes(`no-license-${tag}`)), `names package for ${tag}, got: ${JSON.stringify(out.blocked)}`);
    }
  });

  it('blocks every denylisted package in a mixed set', () => {
    const out = auditLicenses(
      [pkg({ name: 'bad-a', license: 'GPL-3.0' }), pkg({ name: 'good' }), pkg({ name: 'bad-b', license: 'AGPL-3.0' })],
      ['GPL-3.0', 'AGPL-3.0'],
    );
    assert.equal(out.ok, false);
    assert.equal(out.blocked.length, 2);
    assert.ok(out.blocked.some((b) => b.includes('bad-a')));
    assert.ok(out.blocked.some((b) => b.includes('bad-b')));
    assert.deepEqual(out.report.blocked, out.blocked);
  });
});

describe('auditLicenses — shape, boundary, and error paths', () => {
  it('blocked is always a string array; ok is true iff blocked is empty (D4)', () => {
    const good = auditLicenses([pkg()], []);
    assert.ok(Array.isArray(good.blocked) && good.blocked.every((b) => typeof b === 'string'));
    assert.equal(good.ok, good.blocked.length === 0);
    const bad = auditLicenses([pkg({ name: 'shape-bad', license: 'GPL-3.0' })], ['GPL-3.0']);
    assert.ok(Array.isArray(bad.blocked) && bad.blocked.every((b) => typeof b === 'string'));
    assert.equal(bad.ok, bad.blocked.length === 0);
  });

  it('throws TypeError on non-array pkgs/denylist (D3)', () => {
    assert.throws(() => auditLicenses(null, []), TypeError);
    assert.throws(() => auditLicenses('pkgs', []), TypeError);
    assert.throws(() => auditLicenses([], null), TypeError);
    assert.throws(() => auditLicenses([], 'GPL-3.0'), TypeError);
  });

  it('does not mutate its inputs', () => {
    const pkgs = [pkg({ name: 'frozen' })];
    const denylist = ['GPL-3.0'];
    const pkgsJson = JSON.stringify(pkgs);
    const denyJson = JSON.stringify(denylist);
    auditLicenses(pkgs, denylist);
    assert.equal(JSON.stringify(pkgs), pkgsJson);
    assert.equal(JSON.stringify(denylist), denyJson);
  });

  it('names hostile package identities without throwing', () => {
    const name = '../evil\nINJECT';
    const out = auditLicenses([pkg({ name, license: 'GPL-3.0' })], ['GPL-3.0']);
    assert.equal(out.ok, false);
    assert.ok(out.blocked.some((b) => b.includes(name)));
  });

  it('blocked entries and report expose only package identity, never a secret', () => {
    const out = auditLicenses([pkg({ name: 'leak-check', license: 'GPL-3.0' })], ['GPL-3.0']);
    for (const b of [...out.blocked, JSON.stringify(out.report)]) {
      assert.ok(!/token|secret|password|api[_-]?key/i.test(b), `no secret material in: ${b}`);
    }
  });
});

describe('license-report fixture — dist/license-report.json', () => {
  it('fixture exists and parses as JSON', () => {
    const raw = fs.readFileSync(FIXTURE_FILE, 'utf8');
    const parsed = JSON.parse(raw);
    assert.equal(typeof parsed, 'object');
    assert.ok(parsed !== null);
  });

  it('fixture has the auditLicenses report shape: generatedAt/packages/blocked (D6)', () => {
    const report = JSON.parse(fs.readFileSync(FIXTURE_FILE, 'utf8'));
    assert.equal(typeof report.generatedAt, 'string');
    assert.ok(!Number.isNaN(Date.parse(report.generatedAt)), `ISO-8601 generatedAt, got: ${report.generatedAt}`);
    assert.ok(Array.isArray(report.packages), 'packages is an array');
    assert.ok(Array.isArray(report.blocked), 'blocked is an array');
    for (const p of report.packages) {
      assert.equal(typeof p.name, 'string');
      assert.equal(typeof p.version, 'string');
      assert.equal(typeof p.license, 'string');
    }
    for (const b of report.blocked) {
      assert.equal(typeof b, 'string');
    }
  });
});
