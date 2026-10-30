'use strict';
// Batch 1 / T1 (FR-PACKAGING-009, AC-PACKAGING-024 source side).
// Scope: repo-root sdk.yaml strict SemVer version source + read_sdk_version.
// Interface (plan T1 verbatim):
//   `read_sdk_version(path: string): string` — returns `MAJOR.MINOR.PATCH`;
//   errors: `missing-file`, `missing-version-key`, `bad-semver`
//   (each names path + cause).
// Grep contract: `^version: [0-9]+\.[0-9]+\.[0-9]+$`.
// Hermetic: node:test + node:assert/strict + stdlib only. Files live in
// fs.mkdtemp dirs; tool invocation is stubbed by monkey-patching
// `node:child_process` in-process — no network, no secrets, no live services.
// Secrets: none used (dummy placeholder only where a negative-control needs
// a non-version payload; never a real credential).
// ASSUMPTION A-SDK1 (contract genuinely silent): the plan names the function
// signature but not the module path; tests import it from the CLI boundary
// `packages/cli/lib/sdk-version.js` (sibling of `lib/config.js`), exporting
// `read_sdk_version`. A coder-owned equivalent path keeps this contract only
// if the tests' require is re-published with it (GR-08 T1.10 dispute, not a
// silent follow).
// ASSUMPTION A-SDK2: error codes surface as `err.code` equal to
// `missing-file` / `missing-version-key` / `bad-semver`, or — when the coder
// uses plain Errors — as those tokens inside the message. Every error names
// the offending path. Tests accept either shape but require path + cause.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function repoRoot() {
  let dir = __dirname;
  for (let i = 0; i < 8; i += 1) {
    if (fs.existsSync(path.join(dir, 'package.json'))
      && fs.existsSync(path.join(dir, 'packages', 'cli', 'lib', 'config.js'))) return dir;
    dir = path.dirname(dir);
  }
  throw new Error('test setup: cannot locate repo root from ' + __dirname);
}

const ROOT = repoRoot();
const SDK_YAML = path.join(ROOT, 'sdk.yaml');
const SDK_LIB = path.join(ROOT, 'packages', 'cli', 'lib', 'sdk-version.js');

