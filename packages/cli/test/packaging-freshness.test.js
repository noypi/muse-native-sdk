'use strict';
// Batch 1 / T2 (FR-PACKAGING-004 AC-PACKAGING-010; FR-PACKAGING-005 AC-PACKAGING-011).
// Scope: hermetic packaged-`.so` + asset freshness regression tests.
// Asserts the packaged libRuntime.so matches fresh engine output (SHA-256
// hash equality against engine/runtime/build-android-<abi>/libRuntime.so, or
// the required Java_com_muse_host_MainActivity_nativeSetImeBottom export)
// and that a stale-but-nonempty `.so` fails the scaffold `apk` gate closed
// naming the stale ABI path; likewise assets/tree.mui.bin vs
// dist/tree.mui.bin mismatches fail closed with `stale-asset`.
// Hermetic: node:test + node:assert/strict + stdlib only (fs/path/crypto).
// Fixture `.so` bytes and symbol lists only — NEVER the real NDK, NEVER a
// device, NEVER downloads. The scaffold Makefile is read as text (same style
// as packages/cli/test/packaging-make.test.js); `make` itself is never run.
// Secrets: none (fake byte strings only).
// ASSUMPTION A-S1 (plan A1 pins the mechanism here): freshness authority is
// SHA-256 equality with the fresh engine output OR presence of the
// `Java_com_muse_host_MainActivity_nativeSetImeBottom` export — the
// crash-relevant minimum. The coder reuses this exact authority in T4.
// ASSUMPTION A-S2: gate-error naming follows the plan T4 interface:
// `stale-lib: <abi> <sdkSoPath> …` for `.so` staleness and `stale-asset: …`
// for tree-asset staleness, failing before any `aapt2 link` (nonzero exit,
// no partial APK — asserted textually via ordering before the link line).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const REQUIRED_EXPORT = 'Java_com_muse_host_MainActivity_nativeSetImeBottom';

function repoRoot() {
  let dir = __dirname;
  for (let i = 0; i < 8; i += 1) {
    if (fs.existsSync(path.join(dir, 'packages', 'cli', 'templates', 'Makefile'))) return dir;
    dir = path.dirname(dir);
  }
  throw new Error('test setup: cannot locate repo root from ' + __dirname);
}

const ROOT = repoRoot();
const SCAFFOLD_MAKEFILE = path.join(ROOT, 'packages', 'cli', 'templates', 'Makefile');

function readMakefile() {
  assert.ok(fs.existsSync(SCAFFOLD_MAKEFILE), `expected scaffold Makefile at ${SCAFFOLD_MAKEFILE}`);
  return fs.readFileSync(SCAFFOLD_MAKEFILE, 'utf8');
}

function sha256Hex(buf) {
  return crypto.createHash('sha256').update(buf).digest('hex');
}

// Test-local pin of the freshness authority (A-S1): a candidate `.so` is
// fresh iff it byte-matches the fresh engine output, or it carries the
// required nativeSetImeBottom export. Mirrors the gate T4 must implement;
// verdicts below always derive from the Makefile text + fixtures, never from
// a live toolchain.
function isFreshSoBytes(candidate, freshEngineBytes) {
  const cand = Buffer.isBuffer(candidate) ? candidate : Buffer.from(candidate || []);
  if (freshEngineBytes) {
    const eng = Buffer.isBuffer(freshEngineBytes) ? freshEngineBytes : Buffer.from(freshEngineBytes);
    if (cand.length === eng.length && crypto.timingSafeEqual(cand, eng)) return true;
  }
  return cand.includes(REQUIRED_EXPORT);
}

// Emulates the scaffold `apk` gate semantics from the Makefile text: returns
// `absent` when no freshness gate exists (current `test -s`-only state),
// otherwise evaluates the fixture against the pinned authority.
function evaluateSoGate(makefileText, { abi, candidateBytes, engineBytes }) {
  const hasStaleLib = /stale-lib/.test(makefileText);
  const hasAuthority = makefileText.includes(REQUIRED_EXPORT)
    || /build-android-.+\/libRuntime\.so/.test(makefileText);
  if (!hasStaleLib || !hasAuthority) return { gate: 'absent' };
  const sdkSoPath = `$(MUSE_NATIVE_HOME)/android-${abi}/libRuntime.so`;
  const namesAbiPath = makefileText.includes(`android-${abi}/libRuntime.so`);
  if (!namesAbiPath) return { gate: 'absent', reason: `no ABI path for ${abi}` };
  if (!candidateBytes || candidateBytes.length === 0) {
    return { gate: 'reject', reason: `missing engine lib for ABI slot ${abi} at '${sdkSoPath}'` };
  }
  if (isFreshSoBytes(candidateBytes, engineBytes)) return { gate: 'pass' };
  return {
    gate: 'reject',
    reason: `stale-lib: ${abi} ${sdkSoPath} expected hash ${sha256Hex(engineBytes || candidateBytes)}`,
  };
}

