'use strict';
// Batch 2 / T3 (FR-PACKAGING-010, AC-PACKAGING-028 + required AC-023 slice).
// Scope: examples/m3-gallery/Makefile read-only SDK precondition chain in the
// fixed order HOME-set → VERSION-present → line-1-equals-sdk-pin (names both)
// → per-ABI test -s; any failure stops before compilation; no `../` SDK
// traversal remains (ENGINE_LIBS + lib refs rooted at $(MUSE_NATIVE_HOME)).
// Interface (plan T3 verbatim):
//   `make <apk|sign|verify|install>` precondition chain — order:
//   `ifndef MUSE_NATIVE_HOME → error` (SDK-HOME-UNSET) →
//   `test -f $(MUSE_NATIVE_HOME)/VERSION` (SDK-VERSION-MISSING) → line 1
//   equals expected `sdk:` version (SDK-VERSION-MISMATCH names both) →
//   per-ABI `test -s` (SDK-ABI-MISSING); any failure stops before
//   compilation. `none — make precondition surface, no new callable`
// Hermetic: node:test + node:assert/strict + stdlib only. Makefile-text
// assertions plus `make -n` dry runs against a sandbox SDK dir under
// os.tmpdir() (fixture VERSION + libRuntime.so files, local `make` binary
// only — dry-run executes no recipe, needs no NDK/SDK/device/network).
// No secrets, no .env, no live service. RED until the coder prepends the
// check block and swaps `../../build` refs to $(MUSE_NATIVE_HOME).
// ASSUMPTION A-C1 (contract genuinely silent): the expected version is read
// from the project's muse-native.yaml `sdk:` key via a shell one-liner
// (plan Risks); tests accept any grep/sed extraction naming `sdk:`.
// ASSUMPTION A-C2: the single exempt `../$(notdir …)` inside the BUILD_DIR
// staging `cd … && zip` line (comment-marked) is not SDK traversal; the
// no-traversal gate bans `../../`, `../build`, and `../../build` tokens.
// ASSUMPTION A-C3: `make -n` prints recipes without executing them, so live
// gate refusal is proven for the parse-time HOME gate only; recipe-gate
// refusal is proven textually (non-zero `exit 1` after each gate).
// SECURITY COVERAGE (security-reviewer, 0 findings, 2 coverage requests):
// S-C1 malformed VERSION content (wrong line count / non-SemVer line 1 /
//   line 2 missing `sha=`+date / line 3 missing `abis=`) is refused with the
//   SDK-VERSION-MISSING shape before compilation (textual gates + live `make`
//   refusal against a sandbox HOME; live run executes no compile tool).
// S-C2 `sdk_pin` extraction strictness: affixed/range/commented `sdk:` values
//   (`^0.1.0`, `~0.1.0`, `>=0.1.0`, `0.1.x`, trailing `# comment`,
//   commented-out key) never equal the expected version — warn-or-mismatch
//   naming both versions, never equality.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');

function repoRoot() {
  let dir = __dirname;
  for (let i = 0; i < 8; i += 1) {
    if (fs.existsSync(path.join(dir, 'scripts', 'build-sample.sh'))) return dir;
    dir = path.dirname(dir);
  }
  throw new Error('test setup: cannot locate repo root from ' + __dirname);
}

const ROOT = repoRoot();
const SCAFFOLD_MAKEFILE = path.join(ROOT, 'examples', 'm3-gallery', 'Makefile');
const GALLERY_DIR = path.join(ROOT, 'examples', 'm3-gallery');
const ABIS = ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'];

function readScaffold() {
  assert.ok(fs.existsSync(SCAFFOLD_MAKEFILE),
    'scaffold Makefile must exist (T3 owns the check block; RED until the coder adds it)');
  return fs.readFileSync(SCAFFOLD_MAKEFILE, 'utf8');
}

function apkBody(text) {
  const lines = text.split('\n');
  const idx = lines.findIndex((l) => /^apk\s*:/.test(l));
  assert.ok(idx !== -1, 'scaffold Makefile must define target: apk');
  let end = lines.length;
  for (let i = idx + 1; i < lines.length; i += 1) {
    if (/^[A-Za-z0-9_.-]+\s*:(?![:=])/.test(lines[i]) && !/^\s/.test(lines[i])) { end = i; break; }
  }
  return lines.slice(idx, end).join('\n');
}

