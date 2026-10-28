'use strict';
// T6 — native addon verification: per-ABI prebuilts + 16k ELF alignment
// (AC-047 + AC-050 / TC-047 + TC-050).
// Hermetic: node:test + node:assert/strict only. All inputs are inline
// literals; no filesystem, no network, no secrets, no live services.
// Red state: ./verifyNativeAddons.js does not exist yet, so every case below
// fails at require time (MODULE_NOT_FOUND) until the coder lands T6.
//
// Interface under test (plan verbatim):
//   verifyNativeAddons(addons:{name, abi:string|null, prebuiltSoPerAbi:boolean, elfAligned16k:boolean}[]): {ok, violations:string[]}
// Rejects when prebuiltSoPerAbi false, elfAligned16k false, or abi missing.
//
// ASSUMPTIONs (contract is silent here; coder must satisfy these):
//   B1. `abi` counts as missing when it is null, undefined, empty, or a
//       non-string value (whitespace-only counts as missing).
//   B2. A non-array input (null/undefined/non-array) throws TypeError.
//   B3. Every violation names the offending addon by `name`; an entry with a
//       missing name is identified by its index (`addons[<i>]`).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { verifyNativeAddons } = require('./verifyNativeAddons.js');

function addon(overrides = {}) {
  return { name: 'ffmpeg-addon', abi: 'arm64-v8a', prebuiltSoPerAbi: true, elfAligned16k: true, ...overrides };
}

describe('verifyNativeAddons — happy path', () => {
  it('returns ok:true with no violations when every addon is per-ABI, aligned, and ABI-tagged', () => {
    const out = verifyNativeAddons([addon(), addon({ name: 'other', abi: 'x86_64' })]);
    assert.equal(out.ok, true);
    assert.deepEqual(out.violations, []);
  });

  it('returns ok:true for an empty addon list', () => {
    const out = verifyNativeAddons([]);
    assert.equal(out.ok, true);
    assert.deepEqual(out.violations, []);
  });
});

describe('verifyNativeAddons — rejection paths name the addon', () => {
  it('rejects naming the addon when prebuiltSoPerAbi is false', () => {
    const out = verifyNativeAddons([addon({ name: 'no-per-abi', prebuiltSoPerAbi: false })]);
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('no-per-abi')), `names addon, got: ${JSON.stringify(out.violations)}`);
  });

  it('rejects naming the addon when elfAligned16k is false', () => {
    const out = verifyNativeAddons([addon({ name: 'unaligned', elfAligned16k: false })]);
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('unaligned')), `names addon, got: ${JSON.stringify(out.violations)}`);
  });

  it('rejects naming the addon when abi is missing (null/undefined/empty/blank)', () => {
    for (const [tag, abi] of [['null', null], ['undefined', undefined], ['empty', ''], ['blank', '   ']]) {
      const out = verifyNativeAddons([addon({ name: `no-abi-${tag}`, abi })]);
      assert.equal(out.ok, false, `abi ${tag} must fail`);
      assert.ok(out.violations.some((v) => v.includes(`no-abi-${tag}`)), `names addon for abi=${tag}, got: ${JSON.stringify(out.violations)}`);
    }
  });

  it('rejects when abi is a non-string value (B1)', () => {
    const out = verifyNativeAddons([addon({ name: 'numeric-abi', abi: 64 })]);
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('numeric-abi')), `names addon, got: ${JSON.stringify(out.violations)}`);
  });

  it('accumulates violations across addons and across failure modes', () => {
    const out = verifyNativeAddons([
      addon({ name: 'bad-prebuilt', prebuiltSoPerAbi: false }),
      addon({ name: 'bad-aligned', elfAligned16k: false }),
      addon({ name: 'bad-abi', abi: null }),
      addon({ name: 'good' }),
    ]);
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('bad-prebuilt')));
    assert.ok(out.violations.some((v) => v.includes('bad-aligned')));
    assert.ok(out.violations.some((v) => v.includes('bad-abi')));
    assert.ok(!out.violations.some((v) => v.includes('good')), `clean addon must not appear, got: ${JSON.stringify(out.violations)}`);
  });

  it('reports every applicable reason for a fully non-compliant addon', () => {
    const out = verifyNativeAddons([addon({ name: 'triple-bad', abi: null, prebuiltSoPerAbi: false, elfAligned16k: false })]);
    assert.equal(out.ok, false);
    assert.ok(out.violations.length >= 3, `one violation per failure mode, got: ${JSON.stringify(out.violations)}`);
    assert.ok(out.violations.every((v) => v.includes('triple-bad')));
  });
});

describe('verifyNativeAddons — shape, boundary, and error paths', () => {
  it('violations is always a string array; ok is false iff violations is non-empty', () => {
    const good = verifyNativeAddons([addon()]);
    assert.ok(Array.isArray(good.violations) && good.violations.every((v) => typeof v === 'string'));
    assert.equal(good.ok, good.violations.length === 0);
    const bad = verifyNativeAddons([addon({ name: 'shape-bad', elfAligned16k: false })]);
    assert.ok(Array.isArray(bad.violations) && bad.violations.every((v) => typeof v === 'string'));
    assert.equal(bad.ok, bad.violations.length === 0);
  });

  it('throws TypeError on non-array input (B2)', () => {
    for (const bad of [null, undefined, {}, 'addon']) {
      assert.throws(() => verifyNativeAddons(bad), TypeError);
    }
  });

  it('identifies a nameless entry by index instead of crashing (B3)', () => {
    const out = verifyNativeAddons([{ abi: null, prebuiltSoPerAbi: false, elfAligned16k: false }]);
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('addons[0]')), `identifies by index, got: ${JSON.stringify(out.violations)}`);
  });

  it('names hostile addon names without throwing', () => {
    const name = '../evil\nINJECT';
    const out = verifyNativeAddons([addon({ name, prebuiltSoPerAbi: false })]);
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes(name)));
  });

  it('violation messages expose only the addon identity, never a secret', () => {
    const out = verifyNativeAddons([addon({ name: 'leak-check', elfAligned16k: false })]);
    assert.equal(out.ok, false);
    for (const v of out.violations) {
      assert.ok(!/token|secret|key|password/i.test(v), `no secret material in: ${v}`);
    }
  });
});