function evaluateAssetGate(makefileText, { builtBytes, stagedBytes }) {
  const hasStaleAsset = /stale-asset/.test(makefileText);
  const comparesTrees = /assets\/tree\.mui\.bin/.test(makefileText)
    && /dist\/tree\.mui\.bin/.test(makefileText);
  if (!hasStaleAsset || !comparesTrees) return { gate: 'absent' };
  if (sha256Hex(builtBytes) === sha256Hex(stagedBytes)) return { gate: 'pass' };
  return {
    gate: 'reject',
    reason: `stale-asset: assets/tree.mui.bin baked ${sha256Hex(stagedBytes)} != built ${sha256Hex(builtBytes)}`,
  };
}

// Stale-but-nonempty fixture: realistic ELF-magic prefix, nonzero size (so
// the current `test -s` presence gate passes), but WITHOUT the required
// export — the exact class T4 must stop shipping.
const STALE_SO = Buffer.concat([
  Buffer.from([0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00]),
  Buffer.from('stale-prebuilt-libRuntime-without-ime-export-padding-0000000001'),
]);
const FRESH_SO = Buffer.concat([
  Buffer.from([0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00]),
  Buffer.from(`fresh-engine-libRuntime-exporting-${REQUIRED_EXPORT}-padding-02`),
]);

describe('T2 packaged libRuntime.so freshness (AC-PACKAGING-010)', () => {
  it('apk gate fails closed on a stale-but-nonempty SDK .so, naming the stale ABI path', () => {
    const text = readMakefile();
    assert.ok(
      /stale-lib/.test(text),
      'scaffold Makefile apk target must fail closed with a `stale-lib` error',
    );
    assert.ok(
      text.includes('android-arm64-v8a/libRuntime.so'),
      'stale-lib error must name the stale ABI path',
    );
  });

  it('freshness authority is pinned: engine-output hash or the required export', () => {
    const text = readMakefile();
    assert.ok(
      text.includes(REQUIRED_EXPORT) || /build-android-.+\/libRuntime\.so/.test(text),
      `Makefile must pin freshness to engine-output hash compare or the ${REQUIRED_EXPORT} export`,
    );
  });

  it('emulated apk gate rejects a stale-but-nonempty .so that passes test -s', () => {
    assert.ok(STALE_SO.length > 0, 'stale fixture must be nonempty (passes current test -s)');
    assert.ok(!STALE_SO.includes(REQUIRED_EXPORT), 'stale fixture must lack the required export');
    const verdict = evaluateSoGate(readMakefile(), {
      abi: 'arm64-v8a',
      candidateBytes: STALE_SO,
      engineBytes: FRESH_SO,
    });
    assert.equal(verdict.gate, 'reject', 'stale-but-nonempty .so must fail the freshness gate');
    assert.match(verdict.reason, /stale-lib: arm64-v8a/);
    assert.match(verdict.reason, /android-arm64-v8a\/libRuntime\.so/);
  });

  it('emulated apk gate passes a fresh .so carrying the required export', () => {
    const verdict = evaluateSoGate(readMakefile(), {
      abi: 'arm64-v8a',
      candidateBytes: FRESH_SO,
      engineBytes: FRESH_SO,
    });
    assert.equal(verdict.gate, 'pass', 'fresh .so must proceed through the gate');
  });

  it('empty or missing .so bytes are still rejected (presence floor preserved)', () => {
    const text = readMakefile();
    assert.ok(
      /test -s.*libRuntime\.so/.test(text),
      'scaffold Makefile must keep the nonempty presence gate for every ABI lib',
    );
    const verdict = evaluateSoGate(text, {
      abi: 'x86_64',
      candidateBytes: Buffer.alloc(0),
      engineBytes: FRESH_SO,
    });
    assert.notEqual(verdict.gate, 'pass', 'empty .so must never pass, gate or no gate');
  });
});