// --- sandbox SDK HOME for `make -n` dry runs (fixture only) ---
const sandboxes = [];
afterEach(() => {
  while (sandboxes.length) {
    const dir = sandboxes.pop();
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

function sandboxHome({ versionLine1 = '0.1.0', omitVersion = false, omitAbi = null } = {}) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sdk-home-'));
  sandboxes.push(dir);
  if (!omitVersion) {
    fs.writeFileSync(
      path.join(dir, 'VERSION'),
      `${versionLine1}\nsha=abcdef date=2026-10-30T00:00:00Z\nabis=arm64-v8a armeabi-v7a x86_64 x86\n`,
    );
  }
  for (const abi of ABIS) {
    if (abi === omitAbi) continue;
    fs.mkdirSync(path.join(dir, `android-${abi}`), { recursive: true });
    fs.writeFileSync(path.join(dir, `android-${abi}`, 'libRuntime.so'), 'ELF');
  }
  return dir;
}

function makeN(dir, home) {
  const env = { ...process.env };
  if (home === null) delete env.MUSE_NATIVE_HOME;
  else env.MUSE_NATIVE_HOME = home;
  return cp.spawnSync('make', ['-n', 'apk'], { cwd: dir, encoding: 'utf8', env });
}

describe('T3 HOME gate first (AC-PACKAGING-028 order step 1, AC-023)', () => {
  it('ifndef MUSE_NATIVE_HOME errors naming the variable + example export', () => {
    const text = readScaffold();
    assert.match(text, /ifndef\s+MUSE_NATIVE_HOME/, 'check block must open with the HOME guard');
    const head = text.slice(0, text.indexOf('ifndef MUSE_NATIVE_HOME') + 400);
    assert.match(text, /\$\(error[^)]*MUSE_NATIVE_HOME[^)]*\)/, 'HOME gate must $(error) naming the variable');
    assert.ok(head.includes('MUSE_NATIVE_HOME') && /export/.test(text),
      'HOME error must show an example export');
  });

  it('HOME guard precedes every other SDK check (fixed order step 1)', () => {
    const text = readScaffold();
    const homeIdx = text.indexOf('ifndef MUSE_NATIVE_HOME');
    assert.ok(homeIdx !== -1, 'HOME guard must exist');
    assert.ok(homeIdx < text.indexOf('VERSION'), 'HOME guard must precede the VERSION check');
    assert.ok(homeIdx < text.indexOf('test -s'), 'HOME guard must precede the per-ABI gates');
  });

  it('make -n apk with unset HOME fails closed naming the variable', () => {
    const r = makeN(GALLERY_DIR, null);
    assert.notEqual(r.status, 0, 'unset HOME must fail before compilation');
    assert.match(r.stdout + r.stderr, /MUSE_NATIVE_HOME/, 'failure must name the variable');
  });
});

describe('T3 VERSION-present gate second (SDK-VERSION-MISSING)', () => {
  it('apk recipe gates on test -f $(MUSE_NATIVE_HOME)/VERSION', () => {
    const body = apkBody(readScaffold());
    assert.match(body, /test\s+-f[^;\n]*MUSE_NATIVE_HOME[^;\n]*\/VERSION/,
      'apk must gate on test -f $(MUSE_NATIVE_HOME)/VERSION');
    assert.match(body, /exit\s+1/, 'VERSION gate must exit non-zero on failure');
  });

  it('VERSION-present check sits between HOME guard and version-match check', () => {
    const text = readScaffold();
    const presentIdx = text.indexOf('test -f');
    const matchIdx = Math.max(text.indexOf('SDK-VERSION-MISMATCH'), text.indexOf('sdk_pin'));
    assert.ok(presentIdx !== -1 && matchIdx !== -1, 'both gates must exist');
    assert.ok(text.indexOf('ifndef MUSE_NATIVE_HOME') < presentIdx,
      'VERSION-present must follow the HOME guard');
    assert.ok(presentIdx < matchIdx, 'VERSION-present must precede the version-match check');
  });

  it('make -n apk with missing VERSION prints the VERSION gate (dry-run surfaces it)', () => {
    const home = sandboxHome({ omitVersion: true });
    const r = makeN(GALLERY_DIR, home);
    assert.equal(r.status, 0, `dry-run prints recipes without executing: ${r.stderr}`);
    assert.match(r.stdout, /test\s+-f[^\\n]*VERSION/, 'dry-run output must surface the VERSION gate');
  });
});

