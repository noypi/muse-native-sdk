'use strict';
// TC-TSX-024 (AC-TSX-024) — Batch 1 (T1) of FR-TSX-009.
// Hermetic: node:test + node:assert/strict only (+ node:fs/path/os/child_process
// for the tsc fixture, all local files, no network). Inline fixtures only;
// no secrets, no live services.
// Style mirrors packages/compiler/test/binding.test.ts (describe/it + strict
// assert, declaration-position regexes over comment-stripped text, skip-guarded
// local-tsc fixture halves).
// T1 Interface (plan verbatim):
//   `none — <types-only surface; the testable surface is the .d.ts declarations
//   plus a pure helper only if the coder needs one>`
// There is no runtime module to require: every assertion below reads
// packages/jsx/index.d.ts as text, or type-checks a fixture against it with
// the toolchain's own tsc. A compile/import failure counts as red only when it
// is caused by missing declarations, never by a malformed test.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const DTS_PATH = path.join(__dirname, '..', 'index.d.ts');

// Comment-stripped declaration text: bare substring checks would
// false-positive on comments/mentions, so every static case below matches a
// declaration-position regex (same pattern as TC-TSX-018's index.d.ts half).
function strippedDts() {
  const raw = fs.readFileSync(DTS_PATH, 'utf8');
  return raw
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\/\/.*$/gm, '');
}

// The `{ ... }` prop body declared for one intrinsic tag.
function intrinsicBody(dts, tag) {
  const m = dts.match(new RegExp(`'${tag}'\\s*:\\s*\\{([^}]*)\\}`));
  assert.ok(m, `index.d.ts must declare an IntrinsicElements entry for '${tag}'`);
  return m[1];
}

// ---------------------------------------------------------------------------
// Static half — the five `c-*` built-ins with boolean/test/key prop types.
// NOTE (vacuous pass): these declarations pre-exist in index.d.ts, so every
// static case below passes BEFORE the T1 coder work. That is expected and
// documented in unit-tests/T1.md — the same vacuous-pass pattern as the prior
// runs. They pin the declarations so the coder cannot regress them.
// ---------------------------------------------------------------------------
describe('TC-TSX-024 static — built-in prop declarations (AC-TSX-024)', () => {
  it("declares 'c-if' with a required boolean `test` prop", () => {
    const body = intrinsicBody(strippedDts(), 'c-if');
    assert.ok(/\btest\s*:\s*boolean\b/.test(body), "'c-if' must declare `test: boolean`");
    assert.ok(!/\btest\?\s*:/.test(body), "'c-if' `test` must be required, not optional");
  });

  it("declares 'c-else-if' with a required boolean `test` prop", () => {
    const body = intrinsicBody(strippedDts(), 'c-else-if');
    assert.ok(/\btest\s*:\s*boolean\b/.test(body), "'c-else-if' must declare `test: boolean`");
    assert.ok(!/\btest\?\s*:/.test(body), "'c-else-if' `test` must be required, not optional");
  });

  it("declares 'c-else' with no props (children only)", () => {
    const body = intrinsicBody(strippedDts(), 'c-else');
    for (const prop of ['test', 'when', 'each', 'key']) {
      assert.ok(
        !new RegExp(`\\b${prop}\\b`).test(body),
        `'c-else' must not declare a \`${prop}\` prop`,
      );
    }
  });

  it("declares 'c-for' with `each` and a required string `key` prop", () => {
    const body = intrinsicBody(strippedDts(), 'c-for');
    assert.ok(/\beach\b/.test(body), "'c-for' must declare an `each` prop");
    assert.ok(/\bkey\s*:\s*string\b/.test(body), "'c-for' must declare `key: string`");
    assert.ok(!/\bkey\?\s*:/.test(body), "'c-for' `key` must be required, not optional");
  });

  it("declares 'c-show' with a required boolean `when` prop", () => {
    const body = intrinsicBody(strippedDts(), 'c-show');
    assert.ok(/\bwhen\s*:\s*boolean\b/.test(body), "'c-show' must declare `when: boolean`");
    assert.ok(!/\bwhen\?\s*:/.test(body), "'c-show' `when` must be required, not optional");
  });

  it('declares `test`/`when` as boolean-only (no string/unknown widening)', () => {
    const dts = strippedDts();
    for (const tag of ['c-if', 'c-else-if']) {
      const body = intrinsicBody(dts, tag);
      assert.ok(!/\btest\s*:\s*string\b/.test(body), `'${tag}' test must not be string`);
      assert.ok(!/\btest\s*:\s*unknown\b/.test(body), `'${tag}' test must not be unknown`);
    }
    const showBody = intrinsicBody(dts, 'c-show');
    assert.ok(!/\bwhen\s*:\s*string\b/.test(showBody), "'c-show' when must not be string");
    assert.ok(!/\bwhen\s*:\s*unknown\b/.test(showBody), "'c-show' when must not be unknown");
  });
});

