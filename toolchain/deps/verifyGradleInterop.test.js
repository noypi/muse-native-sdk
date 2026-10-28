'use strict';
// T7a — Gradle interop verification: transitive native libs must be
// 16k-aligned and SBOM-listed (AC-048 / TC-048).
// Hermetic: node:test + node:assert/strict only. All inputs are inline
// literals; no filesystem, no network, no secrets, no live services.
// Red state: ./verifyGradleInterop.js does not exist yet, so every case below
// fails at require time (MODULE_NOT_FOUND) until the coder lands T7.
//
// Interface under test (plan verbatim):
//   verifyGradleInterop({direct:{coord, transitiveNativeLibs:{coord, elfAligned16k, sbomListed}[]}}): {ok, violations:string[]}
//
// ASSUMPTIONs (contract is silent here; coder must satisfy these):
//   C1. A missing `direct.coord` (null/undefined/empty) is itself a
//       violation naming the input as `direct`.
//   C2. A missing `transitiveNativeLibs` key (undefined) is treated as an
//       empty list; a non-array value throws TypeError.
//   C3. A non-object input (null/undefined/non-object) throws TypeError.
//   C4. Every violation names the offending coord; a transitive entry with a
//       missing coord is identified by index (`transitiveNativeLibs[<i>]`).
//   C5. Falsy `elfAligned16k`/`sbomListed` (false, null, undefined) fail;
//       only boolean true passes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { verifyGradleInterop } = require('./verifyGradleInterop.js');

function transitiveLib(overrides = {}) {
  return { coord: 'com.example:nativelib', elfAligned16k: true, sbomListed: true, ...overrides };
}

function directInput(libs = [transitiveLib()], coord = 'com.example:app') {
  return { direct: { coord, transitiveNativeLibs: libs } };
}

describe('verifyGradleInterop — happy path', () => {
  it('returns ok:true when every transitive native lib is aligned and SBOM-listed', () => {
    const out = verifyGradleInterop(directInput([
      transitiveLib({ coord: 'com.example:lib-a' }),
      transitiveLib({ coord: 'com.example:lib-b' }),
    ]));
    assert.equal(out.ok, true);
    assert.deepEqual(out.violations, []);
  });

  it('returns ok:true when there are no transitive native libs', () => {
    const out = verifyGradleInterop(directInput([]));
    assert.equal(out.ok, true);
    assert.deepEqual(out.violations, []);
  });

  it('treats a missing transitiveNativeLibs key as an empty list (C2)', () => {
    const out = verifyGradleInterop({ direct: { coord: 'com.example:app' } });
    assert.equal(out.ok, true);
    assert.deepEqual(out.violations, []);
  });
});

describe('verifyGradleInterop — rejection paths name the coord', () => {
  it('fails naming the coord when a transitive lib is not 16k-aligned', () => {
    const out = verifyGradleInterop(directInput([transitiveLib({ coord: 'com.example:unaligned', elfAligned16k: false })]));
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('com.example:unaligned')), `names coord, got: ${JSON.stringify(out.violations)}`);
  });

  it('fails naming the coord when a transitive lib is missing from the SBOM', () => {
    const out = verifyGradleInterop(directInput([transitiveLib({ coord: 'com.example:unlisted', sbomListed: false })]));
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('com.example:unlisted')), `names coord, got: ${JSON.stringify(out.violations)}`);
  });

  it('treats null/undefined flags as failures, not passes (C5)', () => {
    for (const [tag, lib] of [
      ['null-aligned', transitiveLib({ coord: 'com.example:null-a', elfAligned16k: null })],
      ['undef-listed', transitiveLib({ coord: 'com.example:undef-s', sbomListed: undefined })],
    ]) {
      const out = verifyGradleInterop(directInput([lib]));
      assert.equal(out.ok, false, `${tag} must fail`);
      assert.ok(out.violations.some((v) => v.includes(lib.coord)), `names coord for ${tag}, got: ${JSON.stringify(out.violations)}`);
    }
  });

  it('accumulates violations across transitive libs and reports every reason per lib', () => {
    const out = verifyGradleInterop(directInput([
      transitiveLib({ coord: 'com.example:both-bad', elfAligned16k: false, sbomListed: false }),
      transitiveLib({ coord: 'com.example:just-unaligned', elfAligned16k: false }),
      transitiveLib({ coord: 'com.example:clean' }),
    ]));
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('com.example:both-bad')));
    assert.ok(out.violations.some((v) => v.includes('com.example:just-unaligned')));
    assert.ok(!out.violations.some((v) => v.includes('com.example:clean')), `clean lib must not appear, got: ${JSON.stringify(out.violations)}`);
    assert.ok(out.violations.filter((v) => v.includes('com.example:both-bad')).length >= 2, `every reason reported, got: ${JSON.stringify(out.violations)}`);
  });

  it('fails naming `direct` when the direct coord is missing (C1)', () => {
    for (const coord of [null, undefined, '']) {
      const out = verifyGradleInterop({ direct: { coord, transitiveNativeLibs: [] } });
      assert.equal(out.ok, false, `coord ${JSON.stringify(coord)} must fail`);
      assert.ok(out.violations.some((v) => v.includes('direct')), `names direct, got: ${JSON.stringify(out.violations)}`);
    }
  });
});

describe('verifyGradleInterop — shape, boundary, and error paths', () => {
  it('violations is always a string array; ok is false iff violations is non-empty', () => {
    const good = verifyGradleInterop(directInput());
    assert.ok(Array.isArray(good.violations) && good.violations.every((v) => typeof v === 'string'));
    assert.equal(good.ok, good.violations.length === 0);
    const bad = verifyGradleInterop(directInput([transitiveLib({ coord: 'com.example:shape-bad', sbomListed: false })]));
    assert.ok(Array.isArray(bad.violations) && bad.violations.every((v) => typeof v === 'string'));
    assert.equal(bad.ok, bad.violations.length === 0);
  });

  it('throws TypeError on non-object input (C3)', () => {
    for (const bad of [null, undefined, 42, 'direct']) {
      assert.throws(() => verifyGradleInterop(bad), TypeError);
    }
  });

  it('throws TypeError when transitiveNativeLibs is a non-array value (C2)', () => {
    assert.throws(() => verifyGradleInterop({ direct: { coord: 'com.example:app', transitiveNativeLibs: 'lib' } }), TypeError);
  });

  it('identifies a coord-less transitive entry by index instead of crashing (C4)', () => {
    const out = verifyGradleInterop(directInput([{ elfAligned16k: false, sbomListed: false }]));
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('transitiveNativeLibs[0]')), `identifies by index, got: ${JSON.stringify(out.violations)}`);
  });

  it('names hostile coords without throwing', () => {
    const coord = 'com.evil:lib\nINJECT ../../../x';
    const out = verifyGradleInterop(directInput([transitiveLib({ coord, elfAligned16k: false })]));
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes(coord)));
  });

  it('violation messages expose only coord identity, never a secret', () => {
    const out = verifyGradleInterop(directInput([transitiveLib({ coord: 'com.example:leak-check', sbomListed: false })]));
    assert.equal(out.ok, false);
    for (const v of out.violations) {
      assert.ok(!/token|secret|key|password/i.test(v), `no secret material in: ${v}`);
    }
  });
});
