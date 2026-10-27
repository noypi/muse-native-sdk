'use strict';
// T1 — ELF 16 KB alignment gate (AC-BUILD-016; required dep AC-BUILD-061 flag provenance).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Builds fake release trees in os.tmpdir() with per-.so alignment marker
// files; the gate reads markers, not real ELF parsing.
// Red state: ./verifyElfAlignment.js does not exist yet, so every case below
// fails (MODULE_NOT_FOUND) until the coder lands the gate.
//
// Interface under test (plan verbatim):
//   verifyElfAlignment(rootDir: string): { ok: boolean, violations: string[] } — violations name each `.so` whose LOAD segments are not 16 KB-aligned; missing root is a violation, never a throw.
//
// ASSUMPTION (marker-file convention — the coder owns the exact filenames;
// the tests below pin the layout so the gate is implementable):
//   <root>/lib/<abi>/<name>.so            fake shared object (text content ok)
//   <root>/lib/<abi>/<name>.so.align      alignment marker: text "16384" means
//                                         16 KB-aligned, anything else (e.g.
//                                         "4096") means misaligned; a missing
//                                         marker counts as misaligned.
// A tree with every .so marked "16384" yields ok:true; each misaligned .so is
// named in violations; a missing root yields { ok:false, violations:[...] }
// without throwing.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function loadImpl() {
  return require('./verifyElfAlignment.js');
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
}

);
function writeSo(root, abi, name, align) {
  const soRel = path.join('lib', abi, `${name}.so`);
  const full = path.join(root, soRel);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, 'FAKE-ELF');
  if (align !== null) {
    fs.writeFileSync(`${full}.align`, String(align));
  }
  return full;
}

describe('verifyElfAlignment', () => {
  it('aligned tree yields ok:true with no violations', () => {
    const root = mkTmp('elf-align-clean-');
    writeSo(root, 'arm64-v8a', 'libmuse', '16384');
    writeSo(root, 'armeabi-v7a', 'libmuse', '16384');
    const { verifyElfAlignment } = loadImpl();
    assert.deepEqual(verifyElfAlignment(root), { ok: true, violations: [] });
  });

  it('one misaligned .so yields ok:false naming that file', () => {
    const root = mkTmp('elf-align-onebad-');
    writeSo(root, 'arm64-v8a', 'libmuse', '16384');
    writeSo(root, 'arm64-v8a', 'libbad', '4096');
    const { verifyElfAlignment } = loadImpl();
    const result = verifyElfAlignment(root);
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => v.includes('libbad.so')),
      `expected violations to name libbad.so, got: ${JSON.stringify(result.violations)}`
    );
    assert.ok(
      !result.violations.some((v) => v.includes('libmuse.so')),
      `aligned libmuse.so must not be listed: ${JSON.stringify(result.violations)}`
    );
  });

  it('missing root is a violation, never a throw', () => {
    const missing = path.join(os.tmpdir(), 'elf-align-does-not-exist-xyz');
    const { verifyElfAlignment } = loadImpl();
    let result;
    assert.doesNotThrow(() => {
      result = verifyElfAlignment(missing);
    });
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.length > 0,
      'missing root must produce at least one violation'
    );
  });

  it('a .so with no marker file is a violation naming that file', () => {
    const root = mkTmp('elf-align-nomarker-');
    writeSo(root, 'arm64-v8a', 'libmuse', null);
    const { verifyElfAlignment } = loadImpl();
    const result = verifyElfAlignment(root);
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => v.includes('libmuse.so')),
      `expected violations to name libmuse.so, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('empty tree (no .so files) yields ok:true', () => {
    const root = mkTmp('elf-align-empty-');
    fs.mkdirSync(path.join(root, 'lib', 'arm64-v8a'), { recursive: true });
    const { verifyElfAlignment } = loadImpl();
    assert.deepEqual(verifyElfAlignment(root), { ok: true, violations: [] });
  });
});
