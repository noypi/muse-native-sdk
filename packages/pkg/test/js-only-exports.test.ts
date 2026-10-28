'use strict';
// TC-PKG-009 same-form half (AC-PKG-009) — Batch 3 (T5 contract).
// Hermetic: node:test + node:assert/strict only. No network, no secrets,
// no live services, no fs fixtures, no real package registry.
// Binds ONLY to the pinned T5 `Interface:` line, never to an implementation.
// Resolution: `../lib/js-only.js` once it lands, else the `../index.js` shim,
// so every behavior case below fails until T5 lands.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let jsOnlyMod = {};
try {
  jsOnlyMod = require('../lib/js-only.js');
} catch {
  try {
    jsOnlyMod = require('../index.js');
  } catch {
    jsOnlyMod = {};
  }
}
const { importPackage } = jsOnlyMod;

function assertPkgErrorShape(err, expectedPathPart) {
  assert.ok(err, 'must reject with a PkgError');
  assert.equal(typeof err.code, 'string', 'PkgError must carry a string code');
  assert.ok(/^E_/.test(err.code), `code must be a named E_* error, got: ${err.code}`);
  assert.equal(typeof err.path, 'string', 'PkgError must carry a string path');
  assert.equal(typeof err.expected, 'string', 'PkgError must carry a string expected');
  if (expectedPathPart !== undefined) {
    assert.ok(
      String(err.path).includes(expectedPathPart),
      `error path must name "${expectedPathPart}", got: ${err.path}`,
    );
  }
  return err;
}

// ---------------------------------------------------------------------------
// importPackage — T5 contract:
// `importPackage(specifier: string): Promise<unknown>`
// (same form for JS-only and native packages)
// ---------------------------------------------------------------------------
describe('TC-PKG-009 — importPackage same statement form (AC-PKG-009)', () => {
  it('is exported as a single function used for both JS-only and native packages', () => {
    assert.equal(typeof importPackage, 'function');
  });

  it('returns a Promise for a JS-only package specifier', () => {
    const out = importPackage('acme-widgets');
    assert.equal(typeof out.then, 'function', 'importPackage must return a thenable');
    return out.then(
      () => {},
      () => {},
    );
  });

  it('uses the same call shape for a native package specifier (same form)', () => {
    const jsOnlyCall = importPackage('acme-widgets');
    const nativeCall = importPackage('acme-engine');
    assert.equal(typeof jsOnlyCall.then, 'function');
    assert.equal(typeof nativeCall.then, 'function');
    return Promise.allSettled([jsOnlyCall, nativeCall]).then(() => {});
  });

  it('rejects an unknown specifier fail-closed with a named error (ASSUMPTION: unknown fails, exact code not pinned)', async () => {
    await assert.rejects(importPackage('no-such-package-xyz'), (err) => {
      assertPkgErrorShape(err, 'no-such-package-xyz');
      return true;
    });
  });

  it('rejects an empty specifier with a named error (input validation)', async () => {
    await assert.rejects(importPackage(''), (err) => {
      assertPkgErrorShape(err);
      return true;
    });
  });

  it('rejects a traversal specifier with a named error (security)', async () => {
    await assert.rejects(importPackage('../evil'), (err) => {
      assertPkgErrorShape(err, '../evil');
      return true;
    });
  });

  it('settles deterministically: two imports of the same unknown specifier fail with the same code', async () => {
    const [a, b] = await Promise.allSettled([
      importPackage('no-such-package-xyz'),
      importPackage('no-such-package-xyz'),
    ]);
    assert.equal(a.status, 'rejected');
    assert.equal(b.status, 'rejected');
    assert.equal(a.reason.code, b.reason.code);
  });
});
