'use strict';
// Batch 1 / T2 (FR-PACKAGING-010, AC-PACKAGING-027).
// Scope: scripts/release-sdk.sh idempotent release job — stamps build/VERSION
// from sdk.yaml, fail-closed on bad version / dirty tree / missing ABI lib,
// --check warns and --strict fails on engine-changed/version-unchanged.
// T1 (sdk.yaml data file) needs no test file: it declares
// `none — data file, read by T2 via grep`; the grep acceptance is tester-run.
// Must RED until scripts/release-sdk.sh exists (missing script fails setup).
// Hermetic: node:test + node:assert/strict + stdlib only. Each behavior case
// builds its own sandbox repo under os.tmpdir() (sdk.yaml fixture +
// build/android-<abi>/libRuntime.so fixtures + local `git init`/commit via
// -c user.name/email, no network, no global config), copies the real script
// text into the sandbox so root resolution works, and drives it with
// `sh` + `sha256sum` from the local toolchain only. No secrets, no .env,
// no live service, no writes outside the sandbox except reading the script.
// ASSUMPTION A-R1 (contract genuinely silent): ABI dirs are
// `build/android-<abi>/libRuntime.so` for
// arm64-v8a, armeabi-v7a, x86_64, x86 (plan T2 detail; spec AC-025 names the
// same four with the `android-` prefix); the failure names the ABI path.
// ASSUMPTION A-R2: staleness signal is git-based (plan A3) — any tracked-file
// change versus HEAD with sdk.yaml version unchanged; --check/--strict cases
// pass --allow-dirty so the clean-tree gate does not mask the signal.
// ASSUMPTION A-R3: `sha=` is the hex sha256 of the written build/VERSION file
// and stdout carries `version=<ver>`, `sha=<64hex>`, `sizes=`; VERSION line 2
// is `sha=<full sha> date=<UTC ISO-8601>`, line 3 is the fixed ABI list.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');

const ABIS = ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'];

function repoRoot() {
  let dir = __dirname;
  for (let i = 0; i < 8; i += 1) {
    if (fs.existsSync(path.join(dir, 'scripts', 'build-sample.sh'))) return dir;
    dir = path.dirname(dir);
  }
  throw new Error('test setup: cannot locate repo root from ' + __dirname);
}

const ROOT = repoRoot();
const SCRIPT = path.join(ROOT, 'scripts', 'release-sdk.sh');

const sandboxes = [];
afterEach(() => {
  while (sandboxes.length) {
    const dir = sandboxes.pop();
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

function git(dir, ...args) {
  const r = cp.spawnSync('git', ['-c', 'user.name=test', '-c', 'user.email=test@test.invalid', ...args],
    { cwd: dir, encoding: 'utf8' });
  assert.equal(r.status, 0, `git ${args.join(' ')} failed: ${r.stderr}`);
}

function writeFile(dir, rel, content) {
  const p = path.join(dir, rel);
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, content);
}

// Builds a sandbox repo containing the REAL script text. Options:
//   versionLine: full sdk.yaml content (default 'version: 0.1.0\n');
//     null omits sdk.yaml. libs: map abi->bytes (default 3 bytes each);
//     null value omits that lib; empty string writes an empty lib.
//   commit: git init+commit everything (clean tree) when true.
//   extraFiles: { rel: content } seeded before commit.
function sandbox({ versionLine = 'version: 0.1.0\n', libs = {}, extraFiles = {}, commit = true } = {}) {
  assert.ok(fs.existsSync(SCRIPT), `expected release job to exist: ${path.relative(ROOT, SCRIPT)}`);
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'release-sdk-'));
  sandboxes.push(dir);
  const text = fs.readFileSync(SCRIPT, 'utf8');
  writeFile(dir, 'scripts/release-sdk.sh', text);
  fs.chmodSync(path.join(dir, 'scripts', 'release-sdk.sh'), 0o755);
  if (versionLine !== null) writeFile(dir, 'sdk.yaml', versionLine);
  for (const abi of ABIS) {
    const bytes = Object.hasOwn(libs, abi) ? libs[abi] : 'ELF';
    if (bytes === null) continue;
    writeFile(dir, `build/android-${abi}/libRuntime.so`, bytes);
  }
  for (const [rel, content] of Object.entries(extraFiles)) writeFile(dir, rel, content);
  if (commit) {
    git(dir, 'init', '-q');
    git(dir, 'add', '-A');
    git(dir, 'commit', '-qm', 'fixture');
  }
  return dir;
}

function run(dir, args = [], env = {}) {
  return cp.spawnSync('sh', [path.join(dir, 'scripts', 'release-sdk.sh'), ...args], {
    cwd: dir,
    encoding: 'utf8',
    env: { ...process.env, ...env },
  });
}

function versionLines(dir) {
  const p = path.join(dir, 'build', 'VERSION');
  assert.ok(fs.existsSync(p), 'expected build/VERSION to be written');
  return fs.readFileSync(p, 'utf8').split('\n');
}

describe('release-sdk.sh presence and usage', () => {
  it('script exists and is executable', () => {
    assert.ok(fs.existsSync(SCRIPT), 'scripts/release-sdk.sh must exist');
    const mode = fs.statSync(SCRIPT).mode & 0o111;
    assert.ok(mode !== 0, 'scripts/release-sdk.sh must be executable');
  });

  it('passes `sh -n` syntax check', () => {
    assert.ok(fs.existsSync(SCRIPT), 'scripts/release-sdk.sh must exist');
    const r = cp.spawnSync('sh', ['-n', SCRIPT], { encoding: 'utf8' });
    assert.equal(r.status, 0, `sh -n failed: ${r.stderr}`);
  });

  it('--help exits 0 and names the flags', () => {
    assert.ok(fs.existsSync(SCRIPT), 'scripts/release-sdk.sh must exist');
    const r = cp.spawnSync('sh', [SCRIPT, '--help'], { encoding: 'utf8' });
    assert.equal(r.status, 0, `--help failed: ${r.stderr}`);
    for (const flag of ['--allow-dirty', '--check', '--strict']) {
      assert.match(r.stdout, new RegExp(flag.replace(/-/g, '\\-')), `--help must name ${flag}`);
    }
  });
});