describe('T2 staged tree-asset freshness (AC-PACKAGING-011)', () => {
  it('apk gate fails closed when assets/tree.mui.bin mismatches dist/tree.mui.bin', () => {
    const text = readMakefile();
    assert.ok(
      /stale-asset/.test(text),
      'scaffold Makefile must fail closed with a `stale-asset` error on tree mismatch',
    );
    assert.ok(
      text.includes('assets/tree.mui.bin') && text.includes('dist/tree.mui.bin'),
      'asset gate must compare assets/tree.mui.bin against dist/tree.mui.bin',
    );
  });

  it('emulated asset gate rejects staged bytes that differ from the built tree', () => {
    const built = Buffer.from('MUI1-fresh-121-row-tree-bytes');
    const staged = Buffer.from('MUI1-stale-110-row-tree-bytes');
    const verdict = evaluateAssetGate(readMakefile(), { builtBytes: built, stagedBytes: staged });
    assert.equal(verdict.gate, 'reject', 'mismatched staged tree must fail the asset gate');
    assert.match(verdict.reason, /stale-asset/);
    assert.ok(verdict.reason.includes(sha256Hex(built)), 'rejection names the built-tree hash');
    assert.ok(verdict.reason.includes(sha256Hex(staged)), 'rejection names the staged-tree hash');
  });

  it('emulated asset gate passes staged bytes equal to the built tree', () => {
    const built = Buffer.from('MUI1-fresh-121-row-tree-bytes');
    const verdict = evaluateAssetGate(readMakefile(), { builtBytes: built, stagedBytes: Buffer.from(built) });
    assert.equal(verdict.gate, 'pass', 'matching staged tree must proceed through the gate');
  });
});

// Batch 4 / T6 (FR-PACKAGING-004 AC-PACKAGING-010; FR-PACKAGING-005 AC-PACKAGING-011).
// Scope: gallery-Makefile gate parity (examples/m3-gallery/Makefile).
// Mirrors the T2 authority (A-S1) for the gallery Makefile's four ABI slots +
// its tree-asset sync line `mkdir -p assets && cp dist/tree.mui.bin
// assets/tree.mui.bin`. Hermetic: the gallery Makefile is read as text only;
// `make`/adb/NDK are never invoked.
// ASSUMPTION A-S1 (reused from T2): freshness authority is SHA-256 equality
// with the fresh engine output OR presence of REQUIRED_EXPORT.
// ASSUMPTION A-S3: gate-error naming mirrors T4 — `stale-lib: <abi>
// <sdkSoPath> …` / `stale-asset: …`, evaluated before any `aapt2 link`
// (nonzero exit, no partial APK — asserted textually via ordering).

const GALLERY_MAKEFILE = path.join(ROOT, 'examples', 'm3-gallery', 'Makefile');
const GALLERY_ABIS = ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'];

function readGalleryMakefile() {
  assert.ok(fs.existsSync(GALLERY_MAKEFILE), `expected gallery Makefile at ${GALLERY_MAKEFILE}`);
  return fs.readFileSync(GALLERY_MAKEFILE, 'utf8');
}

describe('T6 gallery Makefile libRuntime.so freshness parity (AC-PACKAGING-010)', () => {
  it('gallery apk gate fails closed on a stale-but-nonempty SDK .so, naming the stale ABI path', () => {
    const text = readGalleryMakefile();
    assert.ok(
      /stale-lib/.test(text),
      'gallery Makefile apk target must fail closed with a `stale-lib` error',
    );
    assert.ok(
      text.includes('android-arm64-v8a/libRuntime.so'),
      'gallery stale-lib error must name the stale ABI path',
    );
  });

  it('gallery freshness authority is pinned: engine-output hash or the required export', () => {
    const text = readGalleryMakefile();
    assert.ok(
      text.includes(REQUIRED_EXPORT) || /build-android-.+\/libRuntime\.so/.test(text),
      `gallery Makefile must pin freshness to engine-output hash compare or the ${REQUIRED_EXPORT} export`,
    );
  });

  it('gallery gate covers all four ABI slots of the gallery Makefile', () => {
    const text = readGalleryMakefile();
    for (const abi of GALLERY_ABIS) {
      assert.ok(
        text.includes(`android-${abi}/libRuntime.so`),
        `gallery Makefile gate must cover ABI slot ${abi}`,
      );
    }
  });

  it('emulated gallery apk gate rejects a stale-but-nonempty .so that passes test -s', () => {
    assert.ok(STALE_SO.length > 0, 'stale fixture must be nonempty (passes current test -s)');
    assert.ok(!STALE_SO.includes(REQUIRED_EXPORT), 'stale fixture must lack the required export');
    const verdict = evaluateSoGate(readGalleryMakefile(), {
      abi: 'arm64-v8a',
      candidateBytes: STALE_SO,
      engineBytes: FRESH_SO,
    });
    assert.equal(verdict.gate, 'reject', 'stale-but-nonempty .so must fail the gallery freshness gate');
    assert.match(verdict.reason, /stale-lib: arm64-v8a/);
    assert.match(verdict.reason, /android-arm64-v8a\/libRuntime\.so/);
  });

  it('emulated gallery apk gate passes a fresh .so carrying the required export', () => {
    const verdict = evaluateSoGate(readGalleryMakefile(), {
      abi: 'arm64-v8a',
      candidateBytes: FRESH_SO,
      engineBytes: FRESH_SO,
    });
    assert.equal(verdict.gate, 'pass', 'fresh .so must proceed through the gallery gate');
  });

  it('gallery empty or missing .so bytes are still rejected (presence floor preserved)', () => {
    const text = readGalleryMakefile();
    assert.ok(
      /test -s.*libRuntime\.so/.test(text),
      'gallery Makefile must keep the nonempty presence gate for every ABI lib',
    );
    const verdict = evaluateSoGate(text, {
      abi: 'x86_64',
      candidateBytes: Buffer.alloc(0),
      engineBytes: FRESH_SO,
    });
    assert.notEqual(verdict.gate, 'pass', 'empty .so must never pass, gate or no gate');
  });

  it('gallery freshness gates run before any aapt2 link (nonzero exit, no partial APK)', () => {
    const text = readGalleryMakefile();
    const linkAt = text.indexOf('aapt2 link');
    assert.ok(linkAt !== -1, 'gallery Makefile must contain the aapt2 link line');
    const staleLibAt = text.indexOf('stale-lib');
    assert.ok(staleLibAt !== -1 && staleLibAt < linkAt, 'stale-lib gate must precede aapt2 link');
    const gateSlice = text.slice(0, linkAt);
    assert.ok(/exit 1/.test(gateSlice), 'pre-link freshness gates must exit nonzero on staleness');
  });
});

