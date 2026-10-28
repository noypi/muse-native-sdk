'use strict';
// T8 — Generated (not hand-written) keep rules cover JNI/codegen/FlatBuffers/JSI/Hermes
// (NFR-BUILD-007 / AC-BUILD-012).
// Hermetic: node:test + node:assert/strict (+ node:fs/node:path only to read the
// tester-authored generated-rules.pro fixture beside this file). Pure in-memory
// inputs otherwise; no network, no secrets, no live services.
// Red state: ./keepRules.js does not exist yet, so every validator case below
// fails (MODULE_NOT_FOUND via loadImpl) until T8 lands.
//
// Interface under test (plan verbatim):
//   keepRulesCover(generator: string, rules: string): {ok:boolean, missing:string[]}
// ok:false when generator is hand/manual or any required token
// (native-method, jni-callback, flatbuffer, jsi-exposed, hermes-reflection) absent.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

function loadImpl() {
  return require('./keepRules.js');
}

const TOKENS = ['native-method', 'jni-callback', 'flatbuffer', 'jsi-exposed', 'hermes-reflection'];

function goodRules() {
  return TOKENS.map((t) => `# keep ${t}\n-keep class * { *; } // ${t}`).join('\n');
}

describe('T8 — contract shape (AC-BUILD-012)', () => {
  it('returns { ok: boolean, missing: string[] } for generator-stamped full rules', () => {
    const { keepRulesCover } = loadImpl();
    const result = keepRulesCover('r8-keepgen/1.0', goodRules());
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.missing));
    assert.equal(result.ok, true);
    assert.deepEqual(result.missing, []);
  });
});

describe('T8 — generator-stamped rules with all five tokens pass (AC-BUILD-012)', () => {
  it('generated generator + all tokens passes', () => {
    const { keepRulesCover } = loadImpl();
    const result = keepRulesCover('generated-r8-keepgen', goodRules());
    assert.equal(result.ok, true);
    assert.deepEqual(result.missing, []);
  });

  it('emitted fixture generated-rules.pro passes with a generator stamp', () => {
    const { keepRulesCover } = loadImpl();
    const rules = fs.readFileSync(path.join(__dirname, 'generated-rules.pro'), 'utf8');
    const result = keepRulesCover('r8-keepgen/1.0', rules);
    assert.equal(result.ok, true);
    assert.deepEqual(result.missing, []);
  });

  it('fixture contains a generator provenance header (not hand-written)', () => {
    const text = fs.readFileSync(path.join(__dirname, 'generated-rules.pro'), 'utf8');
    assert.match(text.toLowerCase(), /generat/);
  });
});

describe('T8 — hand-written sources fail (AC-BUILD-012)', () => {
  it("generator 'hand' fails naming no missing token beyond provenance", () => {
    const { keepRulesCover } = loadImpl();
    const result = keepRulesCover('hand', goodRules());
    assert.equal(result.ok, false);
  });

  it("generator 'manual' fails", () => {
    const { keepRulesCover } = loadImpl();
    const result = keepRulesCover('manual', goodRules());
    assert.equal(result.ok, false);
  });

  it('generator match is case-sensitive (HAND is not an allowed stamp)', () => {
    // ASSUMPTION: generator allowlist is exact lowercase match; variants fail.
    const { keepRulesCover } = loadImpl();
    const result = keepRulesCover('HAND', goodRules());
    assert.equal(result.ok, false);
  });

  it('empty generator fails closed', () => {
    const { keepRulesCover } = loadImpl();
    const result = keepRulesCover('', goodRules());
    assert.equal(result.ok, false);
  });
});

describe('T8 — missing tokens fail naming each gap (AC-BUILD-012)', () => {
  for (const token of TOKENS) {
    it(`absent '${token}' fails with missing naming it`, () => {
      const { keepRulesCover } = loadImpl();
      const rules = TOKENS.filter((t) => t !== token)
        .map((t) => `// ${t}`)
        .join('\n');
      const result = keepRulesCover('r8-keepgen/1.0', rules);
      assert.equal(result.ok, false);
      assert.ok(result.missing.includes(token), `missing should name ${token}`);
    });
  }

  it('empty rules fail naming all five tokens', () => {
    const { keepRulesCover } = loadImpl();
    const result = keepRulesCover('r8-keepgen/1.0', '');
    assert.equal(result.ok, false);
    assert.deepEqual([...result.missing].sort(), [...TOKENS].sort());
  });

  it('multiple gaps are all named', () => {
    const { keepRulesCover } = loadImpl();
    const result = keepRulesCover('r8-keepgen/1.0', '// native-method only\nnative-method');
    assert.equal(result.ok, false);
    assert.ok(result.missing.length >= 4);
    assert.ok(result.missing.includes('jni-callback'));
  });
});