describe('sdk.yaml version gate (SDK-VERSION-MISSING)', () => {
  it('missing sdk.yaml fails naming SDK-VERSION-MISSING', () => {
    const dir = sandbox({ versionLine: null });
    const r = run(dir);
    assert.notEqual(r.status, 0, 'must fail without sdk.yaml');
    assert.match(r.stdout + r.stderr, /SDK-VERSION-MISSING/);
  });

  it('bad version line fails naming SDK-VERSION-MISSING', () => {
    for (const bad of ['version: 0.1\n', 'version: latest\n', '# version: 0.1.0\n', '']) {
      const dir = sandbox({ versionLine: bad });
      const r = run(dir);
      assert.notEqual(r.status, 0, `must fail for sdk.yaml ${JSON.stringify(bad)}`);
      assert.match(r.stdout + r.stderr, /SDK-VERSION-MISSING/, `must name cause for ${JSON.stringify(bad)}`);
    }
  });
});

describe('clean-tree gate', () => {
  it('dirty tree without --allow-dirty fails', () => {
    const dir = sandbox();
    fs.writeFileSync(path.join(dir, 'sdk.yaml'), 'version: 0.1.0\n# dirty\n');
    const r = run(dir);
    assert.notEqual(r.status, 0, 'must fail on dirty tree without --allow-dirty');
    assert.match(r.stdout + r.stderr, /dirty/i);
  });

  it('dirty tree with --allow-dirty proceeds to success', () => {
    const dir = sandbox();
    fs.writeFileSync(path.join(dir, 'sdk.yaml'), 'version: 0.1.0\n# dirty\n');
    const r = run(dir, ['--allow-dirty']);
    assert.equal(r.status, 0, `must succeed with --allow-dirty: ${r.stdout} ${r.stderr}`);
  });
});

describe('4-ABI ship gate (SDK-ABI-MISSING)', () => {
  it('missing ABI lib fails naming SDK-ABI-MISSING and the ABI path', () => {
    const dir = sandbox({ libs: { 'arm64-v8a': null }, commit: false });
    git(dir, 'init', '-q');
    git(dir, 'add', '-A');
    git(dir, 'commit', '-qm', 'fixture');
    fs.rmSync(path.join(dir, 'build', 'android-arm64-v8a', 'libRuntime.so'), { force: true });
    const r = run(dir, ['--allow-dirty']);
    assert.notEqual(r.status, 0, 'must fail with a missing ABI lib');
    assert.match(r.stdout + r.stderr, /SDK-ABI-MISSING/);
    assert.match(r.stdout + r.stderr, /arm64-v8a/);
  });

  it('empty ABI lib fails naming SDK-ABI-MISSING', () => {
    const dir = sandbox({ libs: { 'x86_64': '' }, commit: false });
    git(dir, 'init', '-q');
    git(dir, 'add', '-A');
    git(dir, 'commit', '-qm', 'fixture');
    const r = run(dir, ['--allow-dirty']);
    assert.notEqual(r.status, 0, 'must fail with an empty ABI lib');
    assert.match(r.stdout + r.stderr, /SDK-ABI-MISSING/);
  });
});

describe('success shape and idempotency', () => {
  it('clean tree + 4 libs exits 0 with 3-line VERSION and proof line', () => {
    const dir = sandbox();
    const r = run(dir);
    assert.equal(r.status, 0, `must succeed: ${r.stdout} ${r.stderr}`);
    const lines = versionLines(dir);
    assert.equal(lines[0], '0.1.0');
    assert.match(lines[1], /^sha=[0-9a-f]{64} date=\S+/);
    assert.equal(lines[2], 'abis=arm64-v8a armeabi-v7a x86_64 x86');
    assert.match(r.stdout, /version=0\.1\.0/);
    assert.match(r.stdout, /sha=[0-9a-f]{64}/);
    assert.match(r.stdout, /sizes=/);
  });

  it('re-run regenerates identical line 1', () => {
    const dir = sandbox();
    assert.equal(run(dir).status, 0);
    const first = versionLines(dir)[0];
    assert.equal(run(dir, ['--allow-dirty']).status, 0);
    assert.equal(versionLines(dir)[0], first);
  });
});

describe('staleness signal (--check warns, --strict fails)', () => {
  it('--check warns (exit 0) when tracked bytes changed but version did not', () => {
    const dir = sandbox({ extraFiles: { 'engine/runtime/src/jni_bridge.cpp': '// v1\n' } });
    fs.writeFileSync(path.join(dir, 'engine', 'runtime', 'src', 'jni_bridge.cpp'), '// v2\n');
    const r = run(dir, ['--allow-dirty', '--check']);
    assert.equal(r.status, 0, `--check must warn, not fail: ${r.stdout} ${r.stderr}`);
    assert.match(r.stdout + r.stderr, /warn/i);
  });

  it('--strict fails when tracked bytes changed but version did not', () => {
    const dir = sandbox({ extraFiles: { 'engine/runtime/src/jni_bridge.cpp': '// v1\n' } });
    fs.writeFileSync(path.join(dir, 'engine', 'runtime', 'src', 'jni_bridge.cpp'), '// v2\n');
    const r = run(dir, ['--allow-dirty', '--strict']);
    assert.notEqual(r.status, 0, '--strict must fail on unchanged version over changed bytes');
  });
});
