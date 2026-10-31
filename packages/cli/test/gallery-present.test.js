'use strict';
// T4 — `muse-native gallery-present` CLI: EGL path with frame dump (AC-GRAPHICS-033, TC-GRAPHICS-033).
// Spec refs: FR-GRAPHICS-012, AC-GRAPHICS-033. Depends on: T2, T3.
// Interfaces (plan.md verbatim):
//   `parseGalleryPresentArgs(argv: string[]): { path: 'egl' | 'cpu', dumpFrame: string | null };
//    runGalleryPresent(opts: { serial?: string, path: string, dumpFrame?: string }): Promise<{ path: string, frame: string }>;`
//   CLI `muse-native gallery-present --path egl --dump-frame <png>` exits 0 with the frame PNG
//   written; if EGL is unavailable the run falls back to CPU in-process, stderr carries
//   `GFX-BLIT-FALLBACK: <trigger>`, exit 0 only when a frame was actually dumped.
// Detail under test: gallery-proof entry pattern in bin/muse-native.js (intercept before
// lib/cli.js, exit-code convention 0/1/2). Hermetic arg/parse unit tests need no device;
// device assertion runs on 6562.
// Hermetic: node:test + node:assert/strict + stdlib only. Spawns `node bin/muse-native.js`
// for the CLI exit contract. The device runner is stubbed via an injectable `deps`
// override (ASSUMPTION A1) — hermetic cases never touch adb, a device, the network,
// secrets, or live services. Cases that need 127.0.0.1:6562 are marked DEVICE LEG and
// skip unless MUSE_DEVICE_LEG=1.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: `runGalleryPresent` accepts an optional hermetic `deps` override
//   `{ dumpEglFrame, dumpCpuFrame }` (each `async (opts) => Buffer`) plus an optional
//   `fs` double, mirroring the gallery-proof injectable-advance/readIndex pattern.
//   The plan Interface line names only `{ serial, path, dumpFrame }`; the override is
//   test-only seam, reconcile names (not verdicts) with the coder.
// - A2: unknown flags and missing `--path`/`--dump-frame` values exit 2 with usage
//   naming the flag (gallery-proof exit-code convention 0/1/2).
// - A3: on EGL fallback the resolved `path` in the result is `'cpu'` and stderr carries
//   exactly `GFX-BLIT-FALLBACK: <trigger>` with a non-empty trigger.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

const DEVICE_LEG = process.env.MUSE_DEVICE_LEG === '1';
const DEVICE_SERIAL = process.env.ANDROID_SERIAL || '127.0.0.1:6562';

let galleryPresent = null;
try {
  // eslint-disable-next-line global-require
  galleryPresent = require('../lib/gallery-present.js');
} catch {
  galleryPresent = null;
}

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function tmpPng(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  return { dir, png: path.join(dir, 'frame.png') };
}

// Minimal valid 1x1 PNG bytes (opaque red) for fake frame dumps.
function fakePngBytes() {
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

describe('T4 — gallery-present module surface (AC-GRAPHICS-033)', () => {
  it('packages/cli/lib/gallery-present.js exists and loads', () => {
    assert.ok(galleryPresent !== null, 'lib/gallery-present.js must exist');
  });

  it('module exports parseGalleryPresentArgs', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    assert.equal(typeof galleryPresent.parseGalleryPresentArgs, 'function',
      'must export parseGalleryPresentArgs(argv)');
  });

  it('module exports runGalleryPresent', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    assert.equal(typeof galleryPresent.runGalleryPresent, 'function',
      'must export runGalleryPresent(opts)');
  });
});

describe('T4 — parseGalleryPresentArgs hermetic (no device)', () => {
  it('parses --path egl with no dump frame', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    assert.deepEqual(
      galleryPresent.parseGalleryPresentArgs(['gallery-present', '--path', 'egl']),
      { path: 'egl', dumpFrame: null },
    );
  });

  it('parses --path cpu --dump-frame <png>', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    assert.deepEqual(
      galleryPresent.parseGalleryPresentArgs(['gallery-present', '--path', 'cpu', '--dump-frame', '/tmp/x.png']),
      { path: 'cpu', dumpFrame: '/tmp/x.png' },
    );
  });

  it('parses without the leading subcommand token', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    assert.deepEqual(
      galleryPresent.parseGalleryPresentArgs(['--path', 'egl', '--dump-frame', '/tmp/y.png']),
      { path: 'egl', dumpFrame: '/tmp/y.png' },
    );
  });

  it('defaults to the cpu path when --path is omitted', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    const parsed = galleryPresent.parseGalleryPresentArgs(['gallery-present']);
    assert.equal(parsed.path, 'cpu');
    assert.equal(parsed.dumpFrame, null);
  });

  it('parses --path skia with no dump frame', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    assert.deepEqual(
      galleryPresent.parseGalleryPresentArgs(['gallery-present', '--path', 'skia']),
      { path: 'skia', dumpFrame: null },
    );
  });

  it('parses --path skia --dump-frame <png>', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    assert.deepEqual(
      galleryPresent.parseGalleryPresentArgs(['gallery-present', '--path', 'skia', '--dump-frame', '/tmp/s.png']),
      { path: 'skia', dumpFrame: '/tmp/s.png' },
    );
  });

  it('rejects an unknown --path value (usage failure, never silent)', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    assert.throws(() => galleryPresent.parseGalleryPresentArgs(['--path', 'vulkan']), /path|egl|cpu|Usage/i);
  });

  it('rejects unknown flags (exit-2 usage shape)', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    assert.throws(() => galleryPresent.parseGalleryPresentArgs(['--path', 'egl', '--bogus-flag']), /bogus-flag|unknown/i);
  });

  it('rejects a missing --dump-frame value (usage failure, never silent)', () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    assert.throws(() => galleryPresent.parseGalleryPresentArgs(['--path', 'egl', '--dump-frame']), /dump-frame|value|Usage/i);
  });
});

