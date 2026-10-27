'use strict';
// T2 — initial-bundle budget gate (NFR-BUILD-012 / AC-BUILD-037, AC-BUILD-040).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Bundle fixtures live under os.tmpdir(); the budget value is always an INPUT
// (perf-owned per plan A2 — never invented here). No network, no secrets,
// no live services.
// Red state: ./budgetGate.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND) until T2 lands.
//
// Interface under test (plan verbatim):
//   checkBudget({ bundlePath: string, budgetBytes: number, manifest: Array<{module: string, bytes: number}> }): { ok: boolean, totalBytes: number, offender?: string } — `ok:false` names the largest over-contributing module in `offender`; never throws on overrun (returns verdict), throws `BundleError { code:'BAD_INPUT' }` only on missing files.
//
// ASSUMPTION (totalBytes): totalBytes is the sum of `manifest[].bytes`.
// ASSUMPTION (boundary): totalBytes <= budgetBytes passes (`ok:true`); only a
// strict overrun fails.
// ASSUMPTION (offender): on overrun, `offender` is the `module` name of the
// manifest entry with the largest `bytes` value (first wins ties); `offender`
// is absent when `ok:true`.
// ASSUMPTION (BAD_INPUT scope): `BundleError { code:'BAD_INPUT' }` is thrown
// for a missing/unreadable bundlePath, a non-numeric or negative budgetBytes,
// or a malformed manifest (non-array, or entries without a string `module` /
// finite non-negative `bytes`). Overrun itself never throws.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function loadImpl() {
  return require('./budgetGate.js');
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

function mkBundle(dir, name = 'index.bundle.js', bytes = 64) {
  const bundlePath = path.join(dir, name);
  fs.writeFileSync(bundlePath, `// fixture-bundle\n${'x'.repeat(bytes)}\n`);
  return bundlePath;
}

const MANIFEST = [
  { module: 'startup-core', bytes: 400 },
  { module: 'home-screen', bytes: 300 },
  { module: 'settings-screen', bytes: 100 },
];

describe('T2 — checkBudget under-budget verdict', () => {
  it('under-budget returns ok:true with totalBytes equal to the manifest sum', () => {
    const { checkBudget } = loadImpl();
    const root = mkTmp('budget-under-');
    const bundlePath = mkBundle(root);
    const result = checkBudget({ bundlePath, budgetBytes: 10_000, manifest: MANIFEST });
    assert.equal(result.ok, true);
    assert.equal(result.totalBytes, 800);
    assert.equal('offender' in result, false);
  });

  it('exact-boundary total (totalBytes === budgetBytes) passes', () => {
    const { checkBudget } = loadImpl();
    const root = mkTmp('budget-exact-');
    const bundlePath = mkBundle(root);
    const result = checkBudget({ bundlePath, budgetBytes: 800, manifest: MANIFEST });
    assert.equal(result.ok, true);
    assert.equal(result.totalBytes, 800);
  });

  it('empty manifest totals 0 bytes and passes any non-negative budget', () => {
    const { checkBudget } = loadImpl();
    const root = mkTmp('budget-empty-');
    const bundlePath = mkBundle(root);
    const result = checkBudget({ bundlePath, budgetBytes: 0, manifest: [] });
    assert.equal(result.ok, true);
    assert.equal(result.totalBytes, 0);
  });
});

describe('T2 — checkBudget overrun verdict (AC-BUILD-037/040)', () => {
  it('over-budget returns ok:false (never throws) with totalBytes', () => {
    const { checkBudget } = loadImpl();
    const root = mkTmp('budget-over-');
    const bundlePath = mkBundle(root);
    const result = checkBudget({ bundlePath, budgetBytes: 100, manifest: MANIFEST });
    assert.equal(result.ok, false);
    assert.equal(result.totalBytes, 800);
  });

  it('over-budget names the largest module in offender', () => {
    const { checkBudget } = loadImpl();
    const root = mkTmp('budget-offender-');
    const bundlePath = mkBundle(root);
    const result = checkBudget({ bundlePath, budgetBytes: 100, manifest: MANIFEST });
    assert.equal(result.offender, 'startup-core');
  });

  it('offender tracks a different largest module on another fixture', () => {
    const { checkBudget } = loadImpl();
    const root = mkTmp('budget-offender2-');
    const bundlePath = mkBundle(root);
    const manifest = [
      { module: 'tiny-a', bytes: 10 },
      { module: 'huge-b', bytes: 9000 },
    ];
    const result = checkBudget({ bundlePath, budgetBytes: 50, manifest });
    assert.equal(result.ok, false);
    assert.equal(result.offender, 'huge-b');
  });
});

describe('T2 — checkBudget BAD_INPUT errors', () => {
  it('missing bundle file throws BundleError with code BAD_INPUT', () => {
    const { checkBudget, BundleError } = loadImpl();
    const root = mkTmp('budget-missing-');
    assert.throws(
      () => checkBudget({ bundlePath: path.join(root, 'nope.bundle.js'), budgetBytes: 1000, manifest: MANIFEST }),
      (err) => err instanceof BundleError && err.code === 'BAD_INPUT'
    );
  });

  it('directory-as-bundlePath throws BundleError BAD_INPUT', () => {
    const { checkBudget, BundleError } = loadImpl();
    const root = mkTmp('budget-dir-');
    assert.throws(
      () => checkBudget({ bundlePath: root, budgetBytes: 1000, manifest: MANIFEST }),
      (err) => err instanceof BundleError && err.code === 'BAD_INPUT'
    );
  });

  it('negative budgetBytes throws BundleError BAD_INPUT', () => {
    const { checkBudget, BundleError } = loadImpl();
    const root = mkTmp('budget-negbudget-');
    const bundlePath = mkBundle(root);
    assert.throws(
      () => checkBudget({ bundlePath, budgetBytes: -1, manifest: MANIFEST }),
      (err) => err instanceof BundleError && err.code === 'BAD_INPUT'
    );
  });

  it('malformed manifest entry throws BundleError BAD_INPUT', () => {
    const { checkBudget, BundleError } = loadImpl();
    const root = mkTmp('budget-badmanifest-');
    const bundlePath = mkBundle(root);
    assert.throws(
      () => checkBudget({ bundlePath, budgetBytes: 1000, manifest: [{ module: 'x' }] }),
      (err) => err instanceof BundleError && err.code === 'BAD_INPUT'
    );
  });

  it('error messages never leak env secrets', () => {
    const { checkBudget } = loadImpl();
    const root = mkTmp('budget-secret-');
    process.env.MUSE_BUNDLE_TEST_DUMMY = 'top-secret-dummy-value';
    try {
      assert.throws(
        () => checkBudget({ bundlePath: path.join(root, 'missing.js'), budgetBytes: 1, manifest: [] }),
        (err) => !String(err && err.message).includes('top-secret-dummy-value')
      );
    } finally {
      delete process.env.MUSE_BUNDLE_TEST_DUMMY;
    }
  });
});