describe('T3 version-match gate third (SDK-VERSION-MISMATCH names both)', () => {
  it('line 1 is compared against the muse-native.yaml sdk: pin', () => {
    const body = apkBody(readScaffold());
    assert.ok(body.includes('sdk:'), 'match check must read the sdk: pin from muse-native.yaml');
    assert.match(body, /sed\s+-n\s+['"]?1p['"]?/,
      'match check must read VERSION line 1');
  });

  it('mismatch error names both the expected and the found version', () => {
    const body = apkBody(readScaffold());
    const mismatchLines = body.split('\n').filter((l) => /[Mm]ismatch|expected.*found|sdk_pin/.test(l));
    assert.ok(mismatchLines.length >= 1, 'a mismatch branch must exist');
    const branch = mismatchLines.join('\n');
    assert.ok(branch.includes('sdk_pin') || /found/.test(branch),
      'mismatch error must print the found sdk: value');
    assert.ok(branch.includes('sdk_ver') || /expected/.test(branch),
      'mismatch error must print the expected VERSION line-1 value');
  });

  it('version-match check sits between VERSION-present and per-ABI gates', () => {
    const text = readScaffold();
    const matchIdx = Math.max(text.indexOf('sdk_pin'), text.indexOf('SDK-VERSION-MISMATCH'));
    const abiIdx = text.indexOf('test -s');
    assert.ok(matchIdx !== -1 && abiIdx !== -1, 'both gates must exist');
    assert.ok(matchIdx < abiIdx, 'version-match must precede the per-ABI test -s gates');
  });

  it('mismatch fails closed (recipe exits non-zero)', () => {
    const body = apkBody(readScaffold());
    const idx = Math.max(body.indexOf('sdk_pin'), body.indexOf('MISMATCH'));
    const tail = body.slice(idx, idx + 600);
    assert.match(tail, /exit\s+1/, 'version mismatch must stop the build non-zero');
  });
});

describe('T3 per-ABI test -s gates last (SDK-ABI-MISSING)', () => {
  it('every ABI slot is gated with test -s on a MUSE_NATIVE_HOME-rooted lib', () => {
    const body = apkBody(readScaffold());
    for (const abi of ABIS) {
      const gateRe = new RegExp(`test\\s+-s\\s+[^\\n]*MUSE_NATIVE_HOME[^\\n]*${abi.replace('+', '\\+')}[^\\n]*libRuntime\\.so`);
      assert.match(body, gateRe, `slot ${abi} must be gated with test -s on its libRuntime.so`);
    }
  });

  it('per-ABI failures name the ABI path and exit non-zero', () => {
    const body = apkBody(readScaffold());
    assert.match(body, /SDK-ABI-MISSING|missing engine lib/i, 'ABI gate must name the failure');
    const tail = body.slice(body.indexOf('test -s'));
    assert.match(tail, /exit\s+1/, 'missing/empty ABI lib must stop the build non-zero');
  });

  it('all gates precede compilation (aapt2/javac/d8 run last)', () => {
    const body = apkBody(readScaffold());
    const firstCompile = Math.min(
      ...['aapt2', 'javac', 'd8'].map((t) => (body.indexOf(t) === -1 ? Infinity : body.indexOf(t))),
    );
    assert.ok(firstCompile !== Infinity, 'apk must compile via aapt2/javac/d8');
    for (const gate of ['test -f', 'sdk_pin', 'test -s']) {
      assert.ok(body.indexOf(gate) !== -1 && body.indexOf(gate) < firstCompile,
        `gate ${gate} must run before any compilation step`);
    }
  });
});

describe('T3 no ../ SDK traversal (AC-023 slice)', () => {
  it('no ../../build or ../build traversal remains', () => {
    const text = readScaffold();
    assert.ok(!text.includes('../../'), 'scaffold Makefile must not contain ../../ traversal');
    assert.ok(!text.includes('../build'), 'scaffold Makefile must not traverse to ../build');
  });

  it('ENGINE_LIBS is rooted at $(MUSE_NATIVE_HOME)', () => {
    const text = readScaffold();
    const m = /ENGINE_LIBS\s*[:?]?=\s*(.*)/.exec(text);
    assert.ok(m, 'ENGINE_LIBS must be defined');
    assert.ok(m[1].includes('$(MUSE_NATIVE_HOME)'),
      'ENGINE_LIBS must be rooted at $(MUSE_NATIVE_HOME)');
    assert.ok(!m[1].includes('..'), 'ENGINE_LIBS must not traverse upward');
  });
});

describe('T3 dry-run success path (correct env → checks pass, flow proceeds)', () => {
  it('make -n apk with correct HOME exits 0 and surfaces gates + compile in order', () => {
    const home = sandboxHome({ versionLine1: '0.1.0' });
    const r = makeN(GALLERY_DIR, home);
    assert.equal(r.status, 0, `correct env must pass dry-run: ${r.stderr}`);
    const vIdx = r.stdout.indexOf('VERSION');
    const sIdx = r.stdout.indexOf('test -s');
    const cIdx = r.stdout.indexOf('aapt2');
    assert.ok(vIdx !== -1 && sIdx !== -1 && cIdx !== -1, 'dry-run must surface gates + compile');
    assert.ok(vIdx < sIdx && sIdx < cIdx, 'dry-run order must be VERSION → test -s → aapt2');
  });
});

describe('T3 negative control: gate order is machine-checked, not assumed', () => {
  it('reordered fixture text fails the order assertion (RED capability)', () => {
    const fixture = 'test -s x\nifndef MUSE_NATIVE_HOME\ntest -f VERSION\n';
    const homeIdx = fixture.indexOf('ifndef MUSE_NATIVE_HOME');
    assert.ok(!(homeIdx < fixture.indexOf('test -f') && homeIdx < fixture.indexOf('test -s')),
      'fixture sanity: this ordering is wrong');
    assert.ok(homeIdx > fixture.indexOf('test -s'),
      'the order assertion must catch a gate placed after test -s');
  });
});

describe('T3 malformed VERSION refused before compilation (S-C1)', () => {
  const GOOD = '0.1.0\nsha=abcdef date=2026-10-30T00:00:00Z\nabis=arm64-v8a armeabi-v7a x86_64 x86\n';
  const MALFORMED = [
    ['wrong line count (2 lines)', '0.1.0\nsha=abcdef date=2026-10-30T00:00:00Z\n'],
    ['wrong line count (4 lines)', `${GOOD}extra=1\n`],
    ['non-SemVer line 1', GOOD.replace('0.1.0', 'v0.1')],
    ['line 2 missing sha=+date', GOOD.replace('sha=abcdef date=2026-10-30T00:00:00Z', 'rev=abcdef')],
    ['line 3 missing abis=', GOOD.replace('abis=arm64-v8a armeabi-v7a x86_64 x86', 'archs=all')],
  ];

  function sandboxVersion(content) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sdk-home-'));
    sandboxes.push(dir);
    fs.writeFileSync(path.join(dir, 'VERSION'), content);
    for (const abi of ABIS) {
      fs.mkdirSync(path.join(dir, `android-${abi}`), { recursive: true });
      fs.writeFileSync(path.join(dir, `android-${abi}`, 'libRuntime.so'), 'ELF');
    }
    return dir;
  }

  function makeLive(dir, home) {
    return cp.spawnSync('make', ['apk'],
      { cwd: dir, encoding: 'utf8', env: { ...process.env, MUSE_NATIVE_HOME: home } });
  }

  it('wrong line count / non-SemVer line 1 / bad line 2 / bad line 3 fail closed before compile', () => {
    const body = apkBody(readScaffold());
    const firstCompile = Math.min(
      ...['aapt2', 'javac', 'd8'].map((t) => (body.indexOf(t) === -1 ? Infinity : body.indexOf(t))),
    );
    assert.ok(firstCompile !== Infinity, 'apk must compile via aapt2/javac/d8');
    for (const probe of ["print NR", "sed -n '1p'", "sed -n '2p'", "sed -n '3p'"]) {
      const idx = body.indexOf(probe);
      assert.ok(idx !== -1, `malformed gate ${probe} must exist`);
      assert.ok(idx < firstCompile, `malformed gate ${probe} must precede compilation`);
      assert.match(body.slice(idx, idx + 600), /exit\s+1/,
        `malformed gate ${probe} must stop the build non-zero`);
    }
    assert.match(body, /malformed/i, 'malformed gates must name the failure shape');
    for (const [label, content] of MALFORMED) {
      const home = sandboxVersion(content);
      const r = makeLive(GALLERY_DIR, home);
      const out = `${r.stdout}${r.stderr}`;
      assert.notEqual(r.status, 0, `${label}: malformed VERSION must fail the make check`);
      assert.match(out, /malformed/i, `${label}: failure must name the malformed shape`);
      assert.match(out, /VERSION/, `${label}: failure must name the VERSION file`);
      assert.ok(!/aapt2|javac|\bd8\b/.test(out),
        `${label}: compilation must never run on malformed VERSION`);
    }
  });
});