function requireSdkVersion() {
  assert.ok(fs.existsSync(SDK_LIB),
    'lib/sdk-version.js must exist (T1 owns it; RED until the coder adds it)');
  delete require.cache[require.resolve(SDK_LIB)];
  const mod = require(SDK_LIB);
  assert.equal(typeof mod.read_sdk_version, 'function',
    'sdk-version module must export function read_sdk_version');
  return mod.read_sdk_version;
}

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
function writeTmp(dir, name, content) {
  const p = path.join(dir, name);
  fs.writeFileSync(p, content, 'utf8');
  return p;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

// --- hermetic child_process stub: proves these tests shell out to nothing --
let stubCalls = [];
let stubOriginals = {};
function stubTools() {
  const cp = require('node:child_process');
  stubCalls = [];
  stubOriginals = {};
  for (const key of ['execFile', 'execFileSync', 'spawnSync', 'exec', 'execSync', 'spawn']) {
    if (typeof cp[key] === 'function') stubOriginals[key] = cp[key];
  }
  const record = (tool) => stubCalls.push(tool);
  cp.execFileSync = () => { record('execFileSync'); throw new Error('hermetic: no tool calls allowed'); };
  cp.execSync = () => { record('execSync'); throw new Error('hermetic: no tool calls allowed'); };
  cp.spawnSync = () => { record('spawnSync'); throw new Error('hermetic: no tool calls allowed'); };
}
function restoreTools() {
  const cp = require('node:child_process');
  for (const key of Object.keys(stubOriginals)) cp[key] = stubOriginals[key];
  stubOriginals = {};
}
beforeEach(() => stubTools());
afterEach(() => restoreTools());

function assertNamesPathAndCause(err, filePath, code) {
  assert.match(String(err && err.message), /./, 'error must carry a message');
  assert.ok(String(err.message).includes(filePath),
    `error must name the offending path: ${filePath}\nactual: ${err.message}`);
  const codeInMessage = String(err.message).includes(code);
  const codeInProp = err && err.code === code;
  assert.ok(codeInProp || codeInMessage,
    `error must carry cause token '${code}' via err.code or message\nactual code=${err && err.code} message=${err.message}`);
}

describe('T1 sdk.yaml exists with strict SemVer (AC-PACKAGING-024 source side)', () => {
  it('sdk.yaml exists at the repo root', () => {
    assert.ok(fs.existsSync(SDK_YAML),
      'sdk.yaml must exist at the repo root (T1 owns it; RED until the coder adds it)');
  });

  it("sdk.yaml matches grep contract ^version: [0-9]+\\.[0-9]+\\.[0-9]+$", () => {
    assert.ok(fs.existsSync(SDK_YAML), 'sdk.yaml must exist (see previous case)');
    const text = fs.readFileSync(SDK_YAML, 'utf8');
    assert.match(text, /^version: [0-9]+\.[0-9]+\.[0-9]+$/m,
      'sdk.yaml must contain a strict SemVer version line');
  });

  it('sdk.yaml carries exactly one version key', () => {
    assert.ok(fs.existsSync(SDK_YAML), 'sdk.yaml must exist (see previous case)');
    const lines = fs.readFileSync(SDK_YAML, 'utf8').split('\n')
      .filter((l) => /^\s*version\s*:/.test(l));
    assert.equal(lines.length, 1, `expected exactly one version key, found ${lines.length}`);
  });

  // DISPUTE RESOLUTION 2026-10-30 (FR-PACKAGING-009 Notes): the rule "No
  // muse-native.yaml ships in the SDK" governs the shipped build/ artifact,
  // not the repo-root dev sample (FR-MUSE-002 sample config, commit 6923fe0).
  // Root assertion dropped; build/ case below is the normative check.

  it('no muse-native.yaml ships in build/', () => {
    assert.ok(!fs.existsSync(path.join(ROOT, 'build', 'muse-native.yaml')),
      'muse-native.yaml must not ship in build/');
  });
});

describe('T1 read_sdk_version happy path', () => {
  it('returns MAJOR.MINOR.PATCH for a valid file', () => {
    const read_sdk_version = requireSdkVersion();
    const dir = mkTmp('muse-sdk-ver-');
    const p = writeTmp(dir, 'sdk.yaml', 'version: 1.2.3\n');
    assert.equal(read_sdk_version(p), '1.2.3');
  });

  it('returns multi-digit components (e.g. 10.20.30)', () => {
    const read_sdk_version = requireSdkVersion();
    const dir = mkTmp('muse-sdk-ver-');
    const p = writeTmp(dir, 'sdk.yaml', 'version: 10.20.30\n');
    assert.equal(read_sdk_version(p), '10.20.30');
  });

  it('tolerates surrounding keys/comments when the version line is strict', () => {
    const read_sdk_version = requireSdkVersion();
    const dir = mkTmp('muse-sdk-ver-');
    const p = writeTmp(dir, 'sdk.yaml', '# sdk config\nversion: 0.1.0\n');
    assert.equal(read_sdk_version(p), '0.1.0');
  });
});

describe('T1 read_sdk_version missing-file', () => {
  it('absent path fails with missing-file naming the path', () => {
    const read_sdk_version = requireSdkVersion();
    const missing = path.join(mkTmp('muse-sdk-ver-'), 'no-such-sdk.yaml');
    assert.throws(() => read_sdk_version(missing),
      (err) => { assertNamesPathAndCause(err, missing, 'missing-file'); return true; });
  });

  it('directory path fails naming the path (never returns a version)', () => {
    const read_sdk_version = requireSdkVersion();
    const dir = mkTmp('muse-sdk-ver-');
    assert.throws(() => read_sdk_version(dir),
      (err) => {
        assert.ok(String(err.message).includes(dir), 'error must name the offending path');
        return true;
      });
  });
});

describe('T1 read_sdk_version missing-version-key', () => {
  it('file without a version key fails with missing-version-key naming the path', () => {
    const read_sdk_version = requireSdkVersion();
    const dir = mkTmp('muse-sdk-ver-');
    const p = writeTmp(dir, 'sdk.yaml', '# empty config\nname: sdk\n');
    assert.throws(() => read_sdk_version(p),
      (err) => { assertNamesPathAndCause(err, p, 'missing-version-key'); return true; });
  });

  it('empty version value fails (missing-version-key or bad-semver) naming the path', () => {
    const read_sdk_version = requireSdkVersion();
    const dir = mkTmp('muse-sdk-ver-');
    const p = writeTmp(dir, 'sdk.yaml', 'version:\n');
    assert.throws(() => read_sdk_version(p),
      (err) => {
        assert.ok(String(err.message).includes(p), 'error must name the offending path');
        const coded = (err.code === 'missing-version-key' || err.code === 'bad-semver')
          || /missing-version-key|bad-semver/.test(String(err.message));
        assert.ok(coded, `error must carry a version cause token\nactual: ${err.message}`);
        return true;
      });
  });

  it('commented-out version key counts as missing-version-key', () => {
    const read_sdk_version = requireSdkVersion();
    const dir = mkTmp('muse-sdk-ver-');
    const p = writeTmp(dir, 'sdk.yaml', '# version: 1.2.3\n');
    assert.throws(() => read_sdk_version(p),
      (err) => { assertNamesPathAndCause(err, p, 'missing-version-key'); return true; });
  });
});

describe('T1 read_sdk_version bad-semver', () => {
  const badCases = [
    ['leading v', 'version: v1.2.3\n'],
    ['two components', 'version: 1.2\n'],
    ['four components', 'version: 1.2.3.4\n'],
    ['range caret', 'version: ^1.2.3\n'],
    ['range tilde', 'version: ~1.2.3\n'],
    ['wildcard', 'version: 1.2.x\n'],
    ['prerelease suffix', 'version: 1.2.3-alpha\n'],
    ['build metadata', 'version: 1.2.3+build\n'],
    ['trailing whitespace payload', 'version: 1.2.3 extra\n'],
    ['empty', 'version: \n'],
    ['quoted strict still strict but quoted is not the contract', 'version: "1.2.3"\n'],
  ];
  for (const [label, content] of badCases) {
    it(`${label} fails with bad-semver naming path + value (${JSON.stringify(content).slice(0, 48)})`, () => {
      const read_sdk_version = requireSdkVersion();
      const dir = mkTmp('muse-sdk-ver-');
      const p = writeTmp(dir, 'sdk.yaml', content);
      assert.throws(() => read_sdk_version(p),
        (err) => { assertNamesPathAndCause(err, p, 'bad-semver'); return true; });
    });
  }

  it('newline-injection payload after a valid line is rejected (bad-semver)', () => {
    const read_sdk_version = requireSdkVersion();
    const dir = mkTmp('muse-sdk-ver-');
    const p = writeTmp(dir, 'sdk.yaml', 'version: 1.2.3\nmalicious: true\n');
    // A strict single-line reader either returns 1.2.3 (ignoring extras) or
    // rejects; it must never return the injected payload as the version.
    let out;
    try {
      out = read_sdk_version(p);
    } catch (err) {
      assertNamesPathAndCause(err, p, 'bad-semver');
      return;
    }
    assert.equal(out, '1.2.3', 'must return exactly the strict version, never injected content');
  });
});

describe('T1 security: errors name cause without leaking unrelated content', () => {
  it('bad-semver message names the offending value, not the whole file', () => {
    const read_sdk_version = requireSdkVersion();
    const dir = mkTmp('muse-sdk-ver-');
    const secret = 'dummy-token-not-real-abc123';
    const p = writeTmp(dir, 'sdk.yaml', `version: nope\nnote: ${secret}\n`);
    assert.throws(() => read_sdk_version(p),
      (err) => {
        assertNamesPathAndCause(err, p, 'bad-semver');
        assert.ok(!String(err.message).includes(secret),
          'error must not echo unrelated file content (no secret leak)');
        return true;
      });
  });

  it('missing-version-key message does not echo unrelated keys', () => {
    const read_sdk_version = requireSdkVersion();
    const dir = mkTmp('muse-sdk-ver-');
    const secret = 'dummy-key-not-real-xyz789';
    const p = writeTmp(dir, 'sdk.yaml', `other: ${secret}\n`);
    assert.throws(() => read_sdk_version(p),
      (err) => {
        assertNamesPathAndCause(err, p, 'missing-version-key');
        assert.ok(!String(err.message).includes(secret),
          'error must not echo unrelated file content');
        return true;
      });
  });

  it('hermetic: zero external tool invocations', () => {
    assert.equal(stubCalls.length, 0, 'T1 assertions must not shell out to any tool');
  });
});
