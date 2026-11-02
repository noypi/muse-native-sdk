'use strict';
// T9 + T10 — Batch 5 suite reconciliation for `vendor-d3` (plan addendum 2026-11-02).
//
// T9 (plan Interface: `resolvePins(abi, artifact) -> {version, sha256, url}`
// for the three D3 artifacts on all 4 ABIs, EUNPINNED otherwise preserved):
// the committed `toolchain/deps/pins.json` D3 rows trip `setup.js`
// `resolvePins` EUNPINNED because `EXACT_VERSION_RE` demands X.Y.Z while
// `icu4c-79.1` is pinned at two-part `79.1` (planner's verified diagnosis).
// These tests pin per-ABI resolution returning ALL pins including the D3
// rows, with EUNPINNED preserved for genuinely unknown artifacts.
// T10 (plan Interface: `licenseReport() ->
// {generatedAt: string, packages: [...D3 with sha256/url...], blocked: []}`):
// `dist/license-report.json` must keep the `auditLicenses` shape AND carry
// the D3 rows inside `packages[]` with BOTH `license` (American spelling,
// gate-allowed token) and the `licence`/`licence_url`/`sha256`/`url` extras.
//
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only. T9
// never reads the committed pins.json: each case copies the unit under test
// (`setup.js`, unedited) into a fresh tmp dir next to a tmp fixture
// `pins.json` and requires that copy, so the REAL resolver runs against
// tmp data only. T10 builds D3 package literals inline; the only committed
// read is `dist/license-report.json` (read-only shape assertion). No
// network, no secrets, no application config, no live service.
//
// Red state: `setup.js` rejects two-part `79.1`, so every T9 case resolving
// a fixture with the icu row fails EUNPINNED until the coder relaxes the
// version-shape rule (resolver logic only — no pin hash/url change); the
// committed report's D3 rows carry `licence` but no `license`, so the T10
// dual-spelling cases fail until the coder records the gate-allowed token.
//
// Shared-file rule (GR-08 T1.8): this file is assigned to T9 and mapped in
// both unit-tests/T9.md and unit-tests/T10.md.
//
// ASSUMPTIONS (contract silent; coder must satisfy these):
//   A1. Plan T9 writes `resolvePins(abi, artifact)` but the settled,
//       implemented boundary (setup.js + setup.test.js verbatim Interface)
//       is `resolvePins(abi) -> pin[]`; the tests assert per-ABI resolution
//       returning all pins (planner guidance), not a new two-arg overload.
//   A2. Supported ABIs stay exactly arm64-v8a, armeabi-v7a, x86, x86_64.
//   A3. D3 coordinates stay the run-frozen pins (hb-15.0.0 15.0.0
//       968536e9…4a44df, icu-79.1 79.1 06fa63ec…b191d99, ft-2.15.0 2.15.0
//       91866274…a8d92f64, official https origins).
//   A4. Failure codes stay `EUNPINNED` (unpinned/unknown) vs `EHASH`
//       (malformed sha256); SetupError carries string code/artifact/abi.
//   A5. Gate-allowed D3 licence tokens: HarfBuzz MIT (Old-MIT provenance in
//       licence_url/notices per T6 resolution-(a)), ICU Unicode, FreeType FTL.
//   A6. resolvePins is synchronous and performs zero network calls.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { auditLicenses } = require('./licenseAudit.js');

const THIS_DIR = __dirname;
const SETUP_SRC = path.join(THIS_DIR, 'setup.js');
const REPORT_FILE = path.join(THIS_DIR, '..', '..', 'dist', 'license-report.json');

const ABIS = ['arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64'];

const D3 = [
  {
    artifact: 'harfbuzz-15.0.0',
    version: '15.0.0',
    sha256: '968536e9abd782f07f7954044c0c4020199fcdd39b41d221ec42d2a3be4a44df',
    url: 'https://github.com/harfbuzz/harfbuzz/releases/download/15.0.0/harfbuzz-15.0.0.tar.xz',
  },
  {
    artifact: 'icu4c-79.1',
    version: '79.1',
    sha256: '06fa63ecb521048f8082f12ad988f6b678d90cbd88aafce24cbc94902b191d99',
    url: 'https://github.com/unicode-org/icu/releases/download/release-79-1/icu4c-79.1-sources.tgz',
  },
  {
    artifact: 'freetype-2.15.0',
    version: '2.15.0',
    sha256: '9186627467a616d9a9e3e574748ac0c45fa77398e6364edef3041a8ba8d92f64',
    url: 'https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.gz',
  },
];

