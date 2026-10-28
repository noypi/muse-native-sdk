'use strict';
// T2 — Registry publish alignment gate for native addons (AC-BUILD-017).
// Hermetic: node:test + node:assert/strict only (pure-data gate, no fs, no
// network, no secrets, no live services).
// Red state: ./registryAlignmentGate.js does not exist yet, so every case
// below fails (MODULE_NOT_FOUND) until the coder lands the gate.
//
// Interface under test (plan verbatim):
//   checkRegistryAddon(addon: { name: string, abis: string[], segmentsAligned: boolean }): { ok: boolean, reasons: string[] } — `ok:false` rejects publish, reasons name the addon and ABI.
//
// Q-BUILD-001 boundary (plan Detail): third-party prebuilts outside engine
// control are rejected with a vendor-action message, not silently stranded —
// the gate source records this boundary in a code comment naming Q-BUILD-001.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

function loadImpl() {
  return require('./registryAlignmentGate.js');
}

describe('checkRegistryAddon', () => {
  it('aligned addon is accepted', () => {
    const { checkRegistryAddon } = loadImpl();
    assert.deepEqual(
      checkRegistryAddon({ name: 'muse-addon', abis: ['arm64-v8a'], segmentsAligned: true }),
      { ok: true, reasons: [] }
    );
  });

  it('unaligned addon is rejected with reasons naming addon and ABI', () => {
    const { checkRegistryAddon } = loadImpl();
    const result = checkRegistryAddon({
      name: 'muse-addon',
      abis: ['arm64-v8a', 'armeabi-v7a'],
      segmentsAligned: false,
    });
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length > 0, 'rejection must carry reasons');
    assert.ok(
      result.reasons.some((r) => r.includes('muse-addon')),
      `reasons must name the addon, got: ${JSON.stringify(result.reasons)}`
    );
    for (const abi of ['arm64-v8a', 'armeabi-v7a']) {
      assert.ok(
        result.reasons.some((r) => r.includes(abi)),
        `reasons must name ABI ${abi}, got: ${JSON.stringify(result.reasons)}`
      );
    }
  });

  it('rejection carries a vendor-action message for third-party prebuilts', () => {
    const { checkRegistryAddon } = loadImpl();
    const result = checkRegistryAddon({
      name: 'vendor-prebuilt',
      abis: ['arm64-v8a'],
      segmentsAligned: false,
    });
    assert.equal(result.ok, false);
    const joined = result.reasons.join('\n').toLowerCase();
    assert.ok(
      joined.includes('vendor'),
      `rejection must direct action at the vendor, got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('gate source records the Q-BUILD-001 boundary in a code comment', () => {
    const src = fs.readFileSync(path.join(__dirname, 'registryAlignmentGate.js'), 'utf8');
    assert.ok(
      src.includes('Q-BUILD-001'),
      'gate source must cite Q-BUILD-001 in the third-party-prebuilt boundary comment'
    );
    assert.ok(
      src.toLowerCase().includes('vendor'),
      'gate source boundary comment must mention the vendor strand'
    );
  });

  it('empty addon name is rejected, never accepted silently', () => {
    // ASSUMPTION: the plan is silent on malformed input; the most defensible
    // behavior is to reject an unnamed addon rather than publish it.
    const { checkRegistryAddon } = loadImpl();
    const result = checkRegistryAddon({ name: '', abis: ['arm64-v8a'], segmentsAligned: true });
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length > 0, 'rejection must carry reasons');
  });

  it('malicious addon name is treated as an opaque string (no fs access, still named)', () => {
    const { checkRegistryAddon } = loadImpl();
    const evil = '../../etc/malicious';
    const result = checkRegistryAddon({ name: evil, abis: ['arm64-v8a'], segmentsAligned: false });
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => r.includes(evil)),
      `reasons must name the addon opaquely, got: ${JSON.stringify(result.reasons)}`
    );
  });
});
