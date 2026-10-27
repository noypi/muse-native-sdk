'use strict';
// TC-TSX-003 (AC-TSX-003) + TC-TSX-004 (AC-TSX-004) + TC-TSX-005 (AC-TSX-005)
// + TC-TSX-006 (AC-TSX-006) + TC-TSX-007 (AC-TSX-007) — Batch 1 (T1–T5 + wiring).
// Hermetic: node:test + node:assert/strict only (+ node:fs/path/os/child_process
// for the T4 tsc fixture, all local files, no network). Inline strings;
// no secrets, no live services.
// Style mirrors test/conditionals.test.ts (describe/it + strict assert).
// Binds ONLY to the pinned T1–T5 `Interface:` lines, never to implementation.
// Validator resolution: `../lib/builtins.js` once it lands, else an inline
// stub whose behavior assertions fail, so every behavior case below is red until
// T1–T5 land — and the harness itself never crashes.
// `formatGrammarDiagnostic` is imported from `../lib/grammar.js` (exists, committed).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { formatGrammarDiagnostic } = require('../lib/grammar.js');

let builtins = null;
try {
  builtins = require('../lib/builtins.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/builtins.js lands.
  builtins = {
    compileBuiltin: () => null,
    validateElseChain: () => null,
    compileCFor: () => null,
    validateBuiltinTag: () => null,
    validateComponentName: () => null,
  };
}
const {
  compileBuiltin,
  validateElseChain,
  compileCFor,
  validateBuiltinTag,
  validateComponentName,
} = builtins;

// BUG-TSX-007 U1: C2 consumer helpers (plan A2 landing: `../lib/builtins.js`
// or new `../lib/visibility.js` required by it). Neither file exports them
// yet, so these resolve to `undefined` and every new case below fails red
// (missing-behavior red) while the harness still loads and all pre-existing
// TC-TSX-003 assertions stay green.
let visibilityHelpers = {};
try {
  visibilityHelpers = {
    ...visibilityHelpers,
    ...require('../lib/visibility.js'),
  };
} catch {
  // Landing file not yet created — expected red; fall through to builtins.
}
for (const k of [
  'isHiddenLayoutKept',
  'isHiddenPainted',
  'isHiddenHitTestable',
  'isHiddenA11yIncluded',
  'snapshotHonorsReducedMotion',
]) {
  if (typeof builtins[k] === 'function' && typeof visibilityHelpers[k] !== 'function') {
    visibilityHelpers[k] = builtins[k];
  }
}
const {
  isHiddenLayoutKept,
  isHiddenPainted,
  isHiddenHitTestable,
  isHiddenA11yIncluded,
  snapshotHonorsReducedMotion,
} = visibilityHelpers;

function loc(overrides = {}) {
  return {
    file: 'Card.tsx',
    line: 21,
    column: 7,
    nodePath: 'Card > c-if[0]',
    ...overrides,
  };
}

const KNOWN_BUILTINS = ['c-if', 'c-else-if', 'c-else', 'c-for', 'c-show'];

// ---------------------------------------------------------------------------
// TC-TSX-003 → AC-TSX-003 — T1 contract:
// `compileBuiltin(tag: string, props: Record<string, string>): { kind: "if" | "show", expr: string, visibility: null | { keepLayout: true, paintHidden: true, a11yIncluded: false } } | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-003 — c-if/c-show lower to conditional/visibility IR (AC-TSX-003)', () => {
  it('lowers c-if/test to { kind: if, expr, visibility: null }', () => {
    assert.deepEqual(compileBuiltin('c-if', { test: 'ok' }), {
      kind: 'if',
      expr: 'ok',
      visibility: null,
    });
  });

  it('lowers c-show/when to { kind: show } with the D1 visibility record', () => {
    // D1 (Q-TSX-002 recommended default): hidden state keeps layout space,
    // hides paint, excludes the hidden subtree from accessibility.
    const node = compileBuiltin('c-show', { when: 'open' });
    assert.notEqual(node, null, 'c-show must lower to a node, not null');
    assert.equal(node.kind, 'show');
    assert.equal(node.expr, 'open');
    assert.deepEqual(node.visibility, {
      keepLayout: true,
      paintHidden: true,
      hitTestable: false,
      a11yIncluded: false,
    });
  });

  it('returns null for an unknown tag', () => {
    assert.equal(compileBuiltin('c-foo', { test: 'ok' }), null);
  });

  it('returns null for a non-builtin tag', () => {
    assert.equal(compileBuiltin('div', {}), null);
  });

  // BUG-TSX-007 U1 (AC-TSX-033 option A) — six per-dimension red cases.
  // (a) hidden keeps layout space (no-reflow invariant).
  it('U1(a): hidden record keeps layout space (keepLayout: true, never reflows siblings)', () => {
    const node = compileBuiltin('c-show', { when: 'open' });
    assert.notEqual(node, null);
    assert.equal(node.visibility.keepLayout, true);
    // No-reflow invariant: the hidden box is kept, siblings never reflow.
    assert.equal(isHiddenLayoutKept(node.visibility), true);
  });

  // (b) paint skipped when hidden.
  it('U1(b): paint is skipped when hidden (paintHidden: true)', () => {
    const node = compileBuiltin('c-show', { when: 'open' });
    assert.notEqual(node, null);
    assert.equal(node.visibility.paintHidden, true);
    assert.equal(isHiddenPainted(node.visibility), false);
  });

  // (c) hit-test excluded when hidden (new flag).
  it('U1(c): hit-test is excluded when hidden (hitTestable: false)', () => {
    const node = compileBuiltin('c-show', { when: 'open' });
    assert.notEqual(node, null);
    assert.equal(node.visibility.hitTestable, false);
    assert.equal(isHiddenHitTestable(node.visibility), false);
  });

  // (d) hidden subtree excluded from accessibility.
  it('U1(d): hidden subtree is excluded from a11y (a11yIncluded: false)', () => {
    const node = compileBuiltin('c-show', { when: 'open' });
    assert.notEqual(node, null);
    assert.equal(node.visibility.a11yIncluded, false);
    assert.equal(isHiddenA11yIncluded(node.visibility), false);
  });

  // (e) reduce-motion instant snap (instant path, never frame counts).
  it('U1(e): reduce-motion snaps instantly (animate: false, reflow: false)', () => {
    const node = compileBuiltin('c-show', { when: 'open' });
    assert.notEqual(node, null);
    assert.deepEqual(
      snapshotHonorsReducedMotion(node.visibility, { reduceMotion: true }),
      { animate: false, reflow: false },
    );
  });

  // (f) negative: visible state paints + hit-tests + exposes a11y normally.
  it('U1(f): visible state paints + hit-tests + exposes a11y normally', () => {
    const node = compileBuiltin('c-show', { when: 'open' });
    assert.notEqual(node, null);
    // Hidden helpers all deny (false) so the visible path paints, hit-tests
    // and exposes a11y; motion without reduce-motion animates, still no reflow.
    assert.equal(isHiddenPainted(node.visibility), false);
    assert.equal(isHiddenHitTestable(node.visibility), false);
    assert.equal(isHiddenA11yIncluded(node.visibility), false);
    assert.deepEqual(
      snapshotHonorsReducedMotion(node.visibility, { reduceMotion: false }),
      { animate: true, reflow: false },
    );
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-004 → AC-TSX-004 — T2 contract:
// `validateElseChain(tags: string[], loc: SlotLocation): BuiltinDiagnostic | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-004 — else-if/else adjacency validation (AC-TSX-004)', () => {
  it('accepts an adjacent chain [c-if, c-else-if, c-else] (null)', () => {
    assert.equal(validateElseChain(['c-if', 'c-else-if', 'c-else'], loc()), null);
  });

  it('accepts a lone c-if (null)', () => {
    assert.equal(validateElseChain(['c-if'], loc()), null);
  });

  it('rejects an interleaved chain [c-if, Row, c-else] with file:line:column', () => {
    const l = loc();
    const d = validateElseChain(['c-if', 'Row', 'c-else'], l);
    assert.notEqual(d, null, 'interleaved chain must return a non-null diagnostic');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
    const line = formatGrammarDiagnostic(d);
    assert.ok(!line.includes('\n'), 'diagnostic must be one line');
    assert.ok(line.includes('Card.tsx'), 'diagnostic must name the file');
  });

  it('rejects a chain broken by a text node slot', () => {
    const l = loc({ line: 22 });
    const d = validateElseChain(['c-if', 'c-else-if', 'text', 'c-else'], l);
    assert.notEqual(d, null, 'text between chain links must fail');
    assert.equal(d.line, 22);
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-005 → AC-TSX-005 — T3 contract:
// `compileCFor(each: string, key: string | null | undefined, loc: SlotLocation): { kind: "keyed-list", each: string, key: string } | BuiltinDiagnostic`
// ---------------------------------------------------------------------------
describe('TC-TSX-005 — c-for lowers to an ordered keyed list (AC-TSX-005)', () => {
  it('lowers each+key to { kind: keyed-list, each, key }', () => {
    assert.deepEqual(compileCFor('items', 'item.id', loc()), {
      kind: 'keyed-list',
      each: 'items',
      key: 'item.id',
    });
  });

  it('fails a missing key (null) with a diagnostic naming key', () => {
    const d = compileCFor('items', null, loc());
    assert.notEqual(d, null, 'missing key must return a diagnostic');
    assert.ok(d.form.includes('key'), 'diagnostic form must name `key`');
  });

  it('fails an undefined key with a diagnostic naming key', () => {
    const d = compileCFor('items', undefined, loc());
    assert.notEqual(d, null, 'undefined key must return a diagnostic');
    assert.ok(d.form.includes('key'), 'diagnostic form must name `key`');
  });

  it('fails an empty-string key with a diagnostic naming key', () => {
    const d = compileCFor('items', '', loc());
    assert.notEqual(d, null, 'empty key must return a diagnostic');
    assert.ok(d.form.includes('key'), 'diagnostic form must name `key`');
  });

  it('echoes the caller file:line:column on a missing-key diagnostic', () => {
    const l = loc({ line: 30, column: 3 });
    const d = compileCFor('items', null, l);
    assert.notEqual(d, null);
    assert.equal(d.file, 'Card.tsx');
    assert.equal(d.line, 30);
    assert.equal(d.column, 3);
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-006 → AC-TSX-006 — T4 contract:
// `validateBuiltinTag(tag: string, loc: SlotLocation): BuiltinDiagnostic | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-006 — unknown c-* tags fail (AC-TSX-006)', () => {
  it('fails an unknown c-* tag with a diagnostic naming the tag', () => {
    const l = loc();
    const d = validateBuiltinTag('c-foo', l);
    assert.notEqual(d, null, 'unknown c-* tag must return a diagnostic');
    assert.ok(d.form.includes('c-foo'), 'diagnostic form must name the tag');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('accepts all five known built-ins (null)', () => {
    for (const tag of KNOWN_BUILTINS) {
      assert.equal(validateBuiltinTag(tag, loc()), null, `${tag} must be accepted`);
    }
  });

  it('existing @muse/jsx declarations type the five built-ins and declare no unknown c-* tag', () => {
    // Static half of the tsc gate: the declarations already exist in
    // packages/jsx/index.d.ts (plan verdict — this run authors none).
    // This is the binding assertion when no hermetic tsc is available.
    const dts = fs.readFileSync(
      path.join(__dirname, '..', '..', 'jsx', 'index.d.ts'),
      'utf8',
    );
    for (const tag of KNOWN_BUILTINS) {
      assert.ok(dts.includes(`'${tag}'`), `index.d.ts must declare ${tag}`);
    }
    assert.ok(dts.includes('test: boolean'), 'c-if/c-else-if test must be boolean');
    assert.ok(dts.includes('when: boolean'), 'c-show when must be boolean');
    assert.ok(dts.includes('key: string'), 'c-for key must be string');
    assert.ok(!dts.includes('c-bogus'), 'index.d.ts must not declare an unknown c-* tag');
  });

  it('tsc --noEmit rejects an unknown c-* tag and a mistyped built-in prop', { skip: !hasLocalTsc() }, () => {
    // Real-toolchain half of the tsc gate: runs only when a local tsc exists
    // (never downloads — local node_modules/.bin/tsc only). Temp fixture
    // lives under os.tmpdir(); asserts on exit code, not message text.
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-tsx-006-'));
    const jsxDts = path.join(__dirname, '..', '..', 'jsx', 'index.d.ts');
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
          paths: { '@muse/jsx': [jsxDts] },
        },
        include: ['*.tsx'],
      }),
    );
    fs.writeFileSync(
      path.join(dir, 'unknown-tag.tsx'),
      '/** @jsxImportSource @muse/jsx */\nexport function A() { return <c-bogus />; }\n',
    );
    fs.writeFileSync(
      path.join(dir, 'wrong-prop.tsx'),
      '/** @jsxImportSource @muse/jsx */\nexport function B() { return <c-if test="not-bool">x</c-if>; }\n',
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
    assert.notEqual(r.status, 0, 'tsc must reject the unknown tag and the mistyped prop');
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

// ---------------------------------------------------------------------------
// TC-TSX-007 → AC-TSX-007 — T5 contract:
// `validateComponentName(name: string, loc: SlotLocation): BuiltinDiagnostic | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-007 — c- prefix is reserved (AC-TSX-007)', () => {
  it('fails a user component named c-Card with a diagnostic naming it', () => {
    const l = loc();
    const d = validateComponentName('c-Card', l);
    assert.notEqual(d, null, 'c- prefixed declaration must return a diagnostic');
    assert.ok(d.form.includes('c-Card'), 'diagnostic form must name the declaration');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('fails a declaration colliding with a built-in name', () => {
    const d = validateComponentName('c-if', loc());
    assert.notEqual(d, null, 'built-in names are tags, never declarations');
    assert.ok(d.form.includes('c-if'));
  });

  it('accepts a PascalCase component name (null)', () => {
    assert.equal(validateComponentName('Card', loc()), null);
  });
});