describe('T3 sdk_pin extraction strictness (S-C2)', () => {
  function extractPin(yamlText) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'yaml-pin-'));
    sandboxes.push(dir);
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), yamlText);
    const r = cp.spawnSync('sh', ['-c',
      "grep -E '^[[:space:]]*sdk:' muse-native.yaml 2>/dev/null"
      + " | head -n 1 | sed 's/^[[:space:]]*sdk:[[:space:]]*//;s/[[:space:]]*$//'"],
    { cwd: dir, encoding: 'utf8' });
    assert.equal(r.status, 0, 'extraction pipeline must run hermetically');
    return (r.stdout || '').replace(/\n$/, '');
  }

  it('affixed/range/commented sdk: values never equal the expected version', () => {
    const body = apkBody(readScaffold());
    assert.ok(body.includes('^[[:space:]]*sdk:'),
      'extraction must anchor on a line-start sdk: key (commented-out keys never match)');
    assert.ok(body.includes('[ "$$sdk_pin" != "$$sdk_ver" ]'),
      'comparison must be exact string inequality, never a range/pattern match');
    assert.match(body, /warning.*no 'sdk:' pin/i,
      'empty pin must warn instead of matching');
    const mismatchIdx = Math.max(body.indexOf('SDK version mismatch'), body.indexOf('$$sdk_pin'));
    assert.ok(mismatchIdx !== -1, 'a mismatch branch must exist');
    const branch = body.slice(mismatchIdx, mismatchIdx + 600);
    assert.ok(branch.includes('sdk_ver') || /expected/.test(branch),
      'mismatch must name the expected VERSION line-1 value');
    assert.ok(branch.includes('sdk_pin') || /found/.test(branch),
      'mismatch must name the found sdk: value');
    assert.match(branch, /exit\s+1/, 'mismatch must stop the build non-zero');
    for (const line of [
      'sdk: ^0.1.0',
      'sdk: ~0.1.0',
      'sdk: >=0.1.0',
      'sdk: 0.1.x',
      'sdk: v0.1.0',
      'sdk: 0.1.0 # comment',
      '# sdk: 0.1.0',
    ]) {
      const got = extractPin(`name: gallery\n${line}\n`);
      assert.notEqual(got, '0.1.0', `${line} must never extract as the expected version`);
    }
    assert.equal(extractPin('name: gallery\nsdk: 0.1.0\n'), '0.1.0',
      'exact pin still extracts verbatim');
    assert.equal(extractPin('name: gallery\n  sdk: 0.1.0\n'), '0.1.0',
      'indented exact pin still extracts verbatim');
  });
});

