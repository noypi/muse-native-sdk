'use strict';
// T5a — render-path exception-ban gate (NFR-BUILD-016 / AC-BUILD-059).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Builds tmp flags files under os.tmpdir(); no network, no secrets, no live services.
// Red state: ../verifyRenderPathFlags.js does not exist yet, so every case
// below fails (MODULE_NOT_FOUND) until T5 lands.
//
// Interface under test (plan verbatim):
//   verifyRenderPathFlags(flagsPath: string): { ok: boolean, violations: string[] }
// Fails any render-path translation unit enabling exceptions; violations name
// the file and the offending flag.
//
// ASSUMPTION (flags source): `flagsPath` points at a text flags manifest in the
// shape of T4's `toolchain/ndk/release-flags.txt` (one flag per line, `#`
// comments). The gate consumes T4's manifest as the expected-flags source: a
// clean render-path manifest carries `-fno-exceptions` and never carries an
// exceptions-enabling flag (`-fexceptions`, `-fobjc-exceptions`,
// `-fcxx-exceptions`, `-fseh-exceptions`). The coder owns the exact token
// match; fixtures use the canonical tokens below.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function loadImpl() {
  return require('./verifyRenderPathFlags.js');
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

function writeFlags(dir, name, content) {
  const full = path.join(dir, name);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, content);
  return full;
}

const CLEAN_FLAGS =
  '# clean render-path flags (mirrors T4 manifest)\n' +
  '-std=c++20\n' +
  '-stdlib=libc++\n' +
  '-flto=thin\n' +
  '-fvisibility=hidden\n' +
  '-fno-exceptions\n' +
  '-s\n';

describe('T5a — verifyRenderPathFlags contract shape', () => {
  it('returns { ok: boolean, violations: string[] } on a clean flags file', () => {
    const { verifyRenderPathFlags } = loadImpl();
    const dir = mkTmp('render-flags-');
    const flagsPath = writeFlags(dir, 'render-flags.txt', CLEAN_FLAGS);
    const result = verifyRenderPathFlags(flagsPath);
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T5a — clean render-path flags pass', () => {
  it('a flags file carrying -fno-exceptions and no enabling flag passes', () => {
    const { verifyRenderPathFlags } = loadImpl();
    const dir = mkTmp('render-flags-clean-');
    const flagsPath = writeFlags(dir, 'render-flags.txt', CLEAN_FLAGS);
    const result = verifyRenderPathFlags(flagsPath);
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T5a — exceptions-enabled render-path unit fails (TC-BUILD-059)', () => {
  it('a render-path unit with -fexceptions fails naming the file and flag', () => {
    const { verifyRenderPathFlags } = loadImpl();
    const dir = mkTmp('render-flags-exc-');
    const flagsPath = writeFlags(
      dir,
      'render-unit-flags.txt',
      '# render-path unit with exceptions enabled\n-fexceptions\n-fno-exceptions\n'
    );
    const result = verifyRenderPathFlags(flagsPath);
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
    const named = result.violations.some(
      (v) => v.includes('render-unit-flags.txt') && /fexceptions/i.test(v)
    );
    assert.ok(
      named,
      `a violation must name the file and the offending flag, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('an enabling flag buried among clean flags still fails', () => {
    // ASSUMPTION: the ban is per translation unit — one enabling token anywhere
    // in the file fails the gate even when -fno-exceptions is also present.
    const { verifyRenderPathFlags } = loadImpl();
    const dir = mkTmp('render-flags-buried-');
    const flagsPath = writeFlags(dir, 'render-flags.txt', `${CLEAN_FLAGS}-fexceptions\n`);
    const result = verifyRenderPathFlags(flagsPath);
    assert.equal(result.ok, false);
    const named = result.violations.some((v) => /fexceptions/i.test(v));
    assert.ok(
      named,
      `a violation must name the offending flag, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a missing flags file fails closed naming the expected path', () => {
    // ASSUMPTION: a gate that cannot read the flags source fails closed.
    const { verifyRenderPathFlags } = loadImpl();
    const dir = mkTmp('render-flags-missing-');
    const flagsPath = path.join(dir, 'no-such-flags.txt');
    const result = verifyRenderPathFlags(flagsPath);
    assert.equal(result.ok, false);
    const named = result.violations.some((v) => v.includes('no-such-flags.txt'));
    assert.ok(
      named,
      `a violation must name the missing flags file, got: ${JSON.stringify(result.violations)}`
    );
  });
});