describe('T4 — runGalleryPresent hermetic, device runner stubbed (no device)', () => {
  it('egl path writes the dumped frame and resolves { path, frame }', async () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    const { dir, png } = tmpPng('muse-gpresent-egl-');
    try {
      const bytes = fakePngBytes();
      const result = await galleryPresent.runGalleryPresent({
        serial: 'stubbed-no-device',
        path: 'egl',
        dumpFrame: png,
        deps: { dumpEglFrame: async () => bytes, dumpCpuFrame: async () => { throw new Error('must not use cpu path'); } },
      });
      assert.equal(result.path, 'egl');
      assert.equal(result.frame, png);
      assert.ok(fs.existsSync(png), 'frame PNG must be written');
      assert.deepEqual(fs.readFileSync(png), bytes);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it('cpu path writes the dumped frame and resolves { path, frame }', async () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    const { dir, png } = tmpPng('muse-gpresent-cpu-');
    try {
      const bytes = fakePngBytes();
      const result = await galleryPresent.runGalleryPresent({
        serial: 'stubbed-no-device',
        path: 'cpu',
        dumpFrame: png,
        deps: { dumpEglFrame: async () => { throw new Error('must not use egl path'); }, dumpCpuFrame: async () => bytes },
      });
      assert.equal(result.path, 'cpu');
      assert.equal(result.frame, png);
      assert.ok(fs.existsSync(png), 'frame PNG must be written');
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it('skia path writes the dumped frame and resolves { path, frame }', async () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    const { dir, png } = tmpPng('muse-gpresent-skia-');
    try {
      const bytes = fakePngBytes();
      const result = await galleryPresent.runGalleryPresent({
        serial: 'stubbed-no-device',
        path: 'skia',
        dumpFrame: png,
        deps: { dumpSkiaFrame: async () => bytes },
      });
      assert.equal(result.path, 'skia');
      assert.equal(result.frame, png);
      assert.ok(fs.existsSync(png), 'frame PNG must be written');
      assert.deepEqual(fs.readFileSync(png), bytes);
      assert.equal(fs.readFileSync(png).subarray(0, 4).toString('hex'), '89504e47');
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it('skia failure rejects with GFX-BLIT-FALLBACK trigger on stderr', async () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    const { dir, png } = tmpPng('muse-gpresent-skiafail-');
    const errChunks = [];
    const origWrite = process.stderr.write.bind(process.stderr);
    process.stderr.write = (chunk, ...rest) => { errChunks.push(String(chunk)); return origWrite(chunk, ...rest); };
    try {
      const skiaErr = new Error('skia render failed');
      skiaErr.trigger = 'skia-swap-fail';
      await assert.rejects(
        galleryPresent.runGalleryPresent({
          serial: 'stubbed-no-device',
          path: 'skia',
          dumpFrame: png,
          deps: { dumpSkiaFrame: async () => { throw skiaErr; } },
        }),
        /skia|no frame dumped/i,
      );
      const stderr = errChunks.join('');
      assert.ok(/GFX-BLIT-FALLBACK: \S+/.test(stderr),
        `stderr must carry GFX-BLIT-FALLBACK: <trigger>, got: ${stderr.slice(0, 300)}`);
    } finally {
      process.stderr.write = origWrite;
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it('egl failure falls back to cpu in-process with GFX-BLIT-FALLBACK trigger on stderr', async () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    const { dir, png } = tmpPng('muse-gpresent-fb-');
    const errChunks = [];
    const origWrite = process.stderr.write.bind(process.stderr);
    process.stderr.write = (chunk, ...rest) => { errChunks.push(String(chunk)); return origWrite(chunk, ...rest); };
    try {
      const bytes = fakePngBytes();
      const eglErr = new Error('eglSwapBuffers failed');
      eglErr.trigger = 'kSwapFailed';
      const result = await galleryPresent.runGalleryPresent({
        serial: 'stubbed-no-device',
        path: 'egl',
        dumpFrame: png,
        deps: { dumpEglFrame: async () => { throw eglErr; }, dumpCpuFrame: async () => bytes },
      });
      assert.equal(result.path, 'cpu', 'fallen-back run resolves the cpu path');
      assert.equal(result.frame, png);
      assert.ok(fs.existsSync(png), 'a frame must actually be dumped for exit 0');
      const stderr = errChunks.join('');
      assert.ok(/GFX-BLIT-FALLBACK: \S+/.test(stderr),
        `stderr must carry GFX-BLIT-FALLBACK: <trigger>, got: ${stderr.slice(0, 300)}`);
    } finally {
      process.stderr.write = origWrite;
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it('exit 0 only when a frame was dumped: both paths failing rejects', async () => {
    assert.ok(galleryPresent, 'lib/gallery-present.js must load');
    const { dir, png } = tmpPng('muse-gpresent-noframe-');
    try {
      await assert.rejects(
        galleryPresent.runGalleryPresent({
          serial: 'stubbed-no-device',
          path: 'egl',
          dumpFrame: png,
          deps: {
            dumpEglFrame: async () => { const e = new Error('no egl'); e.trigger = 'kInitFailed'; throw e; },
            dumpCpuFrame: async () => { throw new Error('no cpu'); },
          },
        }),
        /frame|dump|fallback|kInitFailed|no cpu/i,
      );
      assert.ok(!fs.existsSync(png), 'no frame file may appear when nothing was dumped');
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});

describe('T4 — gallery-present CLI exit contract (AC-GRAPHICS-033, TC-GRAPHICS-033 Check)', () => {
  it('unknown flags exit 2 with usage naming the flag', () => {
    const res = runCli(['gallery-present', '--path', 'egl', '--bogus-flag']);
    assert.equal(res.status, 2, `unknown flags must exit 2, got ${res.status}`);
    assert.ok(/bogus-flag|Usage/i.test(`${res.stdout}\n${res.stderr}`),
      'stderr must name the unknown flag with usage');
  });

  it('missing --dump-frame value exits 2 with usage (never a silent pass)', () => {
    const res = runCli(['gallery-present', '--path', 'egl', '--dump-frame']);
    assert.equal(res.status, 2, `missing value must exit 2, got ${res.status}`);
    assert.ok(/Usage/i.test(`${res.stdout}\n${res.stderr}`), 'missing-value failure must print usage');
  });

  it('--path bogus exits 2 with usage (never a silent pass)', () => {
    const res = runCli(['gallery-present', '--path', 'bogus']);
    assert.equal(res.status, 2, `unknown path must exit 2, got ${res.status}`);
    assert.ok(/Usage|path|egl|cpu|skia/i.test(`${res.stdout}\n${res.stderr}`),
      'unknown-path failure must print usage');
  });

  it('--path skia --dump-frame exits 0 with a PNG-magic frame (hermetic, no device)', () => {
    const { dir, png } = tmpPng('muse-gpresent-skia-cli-');
    try {
      const res = runCli(['gallery-present', '--path', 'skia', '--dump-frame', png]);
      assert.equal(res.status, 0,
        `expected exit 0, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
      assert.ok(fs.existsSync(png), 'frame PNG must be written');
      assert.equal(fs.readFileSync(png).subarray(0, 4).toString('hex'), '89504e47');
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it('DEVICE LEG: --path egl --dump-frame exits 0 on 127.0.0.1:6562 with the frame PNG written', { skip: !DEVICE_LEG }, () => {
    const { dir, png } = tmpPng('muse-gpresent-dev-');
    try {
      const res = runCli(['gallery-present', '--path', 'egl', '--dump-frame', png],
        { env: { ...process.env, ANDROID_SERIAL: DEVICE_SERIAL } });
      assert.equal(res.status, 0,
        `expected exit 0 on 6562, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
      assert.ok(fs.existsSync(png), 'frame PNG must be written on 6562');
      assert.ok(fs.statSync(png).size > 0, 'frame PNG must be non-empty');
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it('DEVICE LEG: cpu and egl dumps show the identical gallery scene (same byte length, non-blank)', { skip: !DEVICE_LEG }, () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-gpresent-parity-'));
    try {
      const cpuPng = path.join(root, 'cpu.png');
      const eglPng = path.join(root, 'egl.png');
      const env = { ...process.env, ANDROID_SERIAL: DEVICE_SERIAL };
      const cpu = runCli(['gallery-present', '--path', 'cpu', '--dump-frame', cpuPng], { env });
      const egl = runCli(['gallery-present', '--path', 'egl', '--dump-frame', eglPng], { env });
      assert.equal(cpu.status, 0, `cpu path must exit 0, got ${cpu.status}`);
      assert.equal(egl.status, 0, `egl path must exit 0, got ${egl.status}`);
      const cpuBytes = fs.readFileSync(cpuPng);
      const eglBytes = fs.readFileSync(eglPng);
      assert.ok(cpuBytes.length > 0 && eglBytes.length > 0, 'both dumps must be non-empty');
      assert.equal(eglBytes.length, cpuBytes.length, 'egl dump must match the cpu scene byte-for-byte in length');
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  });
});
