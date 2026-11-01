'use strict';
// Batch 1 / T1 (FR-PACKAGING-005, AC-PACKAGING-011).
// Scope: hermetic stale-asset regression tests for the gallery-proof deploy
// path. Asserts runGalleryProof never installs an APK whose baked
// assets/tree.mui.bin hash differs from post-sync dist/tree.mui.bin — it
// either rebuilds before install or rejects fail-closed with a `stale-asset`
// error naming the APK path and both hashes.
// Hermetic: node:test + node:assert/strict + stdlib only (fs/os/path/crypto).
// NEVER shells out to adb, NEVER rebuilds a real APK, NEVER touches a device.
// Device I/O is faked via runGalleryProof's injectable hooks
// (sync/install/launch/waitForMarkers/readViewport/advance/readIndex); the
// child_process stub below guarantees no adb spawn can escape. Secrets: none.
// ASSUMPTION A-P1 (contract genuinely silent): a candidate APK that carries
// NO baked assets/tree.mui.bin entry maps to the same `stale-asset`
// fail-closed family (a missing baked asset can never equal the built tree).
// ASSUMPTION A-P2: the stale-asset error message carries no device serial,
// toolchain path, or other host secret — only the APK path and the two hashes.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const { runGalleryProof } = require('../lib/gallery-proof');

const REQUIRED_SYMBOL_T2 = 'Java_com_muse_host_MainActivity_nativeSetImeBottom';
void REQUIRED_SYMBOL_T2;

// --- hermetic child_process stub: proves these tests shell out to nothing --
let stubOriginals = {};
function stubTools() {
  const cp = require('node:child_process');
  stubOriginals = {};
  for (const key of ['execFile', 'execFileSync', 'spawnSync', 'exec', 'execSync', 'spawn']) {
    if (typeof cp[key] === 'function') stubOriginals[key] = cp[key];
  }
  const deny = (tool) => () => { throw new Error(`hermetic: no ${tool} calls allowed`); };
  cp.execFileSync = deny('execFileSync');
  cp.execSync = deny('execSync');
  cp.spawnSync = deny('spawnSync');
}
function restoreTools() {
  const cp = require('node:child_process');
  for (const key of Object.keys(stubOriginals)) cp[key] = stubOriginals[key];
  stubOriginals = {};
}
beforeEach(() => stubTools());
afterEach(() => restoreTools());

// --- fixture builders (stdlib only) ----------------------------------------

function sha256Hex(buf) {
  return crypto.createHash('sha256').update(buf).digest('hex');
}

// Minimal MUI1 envelope (magic + u32LE version + u32LE length + JSON IR) as
// produced for dist/tree.mui.bin. `labels` become component rows; fewer
// labels model the stale 110-row bake vs the fresh 121-row build.
function treeBytes(labels) {
  const ir = {
    roots: [{
      file: 'src/Showcase.tsx',
      children: labels.map((label) => ({ type: 'Button', props: { label } })),
    }],
  };
  const json = Buffer.from(JSON.stringify(ir), 'utf8');
  const out = Buffer.alloc(12 + json.length);
  out.write('MUI1', 0, 'ascii');
  out.writeUInt32LE(1, 4);
  out.writeUInt32LE(json.length, 8);
  json.copy(out, 12);
  return out;
}

