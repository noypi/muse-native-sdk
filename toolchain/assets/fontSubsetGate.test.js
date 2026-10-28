'use strict';
// T2 — Font subsetting gate with guard entry (NFR-BUILD-017 / AC-BUILD-063, TC-BUILD-063).
// Hermetic: node:test + node:assert/strict only. Fixture fonts are inline dummy
// buffers (never a real font file, never a download); no secrets, no services.
// Red state: ./fontSubset.js does not exist yet (MODULE_NOT_FOUND on every
// impl-dependent case) and the strict main.cpp signature check documents the
// guard entry the coder must expose.
//
// Interface under test (plan verbatim):
//   subsetFont(font: Buffer, locales: string[]): { bytes: Buffer, coverage: string[] }
//     throws AssetError('unshipped-script')
//   main.cpp exposes `int main(int argc, char** argv)` guard entry for the canonical `:toolchain:fontSubset` task.
//
// ASSUMPTIONS:
//   A1: The shipped locale set is consumed by reference (l10n); tests use a
//       small dummy shipped list (latin-script locales) plus CJK locales as
//       the unshipped-script negative case. Exact l10n contents are not
//       redefined here; 'ja'/'zh' stand in for "scripts outside the shipped set".
//   A2: Fixture fonts are marker buffers: LATIN_FONT carries no CJK marker,
//       CJK_FONT carries a 'CJK' marker the implementation detects. Real font
//       parsing is not unit-testable hermetically; marker detection is the
//       agreed boundary fake, owned by the coder to implement.
//   A3: coverage lists the requested locales that survived subsetting (subset
//       of the input locales array).
//   A4: Thrown errors expose err.code ('unshipped-script'); the helper also
//       accepts the code inside err.message.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const GUARD_CPP = path.join(__dirname, '..', 'font-subset', 'main.cpp');

function loadImpl() {
  return require('./fontSubset.js');
}

const SHIPPED_LOCALES = ['en', 'de', 'fr'];
const LATIN_FONT = Buffer.from('FONT:latin-coverage-en-de-fr', 'utf8');
const CJK_FONT = Buffer.from('FONT:CJK-unified-ideographs-U+4E00', 'utf8');

function expectAssetError(fn, code) {
  let thrown = null;
  try {
    fn();
  } catch (err) {
    thrown = err;
  }
  assert.ok(thrown, `expected AssetError('${code}') to be thrown`);
  const blob = `${thrown.code || ''} ${thrown.message || ''}`;
  assert.ok(
    thrown.code === code || blob.includes(code),
    `expected error code '${code}', got code=${JSON.stringify(thrown.code)} message=${JSON.stringify(thrown.message)}`
  );
}

describe('T2 — subsetFont happy path (shipped locales)', () => {
  it('returns { bytes: Buffer, coverage: string[] } for a shipped-script font', () => {
    const { subsetFont } = loadImpl();
    const out = subsetFont(LATIN_FONT, SHIPPED_LOCALES);
    assert.ok(Buffer.isBuffer(out.bytes), 'bytes must be a Buffer');
    assert.ok(Array.isArray(out.coverage), 'coverage must be an array');
    for (const loc of SHIPPED_LOCALES) {
      assert.ok(out.coverage.includes(loc), `coverage must include requested locale ${loc}`);
    }
  });

  it('coverage never exceeds the requested locales', () => {
    const { subsetFont } = loadImpl();
    const out = subsetFont(LATIN_FONT, ['en']);
    assert.ok(out.coverage.every((l) => ['en'].includes(l)), 'coverage must be a subset of the request');
  });
});

describe('T2 — unshipped-script rejection (AC-BUILD-063 audit)', () => {
  it("throws AssetError('unshipped-script') for a CJK font against shipped-only locales", () => {
    const { subsetFont } = loadImpl();
    expectAssetError(() => subsetFont(CJK_FONT, SHIPPED_LOCALES), 'unshipped-script');
  });

  it("throws AssetError('unshipped-script') when requesting an unshipped CJK locale", () => {
    // ASSUMPTION A1: 'ja' stands in for a script outside the shipped set.
    const { subsetFont } = loadImpl();
    expectAssetError(() => subsetFont(LATIN_FONT, ['ja']), 'unshipped-script');
  });

  it('rejects non-Buffer font input with an AssetError (input validation)', () => {
    const { subsetFont } = loadImpl();
    let thrown = null;
    try {
      subsetFont('not-a-buffer', SHIPPED_LOCALES);
    } catch (err) {
      thrown = err;
    }
    assert.ok(thrown, 'expected an AssetError for non-Buffer font input');
    assert.ok(thrown.code || thrown.message, 'error must carry a code or message');
  });
});

describe('T2 — guard entry toolchain/font-subset/main.cpp', () => {
  it('exposes `int main(int argc, char** argv)` for the canonical :toolchain:fontSubset task', () => {
    const src = fs.readFileSync(GUARD_CPP, 'utf8');
    assert.ok(
      src.includes('int main(int argc, char** argv)'),
      'main.cpp must expose the verbatim guard entry `int main(int argc, char** argv)`'
    );
  });
});
