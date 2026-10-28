'use strict';
// TC-SAMPLE-014 (AC-SAMPLE-014, FR-SAMPLE-006) — Batch 1 (T1).
// The scaffolded project is TypeScript and TSX only: tsconfig.json sets
// `jsxImportSource: "@muse/jsx"`, the entry component is App.tsx, and no
// JavaScript or `.capo` template is produced.
// Hermetic: node:test + node:assert/strict + stdlib only. Reads the
// committed templates/ tree plus a freshly scaffolded tmpdir copy; no
// network, no secrets, no live services, no repo writes.
// T1 Interface (plan verbatim):
//   `runInit(dir: string, opts?: { force?: boolean }): void` — copies template
//   tree (muse-native.yaml, package.json with four fixed `@muse/*` deps,
//   tsconfig.json with `jsxImportSource: "@muse/jsx"`, src/App.tsx, README
//   with next commands) into `dir`; exits `2` usage on missing operand, `1`
//   on guard refusal, non-zero with named path on copy failure. Deprecated
//   `muse`/`capo` spellings alias to same path.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T1c: the plan fixes the COUNT (four) and SCOPE (`@muse/*`) of the
//   template deps but not their names/versions; this file asserts exactly
//   four deps, all `@muse/*`-scoped, and does not pin names — the coder
//   picks names per the tsx-dialect package list.
// - A-T1d: "no JavaScript or .capo template" is asserted by walking the
//   whole templates/ tree and rejecting `*.js`/`*.capo` files (docs like
//   README.md are allowed).
// - A-T1e: README "with next commands" must name `cd <dir>`, a
//   `muse-native build` command and a `muse-native run` command.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const TEMPLATES_DIR = path.join(CLI_DIR, 'templates');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

let scratch = null;

beforeEach(() => {
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-init-tpl-'));
});

afterEach(() => {
  if (scratch) {
    fs.rmSync(scratch, { recursive: true, force: true });
    scratch = null;
  }
});

function walkFiles(dir) {
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...walkFiles(full));
    else out.push(full);
  }
  return out;
}

describe('TC-SAMPLE-014 — scaffold is TS/TSX only (AC-SAMPLE-014)', () => {
  it('templates/package.json declares exactly four fixed @muse/* deps', () => {
    const pkg = JSON.parse(fs.readFileSync(path.join(TEMPLATES_DIR, 'package.json'), 'utf8'));
    assert.ok(pkg.dependencies && typeof pkg.dependencies === 'object', 'template package.json must declare dependencies');
    const names = Object.keys(pkg.dependencies);
    assert.equal(names.length, 4, `template must declare exactly four deps, got: ${names.join(', ')}`);
    for (const name of names) {
      assert.ok(
        name.startsWith('@muse/'),
        `every template dep must be @muse/*-scoped, got '${name}'`,
      );
    }
  });

  it('templates/tsconfig.json sets jsxImportSource to @muse/jsx', () => {
    const raw = fs.readFileSync(path.join(TEMPLATES_DIR, 'tsconfig.json'), 'utf8');
    const tsconfig = JSON.parse(raw);
    const jsx = tsconfig && tsconfig.compilerOptions && tsconfig.compilerOptions.jsxImportSource;
    assert.equal(jsx, '@muse/jsx', 'tsconfig.json compilerOptions.jsxImportSource must be "@muse/jsx"');
  });

  it('templates/src/App.tsx exists and is the TSX entry', () => {
    const app = path.join(TEMPLATES_DIR, 'src', 'App.tsx');
    assert.ok(fs.existsSync(app), 'templates must contain src/App.tsx');
    const src = fs.readFileSync(app, 'utf8');
    assert.ok(src.includes('export default'), 'App.tsx must have a default export (the entry component)');
  });

  it('no JavaScript or .capo template is produced anywhere under templates/', () => {
    const files = walkFiles(TEMPLATES_DIR);
    assert.ok(files.length > 0, 'templates/ must not be empty');
    for (const full of files) {
      const rel = path.relative(TEMPLATES_DIR, full);
      assert.ok(!rel.endsWith('.js'), `no JavaScript template allowed, found ${rel}`);
      assert.ok(!rel.endsWith('.capo'), `no .capo template allowed, found ${rel}`);
    }
  });

  it('templates/README.md exists and names the next commands', () => {
    const readme = fs.readFileSync(path.join(TEMPLATES_DIR, 'README.md'), 'utf8');
    assert.ok(readme.includes('cd '), 'README must name the cd <dir> next command');
    assert.ok(readme.includes('muse-native build'), 'README must name the muse-native build next command');
    assert.ok(readme.includes('muse-native run'), 'README must name the muse-native run next command');
  });

  it('scaffolded project carries the TS/TSX-only tree (no .js/.capo emitted)', () => {
    const dir = path.join(scratch, 'my-app');
    const r = spawnSync(process.execPath, [BIN_NATIVE, 'init', dir], { encoding: 'utf8' });
    assert.equal(r.status, 0, `init must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(fs.existsSync(path.join(dir, 'tsconfig.json')), 'scaffold must contain tsconfig.json');
    assert.ok(fs.existsSync(path.join(dir, 'src', 'App.tsx')), 'scaffold must contain src/App.tsx');
    for (const full of walkFiles(dir)) {
      const rel = path.relative(dir, full);
      assert.ok(!rel.endsWith('.js'), `scaffold must emit no JavaScript, found ${rel}`);
      assert.ok(!rel.endsWith('.capo'), `scaffold must emit no .capo, found ${rel}`);
    }
  });
});