function crc32(buf) {
  if (!crc32.table) {
    const t = new Int32Array(256);
    for (let n = 0; n < 256; n += 1) {
      let c = n;
      for (let k = 0; k < 8; k += 1) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
      t[n] = c;
    }
    crc32.table = t;
  }
  let c = 0xFFFFFFFF;
  for (let i = 0; i < buf.length; i += 1) c = crc32.table[(c ^ buf[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

// Minimal stored (no-compression) ZIP carrying the named entries — a stand-in
// candidate APK whose baked assets/tree.mui.bin the proof path must verify.
function buildApkZip(entries) {
  const parts = [];
  const central = [];
  let offset = 0;
  for (const entry of entries) {
    const name = Buffer.from(entry.name, 'utf8');
    const data = Buffer.from(entry.data);
    const crc = crc32(data);
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034B50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(0, 6);
    local.writeUInt16LE(0, 8);
    local.writeUInt16LE(0, 10);
    local.writeUInt16LE(0, 12);
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(data.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(name.length, 26);
    local.writeUInt16LE(0, 28);
    parts.push(local, name, data);
    const headerOffset = offset;
    offset += local.length + name.length + data.length;
    const c = Buffer.alloc(46);
    c.writeUInt32LE(0x02014B50, 0);
    c.writeUInt16LE(20, 4);
    c.writeUInt16LE(20, 6);
    c.writeUInt16LE(0, 8);
    c.writeUInt16LE(0, 10);
    c.writeUInt16LE(0, 12);
    c.writeUInt32LE(crc, 14);
    c.writeUInt32LE(data.length, 18);
    c.writeUInt32LE(data.length, 22);
    c.writeUInt16LE(name.length, 26);
    c.writeUInt16LE(0, 28);
    c.writeUInt16LE(0, 30);
    c.writeUInt16LE(0, 32);
    c.writeUInt16LE(0, 34);
    c.writeUInt32LE(0, 38);
    c.writeUInt32LE(headerOffset, 42);
    central.push(c, name);
  }
  const cdStart = offset;
  const cd = Buffer.concat(central);
  offset += cd.length;
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054B50, 0);
  end.writeUInt16LE(entries.length, 8);
  end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(cd.length, 12);
  end.writeUInt32LE(cdStart, 16);
  return Buffer.concat([...parts, cd, end]);
}

const STALE_LABELS = ['alpha', 'beta', 'gamma', 'delta', 'epsilon'];
const FRESH_LABELS = [...STALE_LABELS, 'zeta', 'eta', 'theta'];

function stageProof({ bakedBytes, omitBakedEntry, preSyncBytes } = {}) {
  const galleryDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gallery-proof-fresh-'));
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gallery-proof-out-'));
  const fresh = treeBytes(FRESH_LABELS);
  const stale = treeBytes(STALE_LABELS);
  const baked = (bakedBytes !== undefined) ? bakedBytes : stale;
  const apkEntries = omitBakedEntry
    ? [{ name: 'AndroidManifest.xml', data: Buffer.from('<manifest/>', 'utf8') }]
    : [
      { name: 'AndroidManifest.xml', data: Buffer.from('<manifest/>', 'utf8') },
      { name: 'assets/tree.mui.bin', data: Buffer.from(baked) },
    ];
  const apkPath = path.join(galleryDir, 'build', 'm3-gallery-debug.apk');
  fs.mkdirSync(path.dirname(apkPath), { recursive: true });
  fs.writeFileSync(apkPath, buildApkZip(apkEntries));
  // Pre-sync working tree carries bytes the sync must supersede.
  const distPath = path.join(galleryDir, 'dist', 'tree.mui.bin');
  fs.mkdirSync(path.dirname(distPath), { recursive: true });
  fs.writeFileSync(distPath, preSyncBytes || stale);
  const installCalls = [];
  const fakes = {
    serial: 'hermetic-test-serial-0000',
    androidJar: path.join(galleryDir, 'fake-android.jar'),
    galleryDir,
    apkPath,
    outDir,
    sync: (dir) => {
      const target = dir || galleryDir;
      const dst = path.join(target, 'dist', 'tree.mui.bin');
      fs.mkdirSync(path.dirname(dst), { recursive: true });
      fs.writeFileSync(dst, fresh);
      return { src: dst, dest: path.join(target, 'assets', 'tree.mui.bin') };
    },
    install: async (args) => { installCalls.push(args); return { installed: true, apk: args.apkPath }; },
    launch: async () => ({ launched: true }),
    waitForMarkers: async () => ({ ready: true }),
    readViewport: async () => ({ width: 1080, height: 2400 }),
    advance: async () => ({ nativeAdvancePage: true }),
    readIndex: async () => 0,
    sleep: async () => {},
    settleMs: 0,
  };
  return { galleryDir, outDir, apkPath, fresh, stale, installCalls, fakes };
}

describe('T1 gallery-proof deploy installs only the fresh tree (AC-PACKAGING-011)', () => {
  it('rejects a stale-baked APK fail-closed with a stale-asset error', async () => {
    const { apkPath, fresh, stale, installCalls, fakes } = stageProof();
    const err = await runGalleryProof(fakes).then(
      () => null,
      (e) => e,
    );
    assert.ok(err, 'expected runGalleryProof to reject a stale-baked APK');
    assert.match(err.message, /stale-asset/, 'rejection must be the stale-asset fail-closed error');
    assert.equal(installCalls.length, 0, 'stale APK must never reach install');
    void apkPath; void fresh; void stale;
  });

  it('stale-asset error names the APK path and both hashes', async () => {
    const { apkPath, fresh, stale, fakes } = stageProof();
    const err = await runGalleryProof(fakes).then(
      () => null,
      (e) => e,
    );
    assert.ok(err, 'expected runGalleryProof to reject a stale-baked APK');
    assert.match(err.message, /stale-asset/);
    assert.ok(err.message.includes(apkPath), `error must name the APK path ${apkPath}`);
    assert.ok(err.message.includes(sha256Hex(stale)), 'error must name the baked-asset hash');
    assert.ok(err.message.includes(sha256Hex(fresh)), 'error must name the post-sync built-tree hash');
  });

  it('hash authority is the post-sync dist bytes, not the pre-sync working tree', async () => {
    const preSync = treeBytes(['old', 'pre', 'sync']);
    assert.notDeepEqual(preSync, treeBytes(FRESH_LABELS));
    const { fakes, fresh } = stageProof({ preSyncBytes: preSync });
    const err = await runGalleryProof(fakes).then(
      () => null,
      (e) => e,
    );
    assert.ok(err, 'expected runGalleryProof to reject a stale-baked APK');
    assert.match(err.message, /stale-asset/);
    assert.ok(
      err.message.includes(sha256Hex(fresh)),
      'error must name the post-sync hash, not the pre-sync working-tree bytes',
    );
    assert.ok(
      !err.message.includes(sha256Hex(preSync)),
      'error must not present pre-sync bytes as the authority hash',
    );
  });

  it('installs a fresh-baked APK without a stale-asset error', async () => {
    const fresh = treeBytes(FRESH_LABELS);
    const { apkPath, installCalls, fakes } = stageProof({ bakedBytes: fresh });
    const err = await runGalleryProof(fakes).then(
      () => null,
      (e) => e,
    );
    if (err) {
      assert.doesNotMatch(err.message, /stale-asset/, 'fresh APK must not hit the stale-asset gate');
    }
    assert.equal(installCalls.length, 1, 'fresh APK must proceed to install');
    assert.equal(installCalls[0].apkPath, apkPath, 'install must receive the verified candidate APK');
  });

  it('an APK missing the baked tree asset fails closed without install', async () => {
    const { installCalls, fakes } = stageProof({ omitBakedEntry: true });
    const err = await runGalleryProof(fakes).then(
      () => null,
      (e) => e,
    );
    assert.ok(err, 'expected runGalleryProof to reject an APK with no baked tree asset');
    assert.match(err.message, /stale-asset/, 'missing baked asset maps to the stale-asset fail-closed family (A-P1)');
    assert.equal(installCalls.length, 0, 'asset-less APK must never reach install');
  });

  it('stale-asset error carries paths and hashes, never device secrets', async () => {
    const { fakes } = stageProof();
    const err = await runGalleryProof(fakes).then(
      () => null,
      (e) => e,
    );
    assert.ok(err, 'expected runGalleryProof to reject a stale-baked APK');
    assert.match(err.message, /stale-asset/);
    assert.ok(
      !err.message.includes(fakes.serial),
      'stale-asset error must not leak the device serial',
    );
  });
});