// ---------------------------------------------------------------------------
// Toolchain half — `tsc --noEmit` with `jsxImportSource: "@muse/jsx"`
// validates built-in props: a wrong prop type fails, correct props pass.
// Skip-guarded by design (plan A3, FR-TSX-006 precedent): runs only when a
// local tsc exists (never downloads — local node_modules/.bin/tsc only).
// Temp fixtures live under os.tmpdir(); asserts on exit codes, not message
// text. Because the declarations pre-exist, these halves pass (not red) when
// a local tsc is present, and skip otherwise.
// ---------------------------------------------------------------------------
const tmpRoots = [];
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
});

function localTscBin() {
  return path.join(
    __dirname,
    '..',
    '..',
    '..',
    'node_modules',
    '.bin',
    process.platform === 'win32' ? 'tsc.cmd' : 'tsc',
  );
}

function hasLocalTsc() {
  try {
    return fs.existsSync(localTscBin());
  } catch {
    return false;
  }
}

function writeTsProject(dir, files) {
  const jsxDts = path.join(__dirname, '..', 'index.d.ts');
  const baseConfig = {
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
  };
  for (const [name, content] of Object.entries(files)) {
    if (name.endsWith('.json')) {
      const include = content.include;
      fs.writeFileSync(
        path.join(dir, name),
        JSON.stringify({ ...baseConfig, include }),
      );
    } else {
      fs.writeFileSync(path.join(dir, name), content);
    }
  }
}

describe('TC-TSX-024 toolchain — tsc validates built-in prop types (AC-TSX-024)', () => {
  it('tsc rejects a wrong built-in prop type (string for boolean `test`)', { skip: !hasLocalTsc() }, () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-tsx-024-reject-'));
    tmpRoots.push(dir);
    writeTsProject(dir, {
      'tsconfig.json': { include: ['reject-prop.tsx'] },
      'reject-prop.tsx':
        '/** @jsxImportSource @muse/jsx */\n' +
        'export function A() { return (<c-if test="yes">x</c-if>); }\n',
    });
    const r = spawnSync(localTscBin(), ['--noEmit', '-p', dir], { encoding: 'utf8' });
    assert.notEqual(r.status, 0, 'tsc must reject a string `test` on `c-if`');
  });

  it('tsc rejects a wrong `key` type (number for string `key`)', { skip: !hasLocalTsc() }, () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-tsx-024-rejectkey-'));
    tmpRoots.push(dir);
    writeTsProject(dir, {
      'tsconfig.json': { include: ['reject-key.tsx'] },
      'reject-key.tsx':
        '/** @jsxImportSource @muse/jsx */\n' +
        'export function A() { return (<c-for each={[1]} key={42}>x</c-for>); }\n',
    });
    const r = spawnSync(localTscBin(), ['--noEmit', '-p', dir], { encoding: 'utf8' });
    assert.notEqual(r.status, 0, 'tsc must reject a numeric `key` on `c-for`');
  });

  it('tsc accepts correct built-in props on all five built-ins', { skip: !hasLocalTsc() }, () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-tsx-024-accept-'));
    tmpRoots.push(dir);
    writeTsProject(dir, {
      'tsconfig.json': { include: ['accept-props.tsx'] },
      'accept-props.tsx':
        '/** @jsxImportSource @muse/jsx */\n' +
        'export function A() {\n' +
        '  return (\n' +
        '    <c-show when={false}>\n' +
        '      <c-if test={true}>a</c-if>\n' +
        '      <c-else-if test={false}>b</c-else-if>\n' +
        '      <c-else>c</c-else>\n' +
        '      <c-for each={[1, 2]} key="k">d</c-for>\n' +
        '    </c-show>\n' +
        '  );\n' +
        '}\n',
    });
    const r = spawnSync(localTscBin(), ['--noEmit', '-p', dir], { encoding: 'utf8' });
    assert.equal(r.status, 0, `tsc must accept correct built-in props, got: ${r.stdout}${r.stderr}`);
  });
});
