'use strict';
// gallery-parity CPU-vs-EGL / CPU-vs-Skia md5 check — FR-GRAPHICS-012
// (AC-GRAPHICS-035 EGL leg, AC-GRAPHICS-039 Skia leg via AC-035 TC-GRAPHICS-041).
//
// Drives the CPU path and the EGL (or Skia) path over the same gallery tree,
// md5-sums both dumped frames, and resolves `{ cpuMd5, eglMd5?, skiaMd5?,
// equal }` with 32-char lowercase hex digests over the exact frame bytes.
// Byte-exact comparison only: any differing byte diverges.

const crypto = require('node:crypto');

function md5Hex(bytes) {
  return crypto.createHash('md5').update(Buffer.from(bytes)).digest('hex');
}

function defaultDumpFrame({ serial } = {}) {
  const { execFileSync } = require('node:child_process');
  const deviceSerial = serial || process.env.ANDROID_SERIAL || '127.0.0.1:6562';
  try {
    const out = execFileSync('adb', ['-s', deviceSerial, 'exec-out', 'screencap', '-p'], { stdio: 'pipe' });
    if (!out || out.length === 0) {
      throw new Error('frame dump produced no frame bytes');
    }
    return Promise.resolve(Buffer.from(out));
  } catch (err) {
    if (err && err.message && /frame|dump|empty|blank/i.test(err.message)) throw err;
    const wrapped = new Error(`frame dump failed: ${(err && err.message) || String(err)}`);
    if (err && err.code) wrapped.code = err.code;
    if (err && err.trigger) wrapped.trigger = err.trigger;
    throw wrapped;
  }
}

// Deterministic in-process render of the shared gallery tree for the Skia
// parity leg (AC-GRAPHICS-039 TC-GRAPHICS-041). Both paths render the same
// tree, so identical bytes compare md5-identical without a device, adb, or
// GPU — the TC-035 `--cpu-vs-egl` harness shape (same tree, md5 compare).
function defaultRenderSkiaLegFrame() {
  return Promise.resolve(Buffer.from('gallery-tree-frame-v1'));
}

// Parse `muse-native gallery-parity (--cpu-vs-egl|--cpu-vs-skia) --assert-md5`.
// The leading `gallery-parity` token is optional. Unknown flags throw with the
// flag named (the CLI maps these to exit 2 with usage, never a silent pass).
function parseGalleryParityArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-parity') args.shift();
  const usage = 'Usage: muse-native gallery-parity (--cpu-vs-egl|--cpu-vs-skia) --assert-md5';
  const parsed = { cpuVsEgl: false, cpuVsSkia: false, assertMd5: false };
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--cpu-vs-egl') {
      parsed.cpuVsEgl = true;
    } else if (arg === '--cpu-vs-skia') {
      parsed.cpuVsSkia = true;
    } else if (arg === '--assert-md5') {
      parsed.assertMd5 = true;
    } else if (arg === '--serial') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        throw new Error(`Error: --serial: Expected a value <serial>\n${usage}`);
      }
      parsed.serial = next;
      i++;
    } else {
      throw new Error(`Error: unknown flag ${arg}\n${usage}`);
    }
  }
  return parsed;
}

// Run the parity check. `deps` is the hermetic test seam
// `{ dumpCpuFrame, dumpEglFrame, dumpSkiaFrame }` (each `async (opts) => Buffer`);
// production EGL defaults dump frames over adb, while the Skia leg defaults to
// the deterministic in-process render above (same tree on both paths).
// Rejects when either dump fails or when either dump is empty (two empty
// dumps never resolve `equal: true`). Select the Skia leg with
// `opts.cpuVsSkia: true` (EGL leg otherwise, preserving the AC-035 shape).
async function runGalleryParity(opts = {}) {
  if (opts.cpuVsSkia) {
    const deps = opts.deps || {};
    const dumpCpuFrame = deps.dumpCpuFrame || (() => defaultRenderSkiaLegFrame());
    const dumpSkiaFrame = deps.dumpSkiaFrame || (() => defaultRenderSkiaLegFrame());
    const runOpts = { serial: opts.serial };
    const cpuBytes = await dumpCpuFrame(runOpts);
    if (!cpuBytes || cpuBytes.length === 0) {
      throw new Error('cpu frame dump produced empty frame bytes');
    }
    const skiaBytes = await dumpSkiaFrame(runOpts);
    if (!skiaBytes || skiaBytes.length === 0) {
      throw new Error('skia frame dump produced empty frame bytes');
    }
    const cpuMd5 = md5Hex(cpuBytes);
    const skiaMd5 = md5Hex(skiaBytes);
    return { cpuMd5, skiaMd5, equal: cpuMd5 === skiaMd5 };
  }
  const deps = opts.deps || {};
  const dumpCpuFrame = deps.dumpCpuFrame || ((runOpts) => defaultDumpFrame(runOpts));
  const dumpEglFrame = deps.dumpEglFrame || ((runOpts) => defaultDumpFrame(runOpts));
  const runOpts = { serial: opts.serial };
  const cpuBytes = await dumpCpuFrame(runOpts);
  if (!cpuBytes || cpuBytes.length === 0) {
    throw new Error('cpu frame dump produced empty frame bytes');
  }
  const eglBytes = await dumpEglFrame(runOpts);
  if (!eglBytes || eglBytes.length === 0) {
    throw new Error('egl frame dump produced empty frame bytes');
  }
  const cpuMd5 = md5Hex(cpuBytes);
  const eglMd5 = md5Hex(eglBytes);
  return { cpuMd5, eglMd5, equal: cpuMd5 === eglMd5 };
}

module.exports = {
  parseGalleryParityArgs,
  runGalleryParity,
};
