'use strict';
// TC-TSX-016 (AC-TSX-016) + TC-TSX-017 (AC-TSX-017) + TC-TSX-018 (AC-TSX-018) — Batch 1 (T1–T4 + wiring).
// Hermetic: node:test + node:assert/strict only (+ node:fs/path/os/child_process
// for the T4 tsc fixture, all local files, no network). Inline fixtures only;
// no secrets, no live services.
// Style mirrors test/builtins.test.ts (describe/it + strict assert).
// Binds ONLY to the pinned T1–T4 `Interface:` lines, never to implementation.
// Validator resolution: `../lib/binding.js` once it lands, else an inline
// stub whose behavior assertions fail, so every behavior case below is red until
// T1–T4 land — and the harness itself never crashes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

let binding = null;
try {
  binding = require('../lib/binding.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/binding.js lands.
  binding = {
    compileValueBinding: () => null,
    compileReadonlyBinding: () => null,
    compileSpreadBinding: () => ({ kind: 'binding', mode: 'two-way' }),
    validateBindingProp: () => null,
  };
}
const {
  compileValueBinding,
  compileReadonlyBinding,
  compileSpreadBinding,
  validateBindingProp,
} = binding;

function loc(overrides = {}) {
  return {
    file: 'Card.tsx',
    line: 42,
    column: 9,
    nodePath: 'Card > TextInput[0]',
    ...overrides,
  };
}

// ---------------------------------------------------------------------------
// TC-TSX-016 → AC-TSX-016 — T1 contract:
// `compileValueBinding(args: { value: string, isBareWritable: boolean, hasOnChange: boolean }, loc: { file: string, line: number, column: number, nodePath: string }): { kind: "binding", mode: "two-way", target: string, reads: string[], writes: string[] } | { kind: "binding", mode: "one-way", expr: string } | BuiltinDiagnostic`
// ---------------------------------------------------------------------------
describe('TC-TSX-016 — bare writable value lowers to two-way (AC-TSX-016)', () => {
  it('lowers bare writable value to a two-way node with read+write edges', () => {
    assert.deepEqual(
      compileValueBinding(
        { value: 'someState', isBareWritable: true, hasOnChange: false },
        loc(),
      ),
      {
        kind: 'binding',
        mode: 'two-way',
        target: 'someState',
        reads: ['someState'],
        writes: ['someState'],
      },
    );
  });

  it('non-bare value (member access) falls through to the T2 gate, not two-way', () => {
    // Row contract is not-two-way + fall-through ONLY; the one-way shape for
    // the hasOnChange:true path is pinned by TC-TSX-017, not here.
    const node = compileValueBinding(
      { value: 'state.count', isBareWritable: true, hasOnChange: true },
      loc(),
    );
    assert.notEqual(node, null, 'non-bare value must not return null');
    assert.notEqual(node.mode, 'two-way', 'non-bare value must never be two-way');
  });

  it('call-expression value with isBareWritable:true is not two-way (fall-through)', () => {
    const node = compileValueBinding(
      { value: 'f()', isBareWritable: true, hasOnChange: true },
      loc(),
    );
    assert.notEqual(node, null, 'call-expression value must not return null');
    assert.notEqual(node.mode, 'two-way', 'call-expression value must never be two-way');
  });

  it('binary-expression value with isBareWritable:true is not two-way (fall-through)', () => {
    const node = compileValueBinding(
      { value: 'a + b', isBareWritable: true, hasOnChange: true },
      loc(),
    );
    assert.notEqual(node, null, 'binary-expression value must not return null');
    assert.notEqual(node.mode, 'two-way', 'binary-expression value must never be two-way');
  });

  it('bare-shape value with isBareWritable:false falls through, not two-way', () => {
    const node = compileValueBinding(
      { value: 'someState', isBareWritable: false, hasOnChange: true },
      loc(),
    );
    assert.notEqual(node, null, 'readonly bare value must not return null');
    assert.notEqual(node.mode, 'two-way', 'readonly bare value must never be two-way');
    assert.equal(node.mode, 'one-way');
  });

  it('padded bare writable value lowers to two-way on the trimmed name', () => {
    // Plan T1 Detail: the identifier-shape gate tests the trimmed value, so
    // surrounding whitespace must not defeat two-way lowering; the node
    // echoes the trimmed name.
    assert.deepEqual(
      compileValueBinding(
        { value: ' someState ', isBareWritable: true, hasOnChange: false },
        loc(),
      ),
      {
        kind: 'binding',
        mode: 'two-way',
        target: 'someState',
        reads: ['someState'],
        writes: ['someState'],
      },
    );
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-017 → AC-TSX-017 — T2 contract:
// `compileReadonlyBinding(args: { expr: string, hasOnChange: boolean }, loc: { file: string, line: number, column: number, nodePath: string }): { kind: "binding", mode: "one-way", expr: string } | BuiltinDiagnostic`
// ---------------------------------------------------------------------------
describe('TC-TSX-017 — readonly / non-bare value requires onChange (AC-TSX-017)', () => {
  it('returns a one-way node when hasOnChange is true', () => {
    assert.deepEqual(
      compileReadonlyBinding({ expr: 'anything', hasOnChange: true }, loc()),
      { kind: 'binding', mode: 'one-way', expr: 'anything' },
    );
  });

  it('returns a diagnostic naming onChange when hasOnChange is false', () => {
    const l = loc();
    const d = compileReadonlyBinding({ expr: 'anything', hasOnChange: false }, l);
    assert.notEqual(d, null, 'missing onChange must return a diagnostic');
    assert.ok(d.form.includes('onChange'), 'diagnostic form must name `onChange`');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('non-bare value with hasOnChange:true lowers to a one-way node', () => {
    const node = compileValueBinding(
      { value: 'state.count', isBareWritable: true, hasOnChange: true },
      loc(),
    );
    assert.notEqual(node, null);
    assert.equal(node.mode, 'one-way');
    assert.equal(node.expr, 'state.count');
  });

  it('non-bare value with hasOnChange:false returns a diagnostic naming the value form', () => {
    const l = loc();
    const d = compileValueBinding(
      { value: 'state.count', isBareWritable: true, hasOnChange: false },
      l,
    );
    assert.notEqual(d, null, 'non-bare value without onChange must fail');
    assert.ok(
      d.form.includes('state.count') || d.form.includes('onChange'),
      'diagnostic form must name the value form or the missing `onChange`',
    );
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('readonly bare value (isBareWritable:false) without onChange returns a diagnostic naming the form', () => {
    const l = loc();
    const d = compileValueBinding(
      { value: 'someReadonly', isBareWritable: false, hasOnChange: false },
      l,
    );
    assert.notEqual(d, null, 'readonly bare value without onChange must fail');
    assert.ok(
      d.form.includes('someReadonly'),
      'diagnostic form must name the readonly value form',
    );
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('readonly bare value (isBareWritable:false) with onChange lowers to a one-way node', () => {
    const node = compileValueBinding(
      { value: 'someReadonly', isBareWritable: false, hasOnChange: true },
      loc(),
    );
    assert.notEqual(node, null);
    assert.equal(node.mode, 'one-way');
    assert.equal(node.expr, 'someReadonly');
  });

  it('computed value without onChange returns a diagnostic naming the form', () => {
    const l = loc();
    const d = compileValueBinding(
      { value: 'a + b', isBareWritable: true, hasOnChange: false },
      l,
    );
    assert.notEqual(d, null, 'computed value without onChange must fail');
    assert.ok(
      d.form.includes('a + b'),
      'diagnostic form must name the computed value form',
    );
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('computed value with onChange lowers to a one-way node', () => {
    const node = compileValueBinding(
      { value: 'a + b', isBareWritable: true, hasOnChange: true },
      loc(),
    );
    assert.notEqual(node, null);
    assert.equal(node.mode, 'one-way');
    assert.equal(node.expr, 'a + b');
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-018 → AC-TSX-018 — T3 + T4 contracts:
// `compileSpreadBinding(args: { hasSpreadValue: boolean }, loc: { file: string, line: number, column: number, nodePath: string }): null`
// `validateBindingProp(prop: string, loc: { file: string, line: number, column: number, nodePath: string }): BuiltinDiagnostic | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-018 — spread never synthesizes two-way; model is rejected (AC-TSX-018)', () => {
  it('compileSpreadBinding({ hasSpreadValue: true }) returns null', () => {
    assert.equal(
      compileSpreadBinding({ hasSpreadValue: true }, loc()),
      null,
    );
  });

  it('compileSpreadBinding({ hasSpreadValue: false }) returns null', () => {
    assert.equal(
      compileSpreadBinding({ hasSpreadValue: false }, loc()),
      null,
    );
  });

  it('validateBindingProp rejects model with a diagnostic naming model', () => {
    const l = loc();
    const d = validateBindingProp('model', l);
    assert.notEqual(d, null, 'model must return a diagnostic');
    assert.ok(d.form.includes('model'), 'diagnostic form must name `model`');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('validateBindingProp accepts known props (value, valueReadonly, onChange)', () => {
    for (const prop of ['value', 'valueReadonly', 'onChange']) {
      assert.equal(validateBindingProp(prop, loc()), null, `${prop} must be accepted`);
    }
  });

  it('index.d.ts declares value/valueReadonly/onChange and no model prop (static half of the tsc gate)', () => {
    // Static half: asserts the three declared props are present on the
    // applicable element AND that `model` is absent. Prop positions are
    // matched with a declaration-position regex over comment-stripped text
    // (bare substring checks would false-positive on comments/mentions).
    const raw = fs.readFileSync(
      path.join(__dirname, '..', '..', 'jsx', 'index.d.ts'),
      'utf8',
    );
    const dts = raw
      .replace(/\/\*[\s\S]*?\*\//g, '')
      .replace(/\/\/.*$/gm, '');
    for (const prop of ['value', 'valueReadonly', 'onChange']) {
      assert.ok(
        new RegExp(`\\b${prop}\\s*\\??:`).test(dts),
        `index.d.ts must declare a \`${prop}\` prop`,
      );
    }
    assert.ok(
      !/\bmodel\s*\??:/.test(dts),
      'index.d.ts must not declare a `model` prop',
    );
  });

  it('tsc --noEmit rejects model and accepts value/valueReadonly+onChange', () => {
    // Fail-closed live-tsc gate (FR-TSX-006, AC-TSX-018, RULE-TSX-003): a
    // missing local tsc must FAIL, never silently SKIP. Probe unchanged.
    if (!hasLocalTsc()) {
      assert.fail(
        'live tsc fixture requires node_modules/.bin/tsc — run npm install (declares typescript per T1)',
      );
    }
    // Real-toolchain half of the tsc gate: runs only when a local tsc exists
    // (never downloads — local node_modules/.bin/tsc only). Temp fixture
    // lives under os.tmpdir(); asserts on exit code, not message text.
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-tsx-018-'));
    const jsxDts = path.join(__dirname, '..', '..', 'jsx', 'index.d.ts');
    // Fixture-level resolution scaffolding (test-owned, under os.tmpdir()):
    // (a) `paths` only redirects imports — it never adds files to the program —
    // so the real index.d.ts (global JSX namespace) must be listed in `include`
    // by absolute path or TS7026 reports "no interface 'JSX.IntrinsicElements'";
    // (b) `jsx: react-jsx` requires a `@muse/jsx/jsx-runtime` module (TS2875),
    // which the types-only @muse/jsx package legitimately does not ship
    // (AC-TSX-021: "No runtime entry"), so the fixture provides a temp-local
    // type-only shim. Neither changes production; both fix exit-2
    // config/resolution failures, not type assertions.
    fs.writeFileSync(
      path.join(dir, 'jsx-runtime.d.ts'),
      'export function jsx(...args: any[]): any;\nexport function jsxs(...args: any[]): any;\nexport const Fragment: any;\n',
    );
    fs.writeFileSync(
      path.join(dir, 'tsconfig.json'),
      JSON.stringify({
        compilerOptions: {
          strict: true,
          noEmit: true,
          jsx: 'react-jsx',
          jsxImportSource: '@muse/jsx',
          moduleResolution: 'bundler',
          module: 'esnext',
          target: 'es2022',
          baseUrl: '.',
          paths: {
            '@muse/jsx': [jsxDts],
            '@muse/jsx/jsx-runtime': ['./jsx-runtime.d.ts'],
          },
        },
        include: ['*.tsx', './jsx-runtime.d.ts', jsxDts],
      }),
    );
    fs.writeFileSync(
      path.join(dir, 'reject-model.tsx'),
      '/** @jsxImportSource @muse/jsx */\nexport function A() { return <input model={1} />; }\n',
    );
    // Accept half, same temp dir: value + valueReadonly/onChange on the typed
    // element must pass. Kept in a separate accept-only project (own tsconfig
    // including just this file) so the reject fixture above cannot mask it.
    fs.writeFileSync(
      path.join(dir, 'accept-value.tsx'),
      '/** @jsxImportSource @muse/jsx */\ndeclare const s: unknown;\ndeclare const e: unknown;\ndeclare const f: (next: unknown) => void;\nexport function B() { return <input value={s} />; }\nexport function C() { return <input valueReadonly={e} onChange={f} />; }\n',
    );
    fs.writeFileSync(
      path.join(dir, 'tsconfig.accept.json'),
      JSON.stringify({
        compilerOptions: {
          strict: true,
          noEmit: true,
          jsx: 'react-jsx',
          jsxImportSource: '@muse/jsx',
          moduleResolution: 'bundler',
          module: 'esnext',
          target: 'es2022',
          baseUrl: '.',
          paths: {
            '@muse/jsx': [jsxDts],
            '@muse/jsx/jsx-runtime': ['./jsx-runtime.d.ts'],
          },
        },
        include: ['accept-value.tsx', './jsx-runtime.d.ts', jsxDts],
      }),
    );
    const tscBin = path.join(
      __dirname,
      '..',
      '..',
      '..',
      'node_modules',
      '.bin',
      process.platform === 'win32' ? 'tsc.cmd' : 'tsc',
    );
    const r = spawnSync(tscBin, ['--noEmit', '-p', dir], { encoding: 'utf8' });
    assert.notEqual(r.status, 0, 'tsc must reject the model prop');
    const a = spawnSync(tscBin, ['--noEmit', '-p', path.join(dir, 'tsconfig.accept.json')], { encoding: 'utf8' });
    assert.equal(a.status, 0, 'tsc must accept value and valueReadonly+onChange');
  });
});

function hasLocalTsc() {
  try {
    const tscBin = path.join(
      __dirname,
      '..',
      '..',
      '..',
      'node_modules',
      '.bin',
      process.platform === 'win32' ? 'tsc.cmd' : 'tsc',
    );
    return fs.existsSync(tscBin);
  } catch {
    return false;
  }
}
