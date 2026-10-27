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
  try {
    return require.resolve('typescript/bin/tsc');
  } catch {
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
}

function hasLocalTsc() {
  try {
    return fs.existsSync(localTscBin());
  } catch {
    return false;
  }
}

// T3 — loud skip where AC-TSX-024 is authoritative (BUG-TSX-006 regression
// test). Env contract: `MUSE_REQUIRE_LIVE_TSC=1` or `CI=true` (any non-empty
// value other than '0'/'false') means the suite is authoritative, so an
// unresolvable local tsc is a failure naming the missing tsc and TC-TSX-024
// instead of a silent skip. Otherwise the historical skip is preserved.
function isStrictLiveTscRequired() {
  const on = (v) => v !== undefined && v !== '' && v !== '0' && v !== 'false';
  return on(process.env.MUSE_REQUIRE_LIVE_TSC) || on(process.env.CI);
}

function liveTscGuard() {
  if (hasLocalTsc()) return { skip: false };
  if (isStrictLiveTscRequired()) {
    throw new Error(
      'Missing local tsc: no local TypeScript compiler resolves, but the run ' +
        'is authoritative (MUSE_REQUIRE_LIVE_TSC or CI is set); ' +
        'TC-TSX-024 live halves must FAIL here, not SKIP.',
    );
  }
  return { skip: true };
}

// Non-throwing skip probe for the `it(..., { skip })` option: strict-but-
// tsc-less resolves to `false` so the half runs and fails loudly in its
// pre-run `liveTscGuard()` assertion instead of skipping.
function liveTscSkip() {
  try {
    return liveTscGuard().skip;
  } catch {
    return false;
  }
}

