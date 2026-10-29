'use strict';
// Batch 4 (FR-PACKAGING-004 TC-PACKAGING-010 / FR-PACKAGING-005 TC-011/012).
// Scope: T7 hermetic pre-checks gating the manual device procedure; T8 has no
// new unit-testable surface (its verification rows are manual) and reuses
// these pre-checks as its entry gate.
// Hermetic: node:test + node:assert/strict + stdlib only. NEVER shells out to
// adb, apksigner, or any SDK tool; NEVER requires a device or emulator. The
// device/tool gates are pure functions returning { skip, reason }; the runner
// surfaces them via t.skip(reason). Real-tool/device proof is manual (T7/T8).
// ASSUMPTION A-G1 (contract genuinely silent): no device present at plan time
// (`adb devices` empty) — device-dependent assertions always SKIP naming the
// empty `adb devices` output, never fail, never shell out.
// ASSUMPTION A-G2: `apksigner verify` is skipped with a clear message when
// build-tools are absent; the skip names the missing tool path.
// ASSUMPTION A-G3: minimum artifact size gate is 1 KiB — a defensible floor
// for a real signed APK; the plan pins no byte size.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const DIST_APK = path.join(REPO_ROOT, 'examples', 'm3-gallery', 'dist', 'app-fat.apk');
const MIN_APK_SIZE = 1024;

// --- hermetic helpers (pure, no process spawn) ------------------------------

function artifactStatus(apkPath) {
  try {
    const st = fs.statSync(apkPath);
    return { exists: st.isFile(), size: st.isFile() ? st.size : 0, path: apkPath };
  } catch (_) {
    return { exists: false, size: 0, path: apkPath };
  }
}

function listZipNames(buf) {
  if (!Buffer.isBuffer(buf)) throw new Error('precheck: expected a Buffer');
  if (buf.length <= 22) throw new Error('precheck: too small to be a ZIP');
  let eocd = -1;
  for (let i = buf.length - 22; i >= 0; i--) {
    if (buf[i] === 0x50 && buf[i + 1] === 0x4b && buf[i + 2] === 0x05 && buf[i + 3] === 0x06) {
      eocd = i;
      break;
    }
  }
  if (eocd < 0) throw new Error('precheck: EOCD record missing — not a valid ZIP');
  const count = buf.readUInt16LE(eocd + 10);
  const cdOffset = buf.readUInt32LE(eocd + 16);
  const names = [];
  let off = cdOffset;
  for (let n = 0; n < count; n++) {
    if (buf.readUInt32LE(off) !== 0x02014b50) throw new Error(`precheck: bad central header #${n}`);
    const nameLen = buf.readUInt16LE(off + 28);
    const extraLen = buf.readUInt16LE(off + 30);
    const commentLen = buf.readUInt16LE(off + 32);
    names.push(buf.toString('utf8', off + 46, off + 46 + nameLen));
    off += 46 + nameLen + extraLen + commentLen;
  }
  return names;
}

function checkSize(size, minSize) {
  if (size < minSize) throw new Error(`precheck: artifact too small (${size} < ${minSize} bytes)`);
}

function adbGate() {
  // Never shells out. No device at plan time: always SKIP with the reason
  // naming the empty `adb devices` output.
  return { skip: true, reason: 'SKIP: `adb devices` empty — no device/emulator present; connect a device to run TC-PACKAGING-010/011/012 manually' };
}

function apksignerGate(buildToolsDir) {
  const tool = path.join(buildToolsDir, 'apksigner');
  if (!fs.existsSync(tool) && !fs.existsSync(`${tool}.bat`)) {
    return { skip: true, reason: `SKIP: apksigner verify skipped — build-tools absent at ${buildToolsDir}` };
  }
  return { skip: false, reason: '' };
}

