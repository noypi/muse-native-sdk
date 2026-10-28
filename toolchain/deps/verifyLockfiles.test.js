'use strict';
// T5 — lockfile verification across JS + Gradle ecosystems
// (AC-BUILD-046 / TC-046).
// Hermetic: node:test + node:assert/strict only. All inputs are inline
// literals; no filesystem, no network, no secrets, no live services.
// Red state: ./verifyLockfiles.js does not exist yet, so every case below
// fails at require time (MODULE_NOT_FOUND) until the coder lands T5.
//
// Interface under test (plan verbatim):
//   verifyLockfiles({js:[{name,version,integrity}], gradle:[{coord,version,checksum}]}): {ok, violations:string[]}
// Unpinned/missing integrity/checksum/mismatch fails naming dep.
//
// ASSUMPTIONs (contract is silent here; coder must satisfy these):
//   A1. "Pinned" means an exact version: no range prefixes (^ ~ >= <= > <
//       * x TikTok-style wildcards), no tags (latest/next), no empty value.
//   A2. "Mismatch" covers (a) a malformed integrity/checksum value that does
//       not match "<algo>-<base64>" shape, and (b) two entries for the same
//       dep/coord that pin different versions.
//   A3. Missing `js`/`gradle` keys (undefined) are treated as empty lists.
//   A4. A non-object input (null/undefined/non-object) throws TypeError.
//   A5. Every violation names the offending dep (js `name` / gradle `coord`).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { verifyLockfiles } = require('./verifyLockfiles.js');

function jsDep(overrides = {}) {
  return { name: 'left-pad', version: '1.3.0', integrity: 'sha512-abc123DEF456', ...overrides };
}

function gradleDep(overrides = {}) {
  return { coord: 'com.example:lib', version: '2.1.0', checksum: 'sha256-abc123DEF456', ...overrides };
}

function lockedInput(overrides = {}) {
  return { js: [jsDep()], gradle: [gradleDep()], ...overrides };
}

describe('verifyLockfiles — happy path', () => {
  it('returns ok:true with no violations for fully pinned + hashed lockfiles', () => {
    const out = verifyLockfiles(lockedInput());
    assert.equal(out.ok, true);
    assert.deepEqual(out.violations, []);
  });

  it('returns ok:true for empty js and gradle lists', () => {
    const out = verifyLockfiles({ js: [], gradle: [] });
    assert.equal(out.ok, true);
    assert.deepEqual(out.violations, []);
  });

  it('treats missing js/gradle keys as empty lists (A3)', () => {
    const out = verifyLockfiles({});
    assert.equal(out.ok, true);
    assert.deepEqual(out.violations, []);
  });

  it('accepts multiple pinned entries across both ecosystems', () => {
    const out = verifyLockfiles({
      js: [jsDep({ name: 'a', version: '1.0.0' }), jsDep({ name: 'b', version: '2.0.1' })],
      gradle: [gradleDep({ coord: 'com.example:a', version: '1.0.0' })],
    });
    assert.equal(out.ok, true);
    assert.deepEqual(out.violations, []);
  });
});

describe('verifyLockfiles — js failures name the dep', () => {
  it('fails naming the dep on missing js version', () => {
    const out = verifyLockfiles({ js: [jsDep({ name: 'no-ver', version: undefined })], gradle: [] });
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('no-ver')), `names dep, got: ${JSON.stringify(out.violations)}`);
  });

  it('fails naming the dep on unpinned js versions', () => {
    for (const v of ['*', '^1.0.0', '~1.0.0', '>=1.0.0', '>1.0.0', '<=2.0.0', '1.x', 'latest', 'next', '']) {
      const name = `unpinned-${v === '' ? 'empty' : v}`;
      const out = verifyLockfiles({ js: [jsDep({ name, version: v })], gradle: [] });
      assert.equal(out.ok, false, `version ${JSON.stringify(v)} must fail`);
      assert.ok(out.violations.some((x) => x.includes(name)), `names dep for ${v}, got: ${JSON.stringify(out.violations)}`);
    }
  });

  it('fails naming the dep on missing/empty js integrity', () => {
    for (const [tag, integrity] of [['missing', undefined], ['empty', '']]) {
      const name = `no-hash-${tag}`;
      const out = verifyLockfiles({ js: [jsDep({ name, integrity })], gradle: [] });
      assert.equal(out.ok, false);
      assert.ok(out.violations.some((v) => v.includes(name)), `names dep, got: ${JSON.stringify(out.violations)}`);
    }
  });

  it('fails naming the dep on malformed js integrity (A2a)', () => {
    const out = verifyLockfiles({ js: [jsDep({ name: 'bad-hash', integrity: 'not-a-hash' })], gradle: [] });
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('bad-hash')), `names dep, got: ${JSON.stringify(out.violations)}`);
  });

  it('fails naming the dep on same-name version conflict (A2b)', () => {
    const out = verifyLockfiles({
      js: [jsDep({ name: 'dup', version: '1.0.0' }), jsDep({ name: 'dup', version: '2.0.0' })],
      gradle: [],
    });
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('dup')), `names dep, got: ${JSON.stringify(out.violations)}`);
  });
});