// --- BUG-SAMPLE-005 Batch 1 / T1+T2: flappy + muse-demo precondition gates ---
// Scope: FR-SAMPLE-007/009, FR-PACKAGING-008/009; AC-SAMPLE-016/018/022,
// AC-PACKAGING-020/021/022/023.
// Interface (plan T1 verbatim):
//   `make apk | make sign | make verify | make install | make clean` (run in
//   `examples/flappy`) → exit 0 on success; `install` with no `adb` device
//   fails closed naming the missing device; unset `MUSE_NATIVE_HOME` fails
//   before compilation naming the variable — none — make precondition surface,
//   no new callable
// Interface (plan T2 verbatim): same five make targets run in
// `examples/muse-demo` (identical fail-closed shape).
// Hermetic: Makefile-text assertions plus `make -n` dry runs against a sandbox
// SDK dir under os.tmpdir() (fixture VERSION + libRuntime.so files, local
// `make` binary only — dry-run executes no recipe, needs no NDK/SDK/device/
// network). No secrets, no .env, no live service. RED until the coder backfills
// each demo's Makefile (+ manifest/strings for the presence cases).
// ASSUMPTION A-D3 (contract genuinely silent): per-demo Makefiles carry the
// same HOME → VERSION-present → version-match → per-ABI gate order as the
// gallery Makefile (verbatim copy per plan Detail) — order assertions mirror T3.
const DEMOS_005 = ['flappy', 'muse-demo'];

