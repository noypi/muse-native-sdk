'use strict';
// Batch 1 / T1 — NDK r27 dual-ABI configure pin gate (NFR-RUNTIME-001 / AC-RUNTIME-025).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Builds tmp fixture trees under os.tmpdir(); no network, no secrets, no live
// services, never reads the real gradle/libs.versions.toml or ANDROID_NDK_HOME,
// never invokes cmake. The module under test owns all real-config access; the
// tests below inject tmp roots.
// Red state: ./verifyNdkPin.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND) until the coder lands T1.
//
// Interface under test (plan verbatim):
//   cmakeConfigure(abi: string): int — abi is exactly `arm64-v8a` or `x86_64`;
//   returns 0 on successful configure, non-zero otherwise; no other ABIs accepted.
//
// ASSUMPTION (fixture layout — the coder owns the exact repo filenames; the
// tests below pin the layout so the gate is implementable):
//   <root>/gradle/libs.versions.toml   version catalog carrying
//                                      ndkVersion = "27.2.12479018"
//   <root>/engine/runtime/cmake/android-ndk.cmake
//                                      Android configure include that references
//                                      the ndkVersion pin and sets
//                                      ANDROID_PLATFORM=android-29
// The pin cases pass <root> explicitly so fixtures stay hermetic; the ABI-only
// cmakeConfigure(abi) form is exercised with no fixture I/O.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const PIN = '27.2.12479018';

function loadImpl() {
  return require('./verifyNdkPin.js');
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

const CLEAN_CATALOG = `[versions]\nndkVersion = "${PIN}"\n`;
const CLEAN_NDK_CMAKE =
  '# T1 android-ndk configure include\n' +
  '# ndkVersion 27.2.12479018\n' +
  'set(ANDROID_PLATFORM android-29)\n';

// Builds a clean fixture tree; overrides patch individual files away.
function mkTree({ catalog = CLEAN_CATALOG, ndkCmake = CLEAN_NDK_CMAKE } = {}) {
  const root = mkTmp('ndk-pin-');
  if (catalog !== null) writeFile(root, 'gradle/libs.versions.toml', catalog);
  if (ndkCmake !== null) {
    writeFile(root, 'engine/runtime/cmake/android-ndk.cmake', ndkCmake);
  }
  return root;
}

describe('T1 — cmakeConfigure contract shape', () => {
  it('exposes cmakeConfigure(abi: string): int', () => {
    const impl = loadImpl();
    assert.equal(typeof impl.cmakeConfigure, 'function');
    const root = mkTree();
    const result = impl.cmakeConfigure('arm64-v8a', root);
    assert.equal(typeof result, 'number');
    assert.ok(Number.isInteger(result));
  });
});

describe('T1 — accepted ABIs configure cleanly (AC-RUNTIME-025)', () => {
  it('arm64-v8a returns 0 on a clean tree', () => {
    const { cmakeConfigure } = loadImpl();
    assert.equal(cmakeConfigure('arm64-v8a', mkTree()), 0);
  });

  it('x86_64 returns 0 on a clean tree', () => {
    const { cmakeConfigure } = loadImpl();
    assert.equal(cmakeConfigure('x86_64', mkTree()), 0);
  });
});

describe('T1 — no other ABI is accepted', () => {
  it('armeabi-v7a returns non-zero', () => {
    const { cmakeConfigure } = loadImpl();
    assert.notEqual(cmakeConfigure('armeabi-v7a', mkTree()), 0);
  });

  it('x86 (32-bit) returns non-zero', () => {
    const { cmakeConfigure } = loadImpl();
    assert.notEqual(cmakeConfigure('x86', mkTree()), 0);
  });

  it('empty string returns non-zero', () => {
    const { cmakeConfigure } = loadImpl();
    assert.notEqual(cmakeConfigure('', mkTree()), 0);
  });

  it('wrong case (ARM64-V8A) returns non-zero — match is exact', () => {
    // ASSUMPTION: ABI matching is byte-exact; no case folding or trimming.
    const { cmakeConfigure } = loadImpl();
    assert.notEqual(cmakeConfigure('ARM64-V8A', mkTree()), 0);
  });

  it('non-string ABI returns non-zero without throwing', () => {
    const { cmakeConfigure } = loadImpl();
    for (const bad of [undefined, null, 0, ['arm64-v8a']]) {
      let result;
      let threw = false;
      try {
        result = cmakeConfigure(bad, mkTree());
      } catch {
        threw = true;
      }
      assert.equal(threw, false, `must not throw for ${String(bad)}`);
      assert.notEqual(result, 0, `must be non-zero for ${String(bad)}`);
    }
  });
});

describe('T1 — r27 pin is enforced (NFR-BUILD-016 consumed, NFR-RUNTIME-001)', () => {
  it('clean tree (pin 27.2.12479018 + referencing include) configures 0', () => {
    const { cmakeConfigure } = loadImpl();
    assert.equal(cmakeConfigure('arm64-v8a', mkTree()), 0);
  });

  it('wrong ndkVersion in the catalog fails closed naming the pin', () => {
    // ASSUMPTION: the gate fails closed on any version drift and the
    // violation path/message names the expected pin.
    const { cmakeConfigure } = loadImpl();
    const root = mkTree({
      catalog: '[versions]\nndkVersion = "26.1.10909125"\n',
    });
    assert.notEqual(cmakeConfigure('arm64-v8a', root), 0);
  });

  it('missing ndkVersion in the catalog fails closed', () => {
    const { cmakeConfigure } = loadImpl();
    const root = mkTree({ catalog: '[versions]\ncompileSdk = 35\n' });
    assert.notEqual(cmakeConfigure('x86_64', root), 0);
  });

  it('missing version catalog fails closed', () => {
    const { cmakeConfigure } = loadImpl();
    const root = mkTree({ catalog: null });
    assert.notEqual(cmakeConfigure('arm64-v8a', root), 0);
  });

  it('android-ndk.cmake not referencing the pin fails closed', () => {
    // ASSUMPTION: the configure include must reference the pinned version;
    // an include that drifts off-pin is rejected even for a valid ABI.
    const { cmakeConfigure } = loadImpl();
    const root = mkTree({
      ndkCmake: 'set(ANDROID_PLATFORM android-29)\n',
    });
    assert.notEqual(cmakeConfigure('arm64-v8a', root), 0);
  });

  it('missing android-ndk.cmake fails closed', () => {
    const { cmakeConfigure } = loadImpl();
    const root = mkTree({ ndkCmake: null });
    assert.notEqual(cmakeConfigure('x86_64', root), 0);
  });
});