function writeTsProject(dir, files) {
  const jsxDts = path.join(__dirname, '..', 'index.d.ts');
  // Fixture wiring for the react-jsx transform: the tmp dir must both load
  // the subject declarations (global JSX.IntrinsicElements) and resolve the
  // '@muse/jsx/jsx-runtime' module the transform imports. A `paths` entry
  // alone does neither (it doesn't include the .d.ts in the program and it
  // doesn't cover the /jsx-runtime subpath), so stage a verbatim copy of the
  // subject plus a minimal jsx-runtime shim and include both in the project.
  fs.writeFileSync(
    path.join(dir, 'muse-jsx-subject.d.ts'),
    fs.readFileSync(jsxDts, 'utf8'),
  );
  fs.writeFileSync(
    path.join(dir, 'muse-jsx-runtime.d.ts'),
    'export declare function jsx(type: unknown, props: unknown, key?: unknown): unknown;\n' +
      'export declare const jsxs: typeof jsx;\n' +
      'export declare const Fragment: unknown;\n',
  );
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
      paths: {
        '@muse/jsx': [jsxDts],
        '@muse/jsx/jsx-runtime': ['./muse-jsx-runtime.d.ts'],
      },
    },
  };
  for (const [name, content] of Object.entries(files)) {
    if (name.endsWith('.json')) {
      const include = [
        ...content.include,
        'muse-jsx-subject.d.ts',
        'muse-jsx-runtime.d.ts',
      ];
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
  it('tsc rejects a wrong built-in prop type (string for boolean `test`)', { skip: liveTscSkip() }, () => {
    liveTscGuard();
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

  it('tsc rejects a wrong `key` type (number for string `key`)', { skip: liveTscSkip() }, () => {
    liveTscGuard();
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

  it('tsc accepts correct built-in props on all five built-ins', { skip: liveTscSkip() }, () => {
    liveTscGuard();
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

// ---------------------------------------------------------------------------
// Batch-1 T2 — hardened `tsc` resolution (FR-TSX-009 / AC-TSX-024).
// Hermetic: stubs `Module._resolveFilename` and `fs.existsSync` in-process,
// restores both in `finally`; no downloads, no env/config reads, no live
// services. RED before the T2 coder work: `localTscBin()` never consults
// `require.resolve('typescript/bin/tsc')`, so the first and fourth cases fail.
// ---------------------------------------------------------------------------
describe('T2 — tsc resolution prefers require.resolve with legacy .bin fallback', () => {
  const Module = require('node:module');

  function withStubs({ resolvedPath, exists }) {
    const origResolve = Module._resolveFilename;
    const origExists = fs.existsSync;
    Module._resolveFilename = function (request, ...rest) {
      if (request === 'typescript/bin/tsc') {
        if (resolvedPath instanceof Error) throw resolvedPath;
        return resolvedPath;
      }
      return origResolve.call(this, request, ...rest);
    };
    fs.existsSync = () => exists;
    return () => {
      Module._resolveFilename = origResolve;
      fs.existsSync = origExists;
    };
  }

  it("localTscBin() uses require.resolve('typescript/bin/tsc') when resolvable", () => {
    const fake = path.join(os.tmpdir(), 'fake-typescript-bin-tsc');
    const restore = withStubs({ resolvedPath: fake, exists: true });
    try {
      assert.equal(
        localTscBin(),
        fake,
        "localTscBin() must return require.resolve('typescript/bin/tsc') when it resolves",
      );
    } finally {
      restore();
    }
  });

  it('localTscBin() falls back to the legacy repo-root .bin shim when require.resolve fails', () => {
    const restore = withStubs({
      resolvedPath: Object.assign(new Error('not found'), { code: 'MODULE_NOT_FOUND' }),
      exists: true,
    });
    try {
      const expected = path.join(
        __dirname,
        '..',
        '..',
        '..',
        'node_modules',
        '.bin',
        process.platform === 'win32' ? 'tsc.cmd' : 'tsc',
      );
      assert.equal(localTscBin(), expected, 'legacy .bin fallback must be preserved');
    } finally {
      restore();
    }
  });

  it('hasLocalTsc() is true when the require.resolve path exists', () => {
    const fake = path.join(os.tmpdir(), 'fake-typescript-bin-tsc');
    const restore = withStubs({ resolvedPath: fake, exists: true });
    try {
      assert.equal(hasLocalTsc(), true, 'hasLocalTsc() must be true via the resolved path');
    } finally {
      restore();
    }
  });

  it('hasLocalTsc() is false (never throws, never downloads) when no tsc resolves', () => {
    const restore = withStubs({
      resolvedPath: Object.assign(new Error('not found'), { code: 'MODULE_NOT_FOUND' }),
      exists: false,
    });
    try {
      assert.equal(hasLocalTsc(), false, 'missing tsc must read as false');
    } finally {
      restore();
    }
  });

  it('hasLocalTsc() never throws when the existence check itself throws', () => {
    const origResolve = Module._resolveFilename;
    const origExists = fs.existsSync;
    Module._resolveFilename = () => {
      throw Object.assign(new Error('not found'), { code: 'MODULE_NOT_FOUND' });
    };
    fs.existsSync = () => {
      throw new Error('EACCES');
    };
    try {
      assert.equal(hasLocalTsc(), false, 'hasLocalTsc() must swallow probe errors');
    } finally {
      Module._resolveFilename = origResolve;
      fs.existsSync = origExists;
    }
  });
});

// ---------------------------------------------------------------------------
// Batch-1 T3 — loud skip where AC-TSX-024 is authoritative (regression test
// for BUG-TSX-006). Env contract: `MUSE_REQUIRE_LIVE_TSC=1` or `CI=true` set
// and no local tsc resolves → each of the 3 live halves FAILS (naming the
// missing tsc and TC-TSX-024) instead of SKIP; neither set → current skip
// preserved. RED before the T3 coder work: no `liveTscGuard()` helper exists
// and the three live halves still use the bare skip guard.
// ---------------------------------------------------------------------------
describe('T3 — authoritative-but-tsc-less fails instead of skipping (AC-TSX-024)', () => {
  function guard() {
    assert.equal(
      typeof liveTscGuard,
      'function',
      'liveTscGuard() helper must exist (T3): authoritative skip must fail, not pass silently',
    );
    return liveTscGuard;
  }

  function withEnv(vars, fn) {
    const saved = {};
    for (const k of Object.keys(vars)) {
      saved[k] = Object.prototype.hasOwnProperty.call(process.env, k)
        ? process.env[k]
        : undefined;
      if (vars[k] === undefined) delete process.env[k];
      else process.env[k] = vars[k];
    }
    try {
      return fn();
    } finally {
      for (const k of Object.keys(vars)) {
        if (saved[k] === undefined) delete process.env[k];
        else process.env[k] = saved[k];
      }
    }
  }

  function withNoTsc(fn) {
    const Module = require('node:module');
    const origResolve = Module._resolveFilename;
    const origExists = fs.existsSync;
    Module._resolveFilename = function (request, ...rest) {
      if (request === 'typescript/bin/tsc') {
        throw Object.assign(new Error('not found'), { code: 'MODULE_NOT_FOUND' });
      }
      return origResolve.call(this, request, ...rest);
    };
    fs.existsSync = () => false;
    try {
      return fn();
    } finally {
      Module._resolveFilename = origResolve;
      fs.existsSync = origExists;
    }
  }

  it('without the env contract and without tsc, the live halves still SKIP (exit 0 path)', () => {
    withEnv({ MUSE_REQUIRE_LIVE_TSC: undefined, CI: undefined }, () =>
      withNoTsc(() => {
        assert.equal(hasLocalTsc(), false, 'precondition: no local tsc in this probe');
        const g = guard()();
        assert.equal(g.skip, true, 'non-authoritative tsc-less runs must keep skipping');
      }),
    );
  });

  it('MUSE_REQUIRE_LIVE_TSC=1 without tsc throws naming the missing tsc and TC-TSX-024', () => {
    withEnv({ MUSE_REQUIRE_LIVE_TSC: '1', CI: undefined }, () =>
      withNoTsc(() => {
        assert.throws(
          () => guard()(),
          (e) => {
            const msg = String((e && e.message) || e);
            return /tsc/i.test(msg) && /TC-TSX-024/.test(msg);
          },
          'strict-but-tsc-less must fail naming the missing tsc and TC-TSX-024',
        );
      }),
    );
  });

  it('CI=true without tsc throws naming the missing tsc and TC-TSX-024', () => {
    withEnv({ MUSE_REQUIRE_LIVE_TSC: undefined, CI: 'true' }, () =>
      withNoTsc(() => {
        assert.throws(
          () => guard()(),
          (e) => {
            const msg = String((e && e.message) || e);
            return /tsc/i.test(msg) && /TC-TSX-024/.test(msg);
          },
          'CI-but-tsc-less must fail naming the missing tsc and TC-TSX-024',
        );
      }),
    );
  });

  it('the three live halves no longer use the bare silent-skip guard', () => {
    const src = fs.readFileSync(__filename, 'utf8');
    const bare = '!' + 'has' + 'LocalTsc()';
    const occurrences = src.split('skip: ' + bare).length - 1;
    assert.equal(
      occurrences,
      0,
      `all 3 live halves must route through liveTscGuard() (found ${occurrences} bare guards)`,
    );
  });
});