const CRONET_STUB = {
  artifact: 'cronet-bundled',
  version: '500.1.0',
  sha256: 'fa286aeebe66dcab4a64df017d566cb8ec563c3bb63af527997c3a4e056bf295',
  url: 'https://maven.google.com/org/chromium/net/cronet-bundled/500.1.0/cronet-bundled-500.1.0.aar',
};

// --- tmp-fixture harness (T9): real setup.js, tmp pins.json, never the committed one ---
const tmpDirs = [];
afterEach(() => {
  while (tmpDirs.length > 0) {
    const dir = tmpDirs.pop();
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

function loadSetupWithPins(pinsObject) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'd3-setup-'));
  tmpDirs.push(dir);
  fs.writeFileSync(path.join(dir, 'pins.json'), JSON.stringify(pinsObject));
  fs.copyFileSync(SETUP_SRC, path.join(dir, 'setup.js'));
  return require(path.join(dir, 'setup.js'));
}

function pinsWithD3(extraRows = []) {
  const pins = {};
  for (const abi of ABIS) {
    pins[abi] = [CRONET_STUB, ...D3.map((r) => ({ ...r })), ...extraRows];
  }
  return pins;
}

function assertSetupError(err, code) {
  assert.ok(err instanceof Error, `is an Error, got: ${err}`);
  assert.equal(err.code, code);
  assert.equal(typeof err.artifact, 'string');
  assert.equal(typeof err.abi, 'string');
}

