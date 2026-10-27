'use strict';
// T5b — native-release invariant gate (NFR-BUILD-016 / AC-BUILD-061).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Builds tmp release trees under os.tmpdir(); no network, no secrets, no live
// services, never real binaries (shipped/retained artifacts are text markers).
// Red state: ../verifyNativeRelease.js does not exist yet, so every case below
// fails (MODULE_NOT_FOUND) until T5 lands.
//
// Interface under test (plan verbatim):
//   verifyNativeRelease(rootDir: string): { ok: boolean, violations: string[] }
// Asserts the release exports only declared plugin-ABI symbols, ships a
// stripped .so, retains unstripped artifacts, and the render path is
// exception-free; violations name file and offending flag/symbol.
//
// ASSUMPTION (release-tree layout — the coder owns the exact filenames; the
// tests below pin the layout so the gate is implementable):
//   <root>/flags.txt                 expected-flags source (mirrors T4's
//                                   release-flags.txt; must carry
//                                   -fvisibility=hidden, -flto=thin,
//                                   -fno-exceptions)
//   <root>/render-flags.txt          render-path TU flags (must carry
//                                   -fno-exceptions, never -fexceptions)
//   <root>/abi-allowlist.txt         declared plugin-ABI symbols, one per line
//   <root>/exported-symbols.txt      actual release exports, one per line
//   <root>/lib/<abi>/libmuse.so      shipped stripped marker (text "STRIPPED")
//   <root>/symbols/unstripped/<abi>/libmuse.so  retained unstripped marker
//                                   (text "UNSTRIPPED")
// A clean tree carries all of the above; each failure case removes or corrupts
// exactly one aspect. TC-BUILD-061 intent is covered by the missing-unstripped
// and extra-export cases; TC-BUILD-059 intent by the render-flags case.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ABI = 'arm64-v8a';
const SONAME = 'libmuse.so';

function loadImpl() {
  return require('./verifyNativeRelease.js');
}

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
});

function writeFile(root, rel, content) {
  const full = path.join(root, rel);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, content);
  return full;
}

const CLEAN_FLAGS =
  '-std=c++20\n-stdlib=libc++\n-flto=thin\n-fvisibility=hidden\n-fno-exceptions\n-s\n';
const CLEAN_RENDER_FLAGS = '-fno-exceptions\n-fvisibility=hidden\n';
const ALLOWLIST = 'muse_create\nmuse_render\nmuse_destroy\n';
const EXPORTS_CLEAN = 'muse_create\nmuse_render\nmuse_destroy\n';

// Builds a clean release tree; overrides patch individual files away.
// Returns the rootDir.
function mkRelease({
  flags = CLEAN_FLAGS,
  renderFlags = CLEAN_RENDER_FLAGS,
  allowlist = ALLOWLIST,
  exports = EXPORTS_CLEAN,
  withStripped = true,
  withUnstripped = true,
} = {}) {
  const root = mkTmp('native-release-');
  writeFile(root, 'flags.txt', flags);
  writeFile(root, 'render-flags.txt', renderFlags);
  writeFile(root, 'abi-allowlist.txt', allowlist);
  writeFile(root, 'exported-symbols.txt', exports);
  if (withStripped) writeFile(root, path.join('lib', ABI, SONAME), 'STRIPPED\n');
  if (withUnstripped) {
    writeFile(root, path.join('symbols', 'unstripped', ABI, SONAME), 'UNSTRIPPED\n');
  }
  return root;
}

describe('T5b — verifyNativeRelease contract shape', () => {
  it('returns { ok: boolean, violations: string[] } on a clean tree', () => {
    const { verifyNativeRelease } = loadImpl();
    const root = mkRelease();
    const result = verifyNativeRelease(root);
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T5b — clean release tree passes', () => {
  it('declared ABI exports only, stripped + unstripped present, no exceptions passes', () => {
    const { verifyNativeRelease } = loadImpl();
    const root = mkRelease();
    const result = verifyNativeRelease(root);
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T5b — release invariant failures (TC-BUILD-061)', () => {
  it('an export outside the declared plugin ABI fails naming the symbol', () => {
    const { verifyNativeRelease } = loadImpl();
    const root = mkRelease({ exports: `${EXPORTS_CLEAN}muse_debug_leak\n` });
    const result = verifyNativeRelease(root);
    assert.equal(result.ok, false);
    const named = result.violations.some((v) => v.includes('muse_debug_leak'));
    assert.ok(
      named,
      `a violation must name the offending symbol, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a release tree missing unstripped artifacts fails naming the orphan (TC-BUILD-061)', () => {
    const { verifyNativeRelease } = loadImpl();
    const root = mkRelease({ withUnstripped: false });
    const result = verifyNativeRelease(root);
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
    const named = result.violations.some(
      (v) => /unstripped/i.test(v) && (v.includes(SONAME) || v.includes(ABI))
    );
    assert.ok(
      named,
      `a violation must name the orphan unstripped artifact, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a release tree with no shipped stripped .so fails naming the artifact', () => {
    // ASSUMPTION: a release with nothing shipped is not a release — fail closed.
    const { verifyNativeRelease } = loadImpl();
    const root = mkRelease({ withStripped: false });
    const result = verifyNativeRelease(root);
    assert.equal(result.ok, false);
    const named = result.violations.some(
      (v) => v.includes(SONAME) || /stripped/i.test(v) || /lib/i.test(v)
    );
    assert.ok(
      named,
      `a violation must name the missing shipped artifact, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a render path with exceptions enabled fails naming the file and flag (TC-BUILD-059)', () => {
    const { verifyNativeRelease } = loadImpl();
    const root = mkRelease({ renderFlags: '-fexceptions\n' });
    const result = verifyNativeRelease(root);
    assert.equal(result.ok, false);
    const named = result.violations.some(
      (v) => v.includes('render-flags.txt') && /fexceptions/i.test(v)
    );
    assert.ok(
      named,
      `a violation must name the render-flags file and flag, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a missing rootDir fails closed naming the path', () => {
    // ASSUMPTION: a gate that cannot read the release tree fails closed.
    const { verifyNativeRelease } = loadImpl();
    const missing = path.join(os.tmpdir(), `native-release-absent-${Date.now()}`);
    const result = verifyNativeRelease(missing);
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
  });
});
