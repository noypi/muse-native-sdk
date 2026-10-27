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
  const EXPECTED = ['batch', 'computed', 'effect', 'onMount', 'onUnmount', 'spring', 'state'];

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

const { spawnSync } = require('node:child_process');

// ---------------------------------------------------------------------------
// T4 — @muse/native skeleton + manifest.
// Interface: `require('@muse/native')` → `{ primitives: [...9 names...],
// components: {...stub map...}, manifest }`; `muse.manifest.json` → `{
// name: '@muse/native', components: [{ name, props }] }` with at least the
// nine primitives listed. Stub components are functions returning
// `{ type: name, props }` descriptors (no rendering).
// ---------------------------------------------------------------------------
describe('TC-TSX-021 Batch 2 — T4 @muse/native skeleton + manifest', () => {
  const NATIVE_DIR = path.join(ROOT, 'packages', 'native');
  const NATIVE_INDEX = path.join(NATIVE_DIR, 'index.js');
  const MANIFEST = path.join(NATIVE_DIR, 'muse.manifest.json');
  const DTS = path.join(NATIVE_DIR, 'index.d.ts');

  it('muse.manifest.json parses with name @muse/native and >=9 component entries', () => {
    const raw = fs.readFileSync(MANIFEST, 'utf8');
    const manifest = JSON.parse(raw);
    assert.equal(manifest.name, '@muse/native', 'manifest name must be @muse/native');
    assert.ok(Array.isArray(manifest.components), 'manifest.components must be an array');
    assert.ok(
      manifest.components.length >= 9,
      `manifest must list >=9 components, got ${manifest.components.length}`,
    );
    for (const entry of manifest.components) {
      assert.ok(entry && typeof entry.name === 'string' && entry.name.length > 0, 'each component needs a name');
      assert.ok(entry.props !== undefined, `component ${entry.name} needs a props field`);
    }
  });

  it('index.d.ts exists for @muse/native', () => {
    const stat = fs.statSync(DTS);
    assert.ok(stat.isFile(), 'packages/native/index.d.ts must exist');
  });

  it("require('@muse/native') exposes primitives, components and manifest", () => {
    const mod = require(NATIVE_INDEX);
    assert.ok(Array.isArray(mod.primitives), 'must export a primitives array');
    assert.ok(mod.primitives.length >= 9, `primitives must hold >=9 names, got ${mod.primitives.length}`);
    assert.ok(mod.components && typeof mod.components === 'object', 'must export a components stub map');
    assert.ok(mod.manifest && typeof mod.manifest === 'object', 'must export the manifest');
  });

  it('primitives match the manifest component names', () => {
    const mod = require(NATIVE_INDEX);
    const manifest = JSON.parse(fs.readFileSync(MANIFEST, 'utf8'));
    const manifestNames = manifest.components.map((c) => c.name).sort();
    assert.deepEqual([...mod.primitives].sort(), manifestNames, 'primitives must list exactly the manifest components');
    for (const name of mod.primitives) {
      assert.equal(typeof mod.components[name], 'function', `components[${name}] must be a stub function`);
    }
  });

  it('stub components return { type: name, props } descriptors', () => {
    const mod = require(NATIVE_INDEX);
    for (const name of mod.primitives) {
      const withProps = mod.components[name]({ label: 'x' });
      assert.deepEqual(withProps, { type: name, props: { label: 'x' } }, `${name} stub must return a descriptor`);
      const bare = mod.components[name]();
      assert.deepEqual(bare, { type: name, props: {} }, `${name} stub with no args must default props to {}`);
    }
  });
});