describe('verifyLockfiles — gradle failures name the coord', () => {
  it('fails naming the coord on missing gradle version', () => {
    const out = verifyLockfiles({ js: [], gradle: [gradleDep({ coord: 'com.example:no-ver', version: '' })] });
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('com.example:no-ver')), `names coord, got: ${JSON.stringify(out.violations)}`);
  });

  it('fails naming the coord on unpinned gradle versions', () => {
    for (const v of ['+', '1.+', '[1.0,2.0)', 'latest.integration', '*']) {
      const coord = `com.example:unpinned-${v === '*' ? 'star' : v}`;
      const out = verifyLockfiles({ js: [], gradle: [gradleDep({ coord, version: v })] });
      assert.equal(out.ok, false, `version ${JSON.stringify(v)} must fail`);
      assert.ok(out.violations.some((x) => x.includes(coord)), `names coord for ${v}, got: ${JSON.stringify(out.violations)}`);
    }
  });

  it('fails naming the coord on missing/empty gradle checksum', () => {
    for (const [tag, checksum] of [['missing', undefined], ['empty', '']]) {
      const coord = `com.example:no-sum-${tag}`;
      const out = verifyLockfiles({ js: [], gradle: [gradleDep({ coord, checksum })] });
      assert.equal(out.ok, false);
      assert.ok(out.violations.some((v) => v.includes(coord)), `names coord, got: ${JSON.stringify(out.violations)}`);
    }
  });

  it('fails naming the coord on malformed gradle checksum (A2a)', () => {
    const out = verifyLockfiles({ js: [], gradle: [gradleDep({ coord: 'com.example:bad-sum', checksum: 'zzz' })] });
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('com.example:bad-sum')), `names coord, got: ${JSON.stringify(out.violations)}`);
  });

  it('fails naming the coord on same-coord version conflict (A2b)', () => {
    const out = verifyLockfiles({
      js: [],
      gradle: [
        gradleDep({ coord: 'com.example:dup', version: '1.0.0' }),
        gradleDep({ coord: 'com.example:dup', version: '1.0.1' }),
      ],
    });
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('com.example:dup')), `names coord, got: ${JSON.stringify(out.violations)}`);
  });
});

describe('verifyLockfiles — aggregation, shape, and error paths', () => {
  it('accumulates one violation per offending entry across ecosystems', () => {
    const out = verifyLockfiles({
      js: [jsDep({ name: 'bad-js', integrity: '' }), jsDep()],
      gradle: [gradleDep({ coord: 'com.example:bad-gradle', checksum: '' }), gradleDep()],
    });
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes('bad-js')));
    assert.ok(out.violations.some((v) => v.includes('com.example:bad-gradle')));
    assert.equal(out.violations.length, 2);
  });

  it('violations is always a string array; ok is false iff violations is non-empty', () => {
    const good = verifyLockfiles(lockedInput());
    assert.ok(Array.isArray(good.violations) && good.violations.every((v) => typeof v === 'string'));
    assert.equal(good.ok, good.violations.length === 0);
    const bad = verifyLockfiles({ js: [jsDep({ name: 'x-bad', version: '*' })], gradle: [] });
    assert.ok(Array.isArray(bad.violations) && bad.violations.every((v) => typeof v === 'string'));
    assert.equal(bad.ok, bad.violations.length === 0);
  });

  it('throws TypeError on non-object input (A4)', () => {
    for (const bad of [null, undefined, 42, 'lock']) {
      assert.throws(() => verifyLockfiles(bad), TypeError);
    }
  });

  it('names hostile dep names without throwing (injection-shaped name)', () => {
    const name = '../../etc/passwd\nDROP TABLE x';
    const out = verifyLockfiles({ js: [jsDep({ name, version: '*' })], gradle: [] });
    assert.equal(out.ok, false);
    assert.ok(out.violations.some((v) => v.includes(name)));
  });

  it('violation messages expose only the dep identity, never a secret', () => {
    const out = verifyLockfiles({ js: [jsDep({ name: 'leak-check', version: '*' })], gradle: [] });
    assert.equal(out.ok, false);
    for (const v of out.violations) {
      assert.ok(!/token|secret|key|password/i.test(v), `no secret material in: ${v}`);
    }
  });
});