// Minimal stored-entry ZIP fixture builder (no compression, no deps).
function fakeZip(names) {
  const parts = [];
  const central = [];
  let offset = 0;
  for (const name of names) {
    const nameBuf = Buffer.from(name, 'utf8');
    const data = Buffer.from(`payload:${name}`, 'utf8');
    const local = Buffer.alloc(30 + nameBuf.length + data.length);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(0, 8); // stored
    local.writeUInt32LE(data.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(nameBuf.length, 26);
    nameBuf.copy(local, 30);
    data.copy(local, 30 + nameBuf.length);
    parts.push(local);
    const c = Buffer.alloc(46 + nameBuf.length);
    c.writeUInt32LE(0x02014b50, 0);
    c.writeUInt32LE(0x04034b50, 42);
    c.writeUInt32LE(data.length, 24);
    c.writeUInt16LE(nameBuf.length, 28);
    nameBuf.copy(c, 46);
    c.writeUInt32LE(offset, 42);
    central.push(c);
    offset += local.length;
  }
  const cdStart = offset;
  const cd = Buffer.concat(central);
  parts.push(cd);
  offset += cd.length;
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(names.length, 8);
  eocd.writeUInt16LE(names.length, 10);
  eocd.writeUInt32LE(cd.length, 12);
  eocd.writeUInt32LE(cdStart, 16);
  parts.push(eocd);
  return Buffer.concat(parts);
}

// --- helper unit tests (PASS headless) --------------------------------------

describe('T7 hermetic pre-check helpers (AC-PACKAGING-010 gate)', () => {
  it('lists entries from an inline valid ZIP (stored entries)', () => {
    const names = listZipNames(fakeZip(['AndroidManifest.xml', 'classes.dex', 'assets/tree.mui.bin']));
    assert.deepEqual(names, ['AndroidManifest.xml', 'classes.dex', 'assets/tree.mui.bin']);
  });

  it('rejects a truncated buffer as not-a-ZIP', () => {
    assert.throws(() => listZipNames(Buffer.from('not a zip')), /precheck: /);
  });

  it('rejects a buffer with no EOCD record', () => {
    assert.throws(() => listZipNames(Buffer.alloc(64, 0x41)), /precheck: EOCD/);
  });

  it('reports a missing artifact as absent without throwing', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gallery-launch-'));
    try {
      const st = artifactStatus(path.join(dir, 'dist', 'app-fat.apk'));
      assert.equal(st.exists, false);
      assert.equal(st.size, 0);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it('size gate rejects an undersized artifact', () => {
    assert.throws(() => checkSize(10, MIN_APK_SIZE), /precheck: artifact too small/);
  });

  it('size gate accepts an artifact at the floor', () => {
    checkSize(MIN_APK_SIZE, MIN_APK_SIZE);
  });
});

describe('T7 device/tool gates never shell out (SKIP-with-reason, headless-safe)', () => {
  it('adb gate always skips naming the empty `adb devices` output', () => {
    const gate = adbGate();
    assert.equal(gate.skip, true);
    assert.match(gate.reason, /adb devices/);
  });

  it('manual install/launch steps SKIP when no device is present', (t) => {
    const gate = adbGate();
    t.skip(gate.reason);
  });

  it('apksigner verify gate skips with a clear message when build-tools are absent', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gallery-launch-tools-'));
    try {
      const gate = apksignerGate(dir);
      assert.equal(gate.skip, true);
      assert.match(gate.reason, /apksigner verify skipped/);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});

// --- artifact pre-checks against the real dist (RED until built) ------------
// Manual procedure (TC-PACKAGING-010, never executed here):
//  1. connect a device/emulator (`adb devices` non-empty);
//  2. `adb install dist/app-fat.apk`; 3. launch MainActivity;
//  4. confirm the main screen appears with no crash.

describe('T7 dist/app-fat.apk pre-checks (TC-PACKAGING-010 gate; RED until built)', () => {
  it('dist/app-fat.apk exists (AC-PACKAGING-010)', () => {
    const st = artifactStatus(DIST_APK);
    assert.ok(st.exists, `missing artifact: ${DIST_APK} — build the APK before manual install (TC-PACKAGING-010)`);
  });

  it('dist/app-fat.apk is a valid ZIP above the size floor listing AndroidManifest.xml', () => {
    const st = artifactStatus(DIST_APK);
    assert.ok(st.exists, `missing artifact: ${DIST_APK}`);
    checkSize(st.size, MIN_APK_SIZE);
    const names = listZipNames(fs.readFileSync(DIST_APK));
    assert.ok(names.includes('AndroidManifest.xml'), 'APK zip listing shows AndroidManifest.xml');
  });
});