// ---------------------------------------------------------------------------
// T5 — @muse/cli binary wiring: canonical `muse-native` + supported `muse`
// alias, NO `capo` spelling (BUG-TSX-001, RULE-TSX-005 v1.12, D1).
// Interface: `node --test test/packages.test.js` → exit 0 iff (bin map is
// exactly `{muse-native, muse}`) and (`bin/capo.js` absent, `bin/muse.js`
// present) and (`lib/cli.js` has no `capo` token / no `capo` alias branch
// while retaining `muse` alias handling) and (`capo` invocation fails closed,
// non-zero; `muse` invocation warns on stderr but behaves identically to
// canonical); non-zero otherwise.
// ---------------------------------------------------------------------------
describe('TC-TSX-022 Batch 2 — T5 cli bin wiring (muse alias, no capo)', () => {
  const CLI_DIR = path.join(ROOT, 'packages', 'cli');
  const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
  const BIN_MUSE = path.join(CLI_DIR, 'bin', 'muse.js');
  const BIN_CAPO = path.join(CLI_DIR, 'bin', 'capo.js');
  const LIB_CLI = path.join(CLI_DIR, 'lib', 'cli.js');

  it('package.json bin maps exactly muse-native + muse (no capo)', () => {
    const pkg = readJson(path.join(CLI_DIR, 'package.json'));
    assert.ok(pkg.bin && typeof pkg.bin === 'object', 'package.json must declare a bin map');
    assert.deepEqual(
      Object.keys(pkg.bin).sort(),
      ['muse', 'muse-native'],
      'bin map must be exactly {muse-native, muse} with no capo entry',
    );
  });

  it('bin/capo.js is absent and bin/muse.js is present', () => {
    assert.ok(!fs.existsSync(BIN_CAPO), 'packages/cli/bin/capo.js must not exist');
    assert.ok(fs.statSync(BIN_MUSE).isFile(), 'packages/cli/bin/muse.js must exist');
  });

  it('bin shims delegate to ../lib/cli.js and lib/cli.js exists', () => {
    assert.ok(fs.statSync(LIB_CLI).isFile(), 'packages/cli/lib/cli.js must exist');
    for (const bin of [BIN_NATIVE, BIN_MUSE]) {
      const text = fs.readFileSync(bin, 'utf8');
      assert.ok(
        text.includes('../lib/cli'),
        `${path.basename(bin)} must require ../lib/cli.js (one-line shim)`,
      );
    }
  });

  it('lib/cli.js contains no capo token/branch but retains muse handling', () => {
    const text = fs.readFileSync(LIB_CLI, 'utf8');
    assert.ok(!text.includes('capo'), 'lib/cli.js must contain no `capo` token or alias branch');
    assert.ok(text.includes('muse'), 'lib/cli.js must retain `muse` alias handling');
    assert.ok(text.includes('INVOKED'), 'lib/cli.js must keep invoked-name dispatch');
  });

  it('muse-native --help lists the four commands and exits 0', () => {
    const r = spawnSync(process.execPath, [BIN_NATIVE, '--help'], { encoding: 'utf8' });
    assert.equal(r.status, 0, `--help must exit 0, got ${r.status}: ${r.stderr}`);
    for (const cmd of ['init', 'build', 'dev', 'run']) {
      assert.ok(r.stdout.includes(cmd), `--help must list ${cmd}`);
    }
  });

  it('unknown command prints usage to stderr and exits 2', () => {
    const r = spawnSync(process.execPath, [BIN_NATIVE, 'no-such-cmd'], { encoding: 'utf8' });
    assert.equal(r.status, 2, `unknown command must exit 2, got ${r.status}`);
    assert.ok(/usage/i.test(r.stderr), 'unknown command must print usage to stderr');
  });

  it('muse alias warns on stderr but behaves identically to canonical', () => {
    const want = spawnSync(process.execPath, [BIN_NATIVE, '--help'], { encoding: 'utf8' });
    const r = spawnSync(process.execPath, [BIN_MUSE, '--help'], { encoding: 'utf8' });
    assert.equal(r.status, want.status, 'muse --help must exit like muse-native');
    assert.equal(r.stdout, want.stdout, 'muse --help stdout must match muse-native');
    assert.match(r.stderr, /deprecat/i, 'muse must print a one-line deprecation warning to stderr');
  });

  it('capo invocation fails closed (no identical stdout, non-zero)', () => {
    const want = spawnSync(process.execPath, [BIN_NATIVE, '--help'], { encoding: 'utf8' });
    const r = spawnSync(process.execPath, [BIN_CAPO, '--help'], { encoding: 'utf8' });
    assert.ok(
      r.error || r.status !== 0 || r.stdout !== want.stdout,
      'capo path must fail closed: missing binary, non-zero exit, or stdout not identical to canonical',
    );
    if (!r.error) {
      assert.ok(r.status !== 0 || r.stdout !== want.stdout, 'capo stdout must not match canonical --help');
    }
  });
});
