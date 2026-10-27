'use strict';
// T6 — unstripped-artifact retention gate (NFR-BUILD-016 / AC-BUILD-058).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Builds tmp dist trees under os.tmpdir(); no network, no secrets, no live
// services, never real binaries (all .so entries are text markers).
// Red state: ../symbolRetention.js does not exist yet, so every gate case
// below fails (MODULE_NOT_FOUND) until T6 lands; the STRIPMAP placeholder case
// fails (ENOENT) until `dist/symbols/STRIPMAP.md` lands.
//
// Interface under test (plan verbatim):
//   verifySymbolRetention(distDir: string): { ok: boolean, violations: string[] }
// Requires every shipped stripped .so to have a retained unstripped counterpart
// plus its strip map; a stripped-only layout returns `ok: false` naming the
// orphan artifact.
//
// ASSUMPTION (dist layout — the coder owns the exact filenames; the tests pin
// the layout so the gate is implementable):
//   <dist>/lib/<abi>/libmuse.so                  shipped stripped marker
//   <dist>/symbols/unstripped/<abi>/libmuse.so    retained unstripped counterpart
//   <dist>/symbols/STRIPMAP.md                    strip map: one
//                                                `<stripped> -> <unstripped>`
//                                                mapping line per shipped .so
// `dist/symbols/` is the retained-artifact layout the gate verifies
// (TC-BUILD-058 intent); `STRIPMAP.md` is the committed layout placeholder
// documenting the stripped→unstripped mapping format real builds populate.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ABI = 'arm64-v8a';
const SONAME = 'libmuse.so';
const STRIPPED_REL = path.join('lib', ABI, SONAME);
const UNSTRIPPED_REL = path.join('symbols', 'unstripped', ABI, SONAME);

function loadImpl() {
  return require('./symbolRetention.js');
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

function stripMapContent() {
  return (
    '# strip map — stripped -> unstripped\n' +
    `${STRIPPED_REL} -> ${UNSTRIPPED_REL}\n`
  );
}

// Builds a dist tree; flags toggle the retained counterpart and the map.
function mkDist({ withUnstripped = true, withMap = true, extraOrphanAbi = null } = {}) {
  const dist = mkTmp('symbol-retention-');
  writeFile(dist, STRIPPED_REL, 'STRIPPED\n');
  if (withUnstripped) writeFile(dist, UNSTRIPPED_REL, 'UNSTRIPPED\n');
  if (withMap) writeFile(dist, path.join('symbols', 'STRIPMAP.md'), stripMapContent());
  if (extraOrphanAbi) {
    writeFile(dist, path.join('lib', extraOrphanAbi, SONAME), 'STRIPPED\n');
  }
  return dist;
}

describe('T6 — verifySymbolRetention contract shape', () => {
  it('returns { ok: boolean, violations: string[] } on a complete tree', () => {
    const { verifySymbolRetention } = loadImpl();
    const dist = mkDist();
    const result = verifySymbolRetention(dist);
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T6 — complete retention tree passes', () => {
  it('stripped + unstripped + strip map passes', () => {
    const { verifySymbolRetention } = loadImpl();
    const dist = mkDist();
    const result = verifySymbolRetention(dist);
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T6 — stripped-only layout fails (TC-BUILD-058)', () => {
  it('a stripped .so with no retained unstripped counterpart fails naming the orphan', () => {
    const { verifySymbolRetention } = loadImpl();
    const dist = mkDist({ withUnstripped: false });
    const result = verifySymbolRetention(dist);
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
    const named = result.violations.some((v) => v.includes(SONAME));
    assert.ok(
      named,
      `a violation must name the orphan artifact, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a tree with no strip map fails naming the artifact or map', () => {
    // ASSUMPTION: the strip map is required evidence — its absence fails the
    // gate even when both .so markers are present.
    const { verifySymbolRetention } = loadImpl();
    const dist = mkDist({ withMap: false });
    const result = verifySymbolRetention(dist);
    assert.equal(result.ok, false);
    const named = result.violations.some(
      (v) => /stripmap/i.test(v) || v.includes(SONAME)
    );
    assert.ok(
      named,
      `a violation must name the missing map or artifact, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('one orphan ABI among complete ABIs still fails naming the orphan', () => {
    const { verifySymbolRetention } = loadImpl();
    const dist = mkDist({ extraOrphanAbi: 'x86_64' });
    const result = verifySymbolRetention(dist);
    assert.equal(result.ok, false);
    const named = result.violations.some(
      (v) => v.includes('x86_64') && v.includes(SONAME)
    );
    assert.ok(
      named,
      `a violation must name the orphan ABI artifact, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a missing distDir fails closed', () => {
    // ASSUMPTION: a gate that cannot read the dist tree fails closed.
    const { verifySymbolRetention } = loadImpl();
    const missing = path.join(os.tmpdir(), `symbol-retention-absent-${Date.now()}`);
    const result = verifySymbolRetention(missing);
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0, 'must report at least one violation');
  });
});

describe('T6 — committed STRIPMAP placeholder (TC-BUILD-058)', () => {
  it('dist/symbols/STRIPMAP.md exists and documents the stripped→unstripped mapping', () => {
    const placeholder = path.join(__dirname, '..', '..', 'dist', 'symbols', 'STRIPMAP.md');
    const content = fs.readFileSync(placeholder, 'utf8');
    assert.match(content, /stripped/i, 'placeholder must name the stripped side');
    assert.match(content, /unstripped/i, 'placeholder must name the unstripped side');
    assert.match(content, /->/, 'placeholder must document the mapping separator');
  });
});