function demoDir(demo) {
  return path.join(ROOT, 'examples', demo);
}

function readDemoScaffold(demo) {
  const p = path.join(ROOT, 'examples', demo, 'Makefile');
  assert.ok(fs.existsSync(p),
    `examples/${demo}/Makefile must exist (BUG-SAMPLE-005 T1/T2 own it; RED until the coder backfills it)`);
  return fs.readFileSync(p, 'utf8');
}

function demoApkBody(text, demo) {
  const lines = text.split('\n');
  const idx = lines.findIndex((l) => /^apk\s*:/.test(l));
  assert.ok(idx !== -1, `examples/${demo}/Makefile must define target: apk`);
  let end = lines.length;
  for (let i = idx + 1; i < lines.length; i += 1) {
    if (/^[A-Za-z0-9_.-]+\s*:(?![:=])/.test(lines[i]) && !/^\s/.test(lines[i])) { end = i; break; }
  }
  return lines.slice(idx, end).join('\n');
}

function demoMakeN(demo, home) {
  const env = { ...process.env };
  if (home === null) delete env.MUSE_NATIVE_HOME;
  else env.MUSE_NATIVE_HOME = home;
  return cp.spawnSync('make', ['-n', 'apk'], { cwd: demoDir(demo), encoding: 'utf8', env });
}