describe('T6 gallery staged tree-asset freshness parity (AC-PACKAGING-011)', () => {
  it('gallery apk gate fails closed when assets/tree.mui.bin mismatches dist/tree.mui.bin', () => {
    const text = readGalleryMakefile();
    assert.ok(
      /stale-asset/.test(text),
      'gallery Makefile must fail closed with a `stale-asset` error on tree mismatch',
    );
    assert.ok(
      text.includes('assets/tree.mui.bin') && text.includes('dist/tree.mui.bin'),
      'gallery asset gate must compare assets/tree.mui.bin against dist/tree.mui.bin',
    );
  });

  it('gallery keeps the tree-asset sync line and gates post-sync bytes by hash', () => {
    const text = readGalleryMakefile();
    assert.ok(
      text.includes('mkdir -p assets && cp dist/tree.mui.bin assets/tree.mui.bin'),
      'gallery Makefile must keep the tree-asset sync line `mkdir -p assets && cp dist/tree.mui.bin assets/tree.mui.bin`',
    );
    assert.ok(/sha256sum/.test(text), 'gallery asset gate must compare post-sync bytes by hash');
    const syncAt = text.indexOf('mkdir -p assets && cp dist/tree.mui.bin assets/tree.mui.bin');
    const assetAt = text.indexOf('stale-asset');
    const linkAt = text.indexOf('aapt2 link');
    assert.ok(assetAt > syncAt, 'stale-asset gate must run after the tree-asset sync line');
    assert.ok(assetAt !== -1 && assetAt < linkAt, 'stale-asset gate must precede aapt2 link');
  });

  it('emulated gallery asset gate rejects staged bytes that differ from the built tree', () => {
    const built = Buffer.from('MUI1-fresh-121-row-tree-bytes');
    const staged = Buffer.from('MUI1-stale-110-row-tree-bytes');
    const verdict = evaluateAssetGate(readGalleryMakefile(), { builtBytes: built, stagedBytes: staged });
    assert.equal(verdict.gate, 'reject', 'mismatched staged tree must fail the gallery asset gate');
    assert.match(verdict.reason, /stale-asset/);
    assert.ok(verdict.reason.includes(sha256Hex(built)), 'rejection names the built-tree hash');
    assert.ok(verdict.reason.includes(sha256Hex(staged)), 'rejection names the staged-tree hash');
  });

  it('emulated gallery asset gate passes staged bytes equal to the built tree', () => {
    const built = Buffer.from('MUI1-fresh-121-row-tree-bytes');
    const verdict = evaluateAssetGate(readGalleryMakefile(), { builtBytes: built, stagedBytes: Buffer.from(built) });
    assert.equal(verdict.gate, 'pass', 'matching staged tree must proceed through the gallery gate');
  });
});
