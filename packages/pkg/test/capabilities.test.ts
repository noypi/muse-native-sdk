'use strict';
// TC-PKG-012 (AC-PKG-012) — Batch 3 (T6 contract).
// Hermetic: node:test + node:assert/strict only. Inline manifests/grant lists;
// no network, no secrets, no live services, no fs fixtures, no sandbox policy engine.
// Binds ONLY to the pinned T6 `Interface:` line, never to an implementation.
// Resolution: `../lib/capabilities.js` once it lands, else the `../index.js`
// shim, so every behavior case below fails until T6 lands.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let capsMod = {};
try {
  capsMod = require('../lib/capabilities.js');
} catch {
  try {
    capsMod = require('../index.js');
  } catch {
    capsMod = {};
  }
}
const { checkCapabilities } = capsMod;

function validManifest(overrides = {}) {
  return {
    name: 'acme-engine',
    version: '1.2.0',
    museApi: '1',
    entry: { form: 'prebuilt', main: 'src/index.js', types: 'src/index.d.ts' },
    abis: ['arm64-v8a'],
    engineAbi: 'deadbeef',
    exports: [],
    capabilities: [],
    ...overrides,
  };
}

function assertCapabilityError(fn, expectedCapability, expectedPolicy) {
  let err;
  try {
    fn();
  } catch (e) {
    err = e;
  }
  assert.ok(err, 'must throw a PkgError');
  assert.equal(err.code, 'E_CAPABILITY', `code must be E_CAPABILITY, got: ${err && err.code}`);
  assert.equal(typeof err.path, 'string', 'PkgError must carry a string path');
  assert.equal(typeof err.expected, 'string', 'PkgError must carry a string expected');
  assert.ok(
    String(err.path).includes(expectedCapability),
    `error path must name the capability "${expectedCapability}", got: ${err.path}`,
  );
  assert.ok(
    String(err.expected).includes(expectedPolicy),
    `error expected must name the policy "${expectedPolicy}", got: ${err.expected}`,
  );
  return err;
}

// ---------------------------------------------------------------------------
// checkCapabilities — T6 contract:
// `checkCapabilities(manifest: ValidatedManifest, granted: string[],
//  policyId: string): void`
// throws `PkgError{ code: "E_CAPABILITY", path: string, expected: string }`
// ---------------------------------------------------------------------------
describe('TC-PKG-012 — capability policy (AC-PKG-012)', () => {
  it('passes when every manifest capability is granted', () => {
    const m = validManifest({ capabilities: ['net:http', 'fs:read'] });
    assert.doesNotThrow(() => checkCapabilities(m, ['net:http', 'fs:read'], 'policy-1'));
  });

  it('passes when granted is a superset of manifest capabilities', () => {
    const m = validManifest({ capabilities: ['net:http'] });
    assert.doesNotThrow(() => checkCapabilities(m, ['net:http', 'fs:read', 'timer'], 'policy-1'));
  });

  it('refuses a capability outside the app grant naming capability + policy', () => {
    const m = validManifest({ capabilities: ['net:http'] });
    assertCapabilityError(() => checkCapabilities(m, [], 'policy-1'), 'net:http', 'policy-1');
  });

  it('names the first ungranted capability when several are missing', () => {
    const m = validManifest({ capabilities: ['net:http', 'fs:read'] });
    assertCapabilityError(() => checkCapabilities(m, ['fs:read'], 'policy-7'), 'net:http', 'policy-7');
  });

  it('passes a capability-free manifest with an empty grant', () => {
    assert.doesNotThrow(() => checkCapabilities(validManifest(), [], 'policy-1'));
  });

  it('requires exact match: net:http grant does not cover net:https', () => {
    const m = validManifest({ capabilities: ['net:https'] });
    assertCapabilityError(() => checkCapabilities(m, ['net:http'], 'policy-1'), 'net:https', 'policy-1');
  });

  it('refuses the ungrantable gpu-surface capability even when listed as granted (ASSUMPTION: MVP never grants graphics)', () => {
    const m = validManifest({ capabilities: ['gpu-surface'] });
    assertCapabilityError(
      () => checkCapabilities(m, ['gpu-surface'], 'policy-1'),
      'gpu-surface',
      'policy-1',
    );
  });

  it('refuses the ungrantable window capability even when listed as granted (ASSUMPTION: MVP never grants graphics)', () => {
    const m = validManifest({ capabilities: ['window'] });
    assertCapabilityError(() => checkCapabilities(m, ['window'], 'policy-1'), 'window', 'policy-1');
  });

  it('treats "*" literally: it does not wildcard-grant (ASSUMPTION: exact-match only)', () => {
    const m = validManifest({ capabilities: ['net:http'] });
    assertCapabilityError(() => checkCapabilities(m, ['*'], 'policy-1'), 'net:http', 'policy-1');
  });

  it('throws E_CAPABILITY naming the policy when policyId is missing (ASSUMPTION)', () => {
    const m = validManifest({ capabilities: ['net:http'] });
    let err;
    try {
      checkCapabilities(m, ['net:http'], '');
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'must throw a PkgError');
    assert.equal(err.code, 'E_CAPABILITY');
  });

  it('treats capability strings opaquely: injection-looking names only match exactly (security)', () => {
    const evil = 'net;drop-policy';
    const m = validManifest({ capabilities: [evil] });
    assertCapabilityError(() => checkCapabilities(m, ['net'], 'policy-1'), evil, 'policy-1');
    assert.doesNotThrow(() => checkCapabilities(m, [evil], 'policy-1'));
  });
});