describe('T9 — per-ABI resolution returns all pins including the D3 rows', () => {
  it('resolves a non-empty well-formed pin list for every supported ABI', () => {
    const { resolvePins } = loadSetupWithPins(pinsWithD3());
    for (const abi of ABIS) {
      const pins = resolvePins(abi);
      assert.ok(Array.isArray(pins), `array for ${abi}`);
      assert.ok(pins.length >= 4, `all pins incl. D3 for ${abi}`);
      for (const entry of pins) {
        assert.equal(typeof entry.version, 'string');
        assert.ok(entry.version.length > 0);
        assert.match(String(entry.sha256), /^[0-9a-fA-F]{64}$/);
        assert.match(String(entry.url), /^https:\/\//);
      }
    }
  });

  it('returns the exact D3 versions incl. two-part icu4c-79.1 v79.1', () => {
    const { resolvePins } = loadSetupWithPins(pinsWithD3());
    for (const abi of ABIS) {
      const byArtifact = Object.fromEntries(
        resolvePins(abi).map((e) => [String(e.artifact ?? e.name), e]),
      );
      assert.equal(byArtifact['harfbuzz-15.0.0'].version, '15.0.0');
      assert.equal(byArtifact['icu4c-79.1'].version, '79.1');
      assert.equal(byArtifact['freetype-2.15.0'].version, '2.15.0');
    }
  });

  it('returns the exact D3 sha256/url rows byte-for-byte', () => {
    const { resolvePins } = loadSetupWithPins(pinsWithD3());
    for (const abi of ABIS) {
      const byArtifact = Object.fromEntries(
        resolvePins(abi).map((e) => [String(e.artifact ?? e.name), e]),
      );
      for (const want of D3) {
        const got = byArtifact[want.artifact];
        assert.ok(got, `${want.artifact} pinned for ${abi}`);
        assert.equal(String(got.sha256).toLowerCase(), want.sha256);
        assert.equal(got.url, want.url);
      }
    }
  });

  it('preserves non-D3 rows untouched alongside the D3 rows', () => {
    const { resolvePins } = loadSetupWithPins(pinsWithD3());
    for (const abi of ABIS) {
      const pins = resolvePins(abi);
      const cronet = pins.find((e) => String(e.artifact ?? e.name) === 'cronet-bundled');
      assert.ok(cronet, `Cronet row preserved for ${abi}`);
      assert.equal(cronet.version, CRONET_STUB.version);
      assert.equal(String(cronet.sha256).toLowerCase(), CRONET_STUB.sha256);
      assert.equal(cronet.url, CRONET_STUB.url);
    }
  });

  it('is deterministic across calls for the same ABI', () => {
    const { resolvePins } = loadSetupWithPins(pinsWithD3());
    assert.deepEqual(resolvePins('arm64-v8a'), resolvePins('arm64-v8a'));
  });
});

describe('T9 — EUNPINNED preserved for genuinely unknown artifacts', () => {
  it('throws EUNPINNED naming artifact+abi for an unknown ABI', () => {
    const { resolvePins } = loadSetupWithPins(pinsWithD3());
    assert.throws(() => resolvePins('mips-unknown'), (err) => {
      assertSetupError(err, 'EUNPINNED');
      assert.equal(err.artifact, 'unknown');
      assert.equal(err.abi, 'mips-unknown');
      return true;
    });
  });

  it('throws EUNPINNED for empty/missing ABI', () => {
    const { resolvePins } = loadSetupWithPins(pinsWithD3());
    for (const abi of ['', null, undefined]) {
      assert.throws(() => resolvePins(abi), (err) => {
        assert.equal(err.code, 'EUNPINNED');
        return true;
      }, `abi ${String(abi)} must fail closed`);
    }
  });

  it('throws EUNPINNED when the ABI slot is absent from the manifest', () => {
    const pins = pinsWithD3();
    delete pins.x86;
    const { resolvePins } = loadSetupWithPins(pins);
    assert.throws(() => resolvePins('x86'), (err) => {
      assertSetupError(err, 'EUNPINNED');
      return true;
    });
  });

  it('throws EUNPINNED (not EHASH) for a range/tag version, never a pass', () => {
    const pins = pinsWithD3();
    pins['arm64-v8a'] = [{ artifact: 'weird-range', version: '>=79.0', sha256: D3[0].sha256, url: D3[0].url }];
    const { resolvePins } = loadSetupWithPins(pins);
    assert.throws(() => resolvePins('arm64-v8a'), (err) => {
      assertSetupError(err, 'EUNPINNED');
      assert.equal(err.artifact, 'weird-range');
      return true;
    });
  });

  it('keeps EHASH for a malformed sha256 (hash rule unchanged)', () => {
    const pins = pinsWithD3();
    pins['arm64-v8a'] = [{ artifact: 'bad-hash', version: '1.0.0', sha256: 'not-a-hash', url: 'https://example.com/x.tar.gz' }];
    const { resolvePins } = loadSetupWithPins(pins);
    assert.throws(() => resolvePins('arm64-v8a'), (err) => {
      assertSetupError(err, 'EHASH');
      assert.equal(err.artifact, 'bad-hash');
      return true;
    });
  });

  it('names a hostile ABI without leaking secrets', () => {
    const { resolvePins } = loadSetupWithPins(pinsWithD3());
    const hostile = '../../etc/passwd\nDROP TABLE x';
    assert.throws(() => resolvePins(hostile), (err) => {
      assert.equal(err.code, 'EUNPINNED');
      assert.ok(String(err.abi).includes(hostile));
      assert.ok(!/token|secret|password|key/i.test(String(err.message)));
      return true;
    });
  });

  it('performs zero network calls (sync pure manifest read)', () => {
    const { resolvePins } = loadSetupWithPins(pinsWithD3());
    const before = typeof globalThis.fetch === 'function' ? globalThis.fetch : undefined;
    let calls = 0;
    globalThis.fetch = (...args) => { calls += 1; return before ? before(...args) : Promise.reject(new Error('no network')); };
    try {
      resolvePins('arm64-v8a');
    } finally {
      if (before === undefined) delete globalThis.fetch;
      else globalThis.fetch = before;
    }
    assert.equal(calls, 0);
  });
});

describe('T10 — auditLicenses shape with D3 rows in packages[] (dual spelling)', () => {
  function d3Packages() {
    return [
      {
        name: 'HarfBuzz', version: '15.0.0', license: 'MIT', licence: 'MIT',
        licence_url: 'https://github.com/harfbuzz/harfbuzz/blob/15.0.0/COPYING#old-mit-bsd-style',
        sha256: D3[0].sha256, url: D3[0].url,
      },
      {
        name: 'ICU4C', version: '79.1', license: 'Unicode', licence: 'Unicode',
        licence_url: 'https://www.unicode.org/copyright.html',
        sha256: D3[1].sha256, url: D3[1].url,
      },
      {
        name: 'FreeType', version: '2.15.0', license: 'FTL', licence: 'FTL',
        licence_url: 'https://freetype.org/license.html',
        sha256: D3[2].sha256, url: D3[2].url,
      },
    ];
  }

  it('keeps the auditLicenses shape: generatedAt/packages/blocked with ok true and blocked []', () => {
    const out = auditLicenses(d3Packages(), ['GPL-3.0']);
    assert.equal(out.ok, true);
    assert.deepEqual(out.blocked, []);
    assert.equal(typeof out.report.generatedAt, 'string');
    assert.ok(!Number.isNaN(Date.parse(out.report.generatedAt)));
    assert.ok(Array.isArray(out.report.packages));
    assert.deepEqual(out.report.blocked, []);
  });

  it('preserves every D3 row verbatim in report.packages incl. extras', () => {
    const pkgs = d3Packages();
    const out = auditLicenses(pkgs, []);
    assert.deepEqual(out.report.packages, pkgs);
    for (const p of out.report.packages) {
      assert.equal(typeof p.license, 'string');
      assert.equal(typeof p.licence, 'string');
      assert.match(String(p.licence_url), /^https:\/\//);
      assert.match(String(p.sha256), /^[0-9a-fA-F]{64}$/);
      assert.match(String(p.url), /^https:\/\//);
    }
  });

  it('D3 rows carry BOTH license and licence spellings with gate-allowed tokens', () => {
    const out = auditLicenses(d3Packages(), ['GPL-3.0', 'LGPL-2.1']);
    assert.equal(out.ok, true, `D3 tokens must pass the gate, blocked: ${JSON.stringify(out.blocked)}`);
    const byName = Object.fromEntries(out.report.packages.map((p) => [p.name, p]));
    assert.equal(byName.HarfBuzz.license, 'MIT');
    assert.equal(byName.ICU4C.license, 'Unicode');
    assert.equal(byName.ICU4C.licence, 'Unicode');
    assert.equal(byName.FreeType.license, 'FTL');
    assert.equal(byName.FreeType.licence, 'FTL');
  });

  it('D3 sha256 values equal the run-frozen request hashes', () => {
    const out = auditLicenses(d3Packages(), []);
    const byName = Object.fromEntries(out.report.packages.map((p) => [p.name, p]));
    assert.equal(byName.HarfBuzz.sha256, D3[0].sha256);
    assert.equal(byName.ICU4C.sha256, D3[1].sha256);
    assert.equal(byName.FreeType.sha256, D3[2].sha256);
  });

  it('committed dist/license-report.json keeps the shape with D3 rows in packages[]', () => {
    const report = JSON.parse(fs.readFileSync(REPORT_FILE, 'utf8'));
    assert.equal(typeof report.generatedAt, 'string');
    assert.ok(!Number.isNaN(Date.parse(report.generatedAt)));
    assert.ok(Array.isArray(report.packages));
    assert.ok(Array.isArray(report.blocked));
    assert.deepEqual(report.blocked, []);
    const text = JSON.stringify(report.packages);
    for (const row of D3) {
      assert.ok(text.includes(row.sha256), `report packages[] missing D3 hash ${row.sha256.slice(0, 12)}…`);
    }
  });

  it('committed report D3 rows carry license (American) plus licence extras', () => {
    const report = JSON.parse(fs.readFileSync(REPORT_FILE, 'utf8'));
    const d3Names = new Set(['HarfBuzz', 'ICU4C', 'FreeType']);
    const rows = report.packages.filter((p) => p && d3Names.has(p.name));
    assert.equal(rows.length, 3, `expected 3 D3 rows, got ${rows.length}`);
    for (const p of rows) {
      assert.equal(typeof p.license, 'string', `${p.name} missing American-spelling license`);
      assert.ok(p.license.length > 0);
      assert.equal(typeof p.licence, 'string', `${p.name} missing licence extra`);
      assert.match(String(p.licence_url), /^https:\/\//, `${p.name} licence_url`);
      assert.match(String(p.sha256), /^[0-9a-fA-F]{64}$/, `${p.name} sha256`);
      assert.match(String(p.url), /^https:\/\//, `${p.name} url`);
    }
  });
});