for (const DEMO of DEMOS_005) {
  describe(`BUG-SAMPLE-005 ${DEMO} HOME gate first (AC-PACKAGING-023)`, () => {
    it('ifndef MUSE_NATIVE_HOME errors naming the variable + example export', () => {
      const text = readDemoScaffold(DEMO);
      assert.match(text, /ifndef\s+MUSE_NATIVE_HOME/, 'check block must open with the HOME guard');
      assert.match(text, /\$\(error[^)]*MUSE_NATIVE_HOME[^)]*\)/, 'HOME gate must $(error) naming the variable');
      assert.ok(/export/.test(text), 'HOME error must show an example export');
    });

    it('HOME guard precedes every other SDK check', () => {
      const text = readDemoScaffold(DEMO);
      const homeIdx = text.indexOf('ifndef MUSE_NATIVE_HOME');
      assert.ok(homeIdx !== -1, 'HOME guard must exist');
      assert.ok(homeIdx < text.indexOf('VERSION'), 'HOME guard must precede the VERSION check');
      assert.ok(homeIdx < text.indexOf('test -s'), 'HOME guard must precede the per-ABI gates');
    });

    it('make -n apk with unset HOME fails closed naming the variable', () => {
      const r = demoMakeN(DEMO, null);
      assert.notEqual(r.status, 0, 'unset HOME must fail before compilation');
      assert.match(r.stdout + r.stderr, /MUSE_NATIVE_HOME/, 'failure must name the variable');
    });
  });

  describe(`BUG-SAMPLE-005 ${DEMO} VERSION + match + per-ABI gates (AC-PACKAGING-020/023)`, () => {
    it('apk recipe gates on test -f $(MUSE_NATIVE_HOME)/VERSION before compilation', () => {
      const body = demoApkBody(readDemoScaffold(DEMO), DEMO);
      assert.match(body, /test\s+-f[^;\n]*MUSE_NATIVE_HOME[^;\n]*\/VERSION/,
        'apk must gate on test -f $(MUSE_NATIVE_HOME)/VERSION');
      assert.match(body, /exit\s+1/, 'VERSION gate must exit non-zero on failure');
    });

    it('line 1 is compared against the muse-native.yaml sdk: pin; mismatch names both', () => {
      const body = demoApkBody(readDemoScaffold(DEMO), DEMO);
      assert.ok(body.includes('sdk:'), 'match check must read the sdk: pin from muse-native.yaml');
      const idx = Math.max(body.indexOf('sdk_pin'), body.indexOf('MISMATCH'));
      assert.ok(idx !== -1, 'a mismatch branch must exist');
      assert.match(body.slice(idx, idx + 600), /exit\s+1/, 'version mismatch must stop the build non-zero');
    });

    it('every ABI slot is gated with test -s on a MUSE_NATIVE_HOME-rooted lib', () => {
      const body = demoApkBody(readDemoScaffold(DEMO), DEMO);
      for (const abi of ABIS) {
        const gateRe = new RegExp(`test\\s+-s\\s+[^\\n]*MUSE_NATIVE_HOME[^\\n]*${abi.replace('+', '\\+')}[^\\n]*libRuntime\\.so`);
        assert.match(body, gateRe, `slot ${abi} must be gated with test -s on its libRuntime.so`);
      }
    });

    it('no ../../build or ../build traversal remains; ENGINE_LIBS rooted at $(MUSE_NATIVE_HOME)', () => {
      const text = readDemoScaffold(DEMO);
      assert.ok(!text.includes('../../'), 'demo Makefile must not contain ../../ traversal');
      assert.ok(!text.includes('../build'), 'demo Makefile must not traverse to ../build');
      const m = /ENGINE_LIBS\s*[:?]?=\s*(.*)/.exec(text);
      assert.ok(m, 'ENGINE_LIBS must be defined');
      assert.ok(m[1].includes('$(MUSE_NATIVE_HOME)'),
        'ENGINE_LIBS must be rooted at $(MUSE_NATIVE_HOME)');
    });

    it('make -n apk with correct HOME exits 0 and surfaces gates + compile in order', () => {
      const home = sandboxHome({ versionLine1: '0.1.0' });
      const r = demoMakeN(DEMO, home);
      assert.equal(r.status, 0, `correct env must pass dry-run: ${r.stderr}`);
      const vIdx = r.stdout.indexOf('VERSION');
      const sIdx = r.stdout.indexOf('test -s');
      const cIdx = r.stdout.indexOf('aapt2');
      assert.ok(vIdx !== -1 && sIdx !== -1 && cIdx !== -1, 'dry-run must surface gates + compile');
      assert.ok(vIdx < sIdx && sIdx < cIdx, 'dry-run order must be VERSION → test -s → aapt2');
    });
  });

  describe(`BUG-SAMPLE-005 ${DEMO} manifest + strings presence (AC-SAMPLE-016/018/022)`, () => {
    it('AndroidManifest.xml exists with the per-demo identity', () => {
      const p = path.join(ROOT, 'examples', DEMO, 'AndroidManifest.xml');
      assert.ok(fs.existsSync(p), `examples/${DEMO}/AndroidManifest.xml must exist`);
      const xml = fs.readFileSync(p, 'utf8');
      assert.match(xml, /package="com\.muse\.host"/, 'manifest package must be com.muse.host');
      assert.match(xml, /android:minSdkVersion="26"/, 'manifest minSdk must be 26');
      assert.match(xml, /android:targetSdkVersion="34"/, 'manifest targetSdk must be 34');
    });

    it('res/values/strings.xml exists with a single demo app_name', () => {
      const p = path.join(ROOT, 'examples', DEMO, 'res', 'values', 'strings.xml');
      assert.ok(fs.existsSync(p), `examples/${DEMO}/res/values/strings.xml must exist`);
      const xml = fs.readFileSync(p, 'utf8');
      const names = [...xml.matchAll(/<string\s+name="([^"]*)"/g)].map((m) => m[1]);
      assert.deepEqual(names, ['app_name'], 'strings.xml must carry exactly one app_name string');
      assert.ok(!xml.includes('M3 UI Gallery'), 'demo app_name must differ from the gallery value');
    });
  });
}
