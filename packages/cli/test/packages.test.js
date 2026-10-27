'use strict';
// TC-TSX-021 — Batch 1 (T1, T2, T3) of FR-TSX-008.
// Hermetic: node:test + node:assert (+ node:fs / node:path for static reads) only.
// No network, no secrets, no live services.
// Append-only: Batch 2 (T4, T5) adds NEW describe blocks below the marker at
// the end of this file. Do not edit the Batch-1 blocks above the marker.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const PKGS = ['core', 'native', 'jsx', 'cli'];

function readJson(p) {
  return JSON.parse(fs.readFileSync(p, 'utf8'));
}

function walkJsFiles(dir) {
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...walkJsFiles(full));
    else if (entry.isFile() && entry.name.endsWith('.js')) out.push(full);
  }
  return out;
}

// ---------------------------------------------------------------------------
// T1 — Root workspace + hermetic test harness.
// Interface: `npm test` → runs all workspace suites, exit 0 on green /
// non-zero on any failure; each package exposes `npm test` →
// `node --test test/`. No dependencies in any `package.json`.
// ---------------------------------------------------------------------------
describe('TC-TSX-021 Batch 1 — T1 workspace + harness', () => {
  it('root package.json declares npm workspaces packages/*', () => {
    const pkg = readJson(path.join(ROOT, 'package.json'));
    const workspaces = Array.isArray(pkg.workspaces) ? pkg.workspaces : pkg.workspaces.packages;
    assert.ok(
      Array.isArray(workspaces) && workspaces.includes('packages/*'),
      'root package.json must declare workspaces ["packages/*"]',
    );
  });

  it('root `npm test` runs all workspace suites (exit 0 green / non-zero red)', () => {
    const pkg = readJson(path.join(ROOT, 'package.json'));
    assert.ok(pkg.scripts && typeof pkg.scripts.test === 'string', 'root needs a `test` script');
    assert.match(pkg.scripts.test, /workspaces|packages/i, 'root `test` script must iterate workspaces');
  });

  for (const name of PKGS) {
    it(`@muse/${name} exposes \`npm test\` → \`node --test test/\``, () => {
      const pkg = readJson(path.join(ROOT, 'packages', name, 'package.json'));
      assert.ok(pkg.scripts && typeof pkg.scripts.test === 'string', `@muse/${name} needs a \`test\` script`);
      assert.match(pkg.scripts.test, /node --test test\//, `@muse/${name} \`test\` must run \`node --test test/\``);
    });
  }

  it('every package.json (root + four packages) declares zero dependencies', () => {
    for (const p of [path.join(ROOT, 'package.json'), ...PKGS.map((n) => path.join(ROOT, 'packages', n, 'package.json'))]) {
      const pkg = readJson(p);
      assert.ok(
        !pkg.dependencies || Object.keys(pkg.dependencies).length === 0,
        `${p} must have no dependencies (zero-dep hermetic)`,
      );
    }
  });
});

// ---------------------------------------------------------------------------
// T2 — @muse/core skeleton.
// Interface: `require('@muse/core')` → `{ state, computed, effect, batch,
// onMount, onUnmount }`; each is a function with the documented arity; stub
// bodies throw `Error('not implemented: FR-MCORE lane')` except
// `state`/`computed` which return a minimal `{ value }` handle so
// import-shape checks pass. No other named exports.
// ---------------------------------------------------------------------------
describe('TC-TSX-021 Batch 1 — T2 @muse/core surface', () => {
  const CORE_INDEX = path.join(ROOT, 'packages', 'core', 'index.js');
  const EXPECTED = ['state', 'computed', 'effect', 'batch', 'onMount', 'onUnmount'];

  it('resolves to exactly the six named function exports', () => {
    const mod = require(CORE_INDEX);
    assert.deepEqual(Object.keys(mod).sort(), [...EXPECTED].sort(), 'exact export list, no more, no fewer');
    for (const name of EXPECTED) {
      assert.equal(typeof mod[name], 'function', `${name} must be a function`);
    }
  });

  it('state/computed return minimal `{ value }` handles', () => {
    const mod = require(CORE_INDEX);
    const s = mod.state(7);
    assert.ok(s && typeof s === 'object' && 'value' in s, 'state() must return a `{ value }` handle');
    const c = mod.computed(() => 42);
    assert.ok(c && typeof c === 'object' && 'value' in c, 'computed() must return a `{ value }` handle');
  });

  it('effect/batch/onMount/onUnmount throw not-implemented for the FR-MCORE lane', () => {
    const mod = require(CORE_INDEX);
    for (const name of ['effect', 'batch', 'onMount', 'onUnmount']) {
      assert.throws(() => mod[name](() => {}), /not implemented: FR-MCORE lane/, `${name} must throw the lane stub error`);
    }
  });
});

// ---------------------------------------------------------------------------
// T3 — @muse/jsx types-only.
// Interface: `index.d.ts` declares `JSX.IntrinsicElements` incl. the five
// `c-*` built-ins (`c-if`, `c-else-if`, `c-else`, `c-for`, `c-show`) with
// their prop types (`test`, `each`+`key`, `when`); package ships zero `.js`
// files. Importing `@muse/jsx` at runtime fails closed (types only).
// ---------------------------------------------------------------------------
describe('TC-TSX-021 Batch 1 — T3 @muse/jsx types-only', () => {
  const JSX_DIR = path.join(ROOT, 'packages', 'jsx');
  const DTS = path.join(JSX_DIR, 'index.d.ts');
  const BUILTINS = ['c-if', 'c-else-if', 'c-else', 'c-for', 'c-show'];

  it('index.d.ts declares JSX.IntrinsicElements with all five c-* built-ins', () => {
    const text = fs.readFileSync(DTS, 'utf8');
    assert.match(text, /IntrinsicElements/, 'must declare JSX.IntrinsicElements');
    for (const tag of BUILTINS) {
      assert.ok(text.includes(`'${tag}'`) || text.includes(`"${tag}"`), `must declare built-in ${tag}`);
    }
  });

  it('built-ins carry the spec prop types (test / each+key / when)', () => {
    const text = fs.readFileSync(DTS, 'utf8');
    assert.match(text, /\btest\s*[?:]\s*boolean/, 'c-if/c-else-if must type `test` as boolean');
    assert.match(text, /\beach\s*[?:]/, 'c-for must declare `each`');
    assert.match(text, /\bkey\s*[?:]/, 'c-for must declare `key`');
    assert.match(text, /\bwhen\s*[?:]\s*boolean/, 'c-show must type `when` as boolean');
  });

  it('c-else takes no props', () => {
    const lines = fs.readFileSync(DTS, 'utf8').split('\n');
    const decl = lines.filter((l) => l.includes('c-else') && !l.includes('c-else-if'));
    assert.ok(decl.length > 0, 'c-else must be declared');
    for (const line of decl) {
      assert.ok(!/\b(test|each|key|when)\b/.test(line), `c-else must not take props, got: ${line.trim()}`);
    }
  });

  it('the package ships zero .js files', () => {
    assert.deepEqual(walkJsFiles(JSX_DIR), [], 'packages/jsx/ must contain no .js files (types only)');
  });

  it('runtime import of @muse/jsx fails closed', () => {
    assert.throws(() => require(JSX_DIR), /Cannot find module/, 'requiring @muse/jsx at runtime must fail');
  });
});

// === Batch 2 (T4 native skeleton, T5 cli wiring) appends NEW describe blocks below. ===
