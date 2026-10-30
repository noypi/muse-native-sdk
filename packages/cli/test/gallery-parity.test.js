'use strict';
// T5 — `muse-native gallery-parity` CLI: md5-identical CPU-vs-EGL frames (AC-GRAPHICS-035, TC-GRAPHICS-035 6562 leg).
// Spec refs: FR-GRAPHICS-012, AC-GRAPHICS-035. Depends on: T4.
// Interface (plan.md verbatim):
//   `runGalleryParity(opts: { serial?: string, assertMd5: boolean }): Promise<{ cpuMd5: string, eglMd5: string, equal: boolean }>;`
//   CLI `muse-native gallery-parity --cpu-vs-egl --assert-md5` exits 0 iff `cpuMd5 === eglMd5`,
//   nonzero otherwise; the check fails for a real raster divergence and never spuriously
//   (same tree, deterministic raster, byte-compared).
// Detail under test: drives the CPU path (T4 --path cpu) and the EGL path (T4 --path egl)
// over the same gallery tree on the target device, md5-sums both dumped frames. Run on 6562
// per the 6562-only constraint; the 6569 leg named in AC-035 is UNVERIFIED-not-run
// (user decision Q2 — recorded, not tested on 6569).
// Hermetic: node:test + node:assert/strict + stdlib (crypto/fs/os/path/child_process) only.
// Frame dumps are stubbed via an injectable `deps` override (ASSUMPTION A1) — hermetic cases
// never touch adb, a device, the network, secrets, or live services. The 6569 leg is
// explicitly out: no test contacts 127.0.0.1:6569. Cases needing 127.0.0.1:6562 are marked
// DEVICE LEG and skip unless MUSE_DEVICE_LEG=1.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: `runGalleryParity` accepts an optional hermetic `deps` override
//   `{ dumpCpuFrame, dumpEglFrame }` (each `async (opts) => Buffer`) so the md5 comparison
//   is unit-testable without a device, mirroring the gallery-proof injectable pattern.
//   The plan Interface line names only `{ serial, assertMd5 }`; the override is a test-only
//   seam, reconcile names (not verdicts) with the coder.
// - A2: unknown flags and a missing `--cpu-vs-egl`/`--assert-md5` pair exit 2 with usage
//   naming the flag (gallery-proof exit-code convention 0/1/2).
// - A3: md5 is hex over the exact dumped frame bytes (node:crypto md5); the CLI logs both
//   md5 values on stdout.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

const DEVICE_LEG = process.env.MUSE_DEVICE_LEG === '1';
const DEVICE_SERIAL = process.env.ANDROID_SERIAL || '127.0.0.1:6562';

let galleryParity = null;
try {
  // eslint-disable-next-line global-require
  galleryParity = require('../lib/gallery-parity.js');
} catch {
  galleryParity = null;
}

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function md5(bytes) {
  return crypto.createHash('md5').update(bytes).digest('hex');
}

// Minimal valid 1x1 PNG bytes; byte-flip the tail for a divergent frame.
function frameA() {
  return Buffer.from([
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41,
    0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
    0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0xfe,
    0xd4, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82,
  ]);
}

function frameB() {
  const b = frameA();
  b[41] ^= 0xff;
  b[42] ^= 0xff;
  return b;
}

describe('T5 — gallery-parity module surface (AC-GRAPHICS-035)', () => {
  it('packages/cli/lib/gallery-parity.js exists and loads', () => {
    assert.ok(galleryParity !== null, 'lib/gallery-parity.js must exist');
  });

  it('module exports runGalleryParity', () => {
    assert.ok(galleryParity, 'lib/gallery-parity.js must load');
    assert.equal(typeof galleryParity.runGalleryParity, 'function',
      'must export runGalleryParity(opts)');
  });
});

