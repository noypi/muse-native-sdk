'use strict';
// T3 — lazy-module / first-frame gate (NFR-BUILD-012 / AC-BUILD-038).
// Hermetic: node:test + node:assert/strict + stdlib only. Pure manifest
// comparison — no engine import, no filesystem, no network, no secrets,
// no live services.
// Red state: ./lazyModuleGate.js does not exist yet, so every case below
// fails (MODULE_NOT_FOUND) until T3 lands.
//
// Interface under test (plan verbatim):
//   checkLazyModules({ startupModules: string[], lazyManifest: Array<{module: string, loadedAt: 'startup'|'lazy'}> }): { ok: boolean, violations: string[] } — any `lazyManifest` module marked `startup`-loaded that is not in the startup allowlist yields a violation naming the module.
//
// ASSUMPTION (allowlist semantics): a manifest entry with
// `loadedAt:'startup'` is a violation only when its `module` is NOT in
// `startupModules`; an allowlisted startup load is legal. Entries with
// `loadedAt:'lazy'` never violate.
// ASSUMPTION (violations shape): `violations` lists every violating module
// name in manifest order with no duplicates; `ok` is true iff `violations`
// is empty.
// ASSUMPTION (input validation): non-array `startupModules`/`lazyManifest`,
// or manifest entries without a string `module` / valid `loadedAt`, throw a
// `TypeError` (fail-closed on malformed gate input). An unknown `loadedAt`
// value is treated as malformed input and throws rather than passing.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./lazyModuleGate.js');
}

describe('T3 — checkLazyModules passing manifests', () => {
  it('all-lazy manifest passes with empty violations', () => {
    const { checkLazyModules } = loadImpl();
    const result = checkLazyModules({
      startupModules: ['app-shell'],
      lazyManifest: [
        { module: 'settings-screen', loadedAt: 'lazy' },
        { module: 'profile-screen', loadedAt: 'lazy' },
      ],
    });
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });

  it('empty manifest passes', () => {
    const { checkLazyModules } = loadImpl();
    const result = checkLazyModules({ startupModules: [], lazyManifest: [] });
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });

  it('allowlisted startup load is not a violation', () => {
    const { checkLazyModules } = loadImpl();
    const result = checkLazyModules({
      startupModules: ['app-shell', 'home-screen'],
      lazyManifest: [{ module: 'home-screen', loadedAt: 'startup' }],
    });
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T3 — checkLazyModules first-frame violations (AC-BUILD-038)', () => {
  it('one startup-loaded lazy module fails naming the module', () => {
    const { checkLazyModules } = loadImpl();
    const result = checkLazyModules({
      startupModules: ['app-shell'],
      lazyManifest: [
        { module: 'settings-screen', loadedAt: 'lazy' },
        { module: 'heavy-charts', loadedAt: 'startup' },
      ],
    });
    assert.equal(result.ok, false);
    assert.deepEqual(result.violations, ['heavy-charts']);
  });

  it('multiple violations are all named in manifest order', () => {
    const { checkLazyModules } = loadImpl();
    const result = checkLazyModules({
      startupModules: [],
      lazyManifest: [
        { module: 'heavy-a', loadedAt: 'startup' },
        { module: 'lazy-ok', loadedAt: 'lazy' },
        { module: 'heavy-b', loadedAt: 'startup' },
      ],
    });
    assert.equal(result.ok, false);
    assert.deepEqual(result.violations, ['heavy-a', 'heavy-b']);
  });

  it('duplicate violating entries collapse to one violation per module', () => {
    const { checkLazyModules } = loadImpl();
    const result = checkLazyModules({
      startupModules: [],
      lazyManifest: [
        { module: 'heavy-a', loadedAt: 'startup' },
        { module: 'heavy-a', loadedAt: 'startup' },
      ],
    });
    assert.equal(result.ok, false);
    assert.deepEqual(result.violations, ['heavy-a']);
  });
});

describe('T3 — checkLazyModules input validation', () => {
  it('non-array lazyManifest throws TypeError', () => {
    const { checkLazyModules } = loadImpl();
    assert.throws(
      () => checkLazyModules({ startupModules: [], lazyManifest: 'nope' }),
      TypeError
    );
  });

  it('non-array startupModules throws TypeError', () => {
    const { checkLazyModules } = loadImpl();
    assert.throws(
      () => checkLazyModules({ startupModules: 'app-shell', lazyManifest: [] }),
      TypeError
    );
  });

  it('entry missing module name throws TypeError', () => {
    const { checkLazyModules } = loadImpl();
    assert.throws(
      () => checkLazyModules({ startupModules: [], lazyManifest: [{ loadedAt: 'lazy' }] }),
      TypeError
    );
  });

  it('unknown loadedAt value throws TypeError instead of passing', () => {
    const { checkLazyModules } = loadImpl();
    assert.throws(
      () => checkLazyModules({
        startupModules: [],
        lazyManifest: [{ module: 'sneaky', loadedAt: 'eventually' }],
      }),
      TypeError
    );
  });

  it('injection-style module names are treated as opaque strings, never executed', () => {
    const { checkLazyModules } = loadImpl();
    const evil = '__proto__/polluted';
    const result = checkLazyModules({
      startupModules: [],
      lazyManifest: [{ module: evil, loadedAt: 'startup' }],
    });
    assert.equal(result.ok, false);
    assert.deepEqual(result.violations, [evil]);
    assert.equal(Object.prototype.polluted, undefined);
  });
});