describe('T5 — runGalleryParity hermetic, frame dumps stubbed (no device)', () => {
  it('identical cpu/egl frames resolve equal:true with both md5 values', async () => {
    assert.ok(galleryParity, 'lib/gallery-parity.js must load');
    const bytes = frameA();
    const result = await galleryParity.runGalleryParity({
      serial: 'stubbed-no-device',
      assertMd5: true,
      deps: { dumpCpuFrame: async () => bytes, dumpEglFrame: async () => Buffer.from(bytes) },
    });
    assert.equal(result.cpuMd5, md5(bytes));
    assert.equal(result.eglMd5, md5(bytes));
    assert.equal(result.equal, true);
  });

  it('md5 values are 32-char lowercase hex over the exact frame bytes', async () => {
    assert.ok(galleryParity, 'lib/gallery-parity.js must load');
    const bytes = frameA();
    const result = await galleryParity.runGalleryParity({
      serial: 'stubbed-no-device',
      assertMd5: true,
      deps: { dumpCpuFrame: async () => bytes, dumpEglFrame: async () => Buffer.from(bytes) },
    });
    assert.ok(/^[0-9a-f]{32}$/.test(result.cpuMd5), `cpuMd5 must be hex md5, got ${result.cpuMd5}`);
    assert.ok(/^[0-9a-f]{32}$/.test(result.eglMd5), `eglMd5 must be hex md5, got ${result.eglMd5}`);
  });

  it('a real raster divergence resolves equal:false (never a spurious pass)', async () => {
    assert.ok(galleryParity, 'lib/gallery-parity.js must load');
    const result = await galleryParity.runGalleryParity({
      serial: 'stubbed-no-device',
      assertMd5: true,
      deps: { dumpCpuFrame: async () => frameA(), dumpEglFrame: async () => frameB() },
    });
    assert.equal(result.equal, false);
    assert.notEqual(result.cpuMd5, result.eglMd5);
    assert.equal(result.cpuMd5, md5(frameA()));
    assert.equal(result.eglMd5, md5(frameB()));
  });

  it('a failed frame dump rejects (never resolves a verdict without both frames)', async () => {
    assert.ok(galleryParity, 'lib/gallery-parity.js must load');
    await assert.rejects(
      galleryParity.runGalleryParity({
        serial: 'stubbed-no-device',
        assertMd5: true,
        deps: {
          dumpCpuFrame: async () => frameA(),
          dumpEglFrame: async () => { const e = new Error('no egl frame'); e.trigger = 'kSwapFailed'; throw e; },
        },
      }),
      /frame|dump|egl/i,
    );
  });

  it('empty/blank frame bytes never count as parity (rejects, never equal:true)', async () => {
    assert.ok(galleryParity, 'lib/gallery-parity.js must load');
    const result = await galleryParity.runGalleryParity({
      serial: 'stubbed-no-device',
      assertMd5: true,
      deps: { dumpCpuFrame: async () => frameA(), dumpEglFrame: async () => frameA() },
    }).catch((err) => err);
    if (result && result.equal === true) {
      assert.ok(result.cpuMd5.length === 32, 'equal:true requires real md5 values, never a vacuous pass');
    } else {
      assert.ok(result instanceof Error || result.equal === false, 'blank handling must not silently pass');
    }
    await assert.rejects(
      galleryParity.runGalleryParity({
        serial: 'stubbed-no-device',
        assertMd5: true,
        deps: { dumpCpuFrame: async () => Buffer.alloc(0), dumpEglFrame: async () => Buffer.alloc(0) },
      }),
      /frame|empty|blank|dump/i,
      'two empty dumps must reject, never resolve equal:true',
    );
  });
});

describe('T5 — gallery-parity CLI exit contract (AC-GRAPHICS-035, TC-GRAPHICS-035 Check)', () => {
  it('unknown flags exit 2 with usage naming the flag', () => {
    const res = runCli(['gallery-parity', '--cpu-vs-egl', '--assert-md5', '--bogus-flag']);
    assert.equal(res.status, 2, `unknown flags must exit 2, got ${res.status}`);
    assert.ok(/bogus-flag|Usage/i.test(`${res.stdout}\n${res.stderr}`),
      'stderr must name the unknown flag with usage');
  });

  it('missing --assert-md5 exits 2 with usage (never a silent pass)', () => {
    const res = runCli(['gallery-parity', '--cpu-vs-egl']);
    assert.equal(res.status, 2, `missing flag must exit 2, got ${res.status}`);
    assert.ok(/Usage/i.test(`${res.stdout}\n${res.stderr}`), 'missing-flag failure must print usage');
  });

  it('missing --cpu-vs-egl exits 2 with usage (never a silent pass)', () => {
    const res = runCli(['gallery-parity', '--assert-md5']);
    assert.equal(res.status, 2, `missing flag must exit 2, got ${res.status}`);
    assert.ok(/Usage/i.test(`${res.stdout}\n${res.stderr}`), 'missing-flag failure must print usage');
  });

  it('6569 leg is explicitly out: no test contacts 127.0.0.1:6569', () => {
    // Guard test: the 6569 leg named in AC-035 is UNVERIFIED-not-run per the 6562-only
    // constraint (user decision Q2). This file must not reference a 6569 device leg.
    const src = require('node:fs').readFileSync(__filename, 'utf8');
    const codeLines = src.split('\n').filter((l) => !/^\s*\/\//.test(l) && !/6569 leg is explicitly out/.test(l));
    const code = codeLines.join('\n');
    assert.ok(!new RegExp('656' + '9').test(code), 'no executable test code may target port 656' + '9 in this batch');
  });

  it('DEVICE LEG: --cpu-vs-egl --assert-md5 exits 0 on 127.0.0.1:6562 with both md5 values logged and equal', { skip: !DEVICE_LEG }, () => {
    const res = runCli(['gallery-parity', '--cpu-vs-egl', '--assert-md5'],
      { env: { ...process.env, ANDROID_SERIAL: DEVICE_SERIAL } });
    assert.equal(res.status, 0,
      `expected exit 0 on 6562 with identical frames, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
    const out = `${res.stdout}\n${res.stderr}`;
    const hexes = out.match(/[0-9a-f]{32}/gi) || [];
    assert.ok(hexes.length >= 2, `output must log both md5 values, got: ${out.slice(0, 300)}`);
    assert.equal(hexes[0].toLowerCase(), hexes[1].toLowerCase(), 'logged cpuMd5 and eglMd5 must be equal');
  });
});
