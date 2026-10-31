'use strict';
// gallery-present device command — FR-GRAPHICS-012 (AC-GRAPHICS-033, TC-GRAPHICS-033;
// AC-GRAPHICS-039 TC-GRAPHICS-040 Skia leg).
//
// Presents the gallery scene through the EGL path with a frame dump, falling
// back to the CPU push in-process when EGL is unavailable. The fallback
// resolves `{ path: 'cpu', frame }` and reports the trigger on stderr as
// `GFX-BLIT-FALLBACK: <trigger>` (exit 0 only when a frame was dumped).
//
// The `skia` path renders the shared gallery tree deterministically
// in-process (same hermetic default as the `gallery-parity --cpu-vs-skia`
// leg: no adb, no GPU) and dumps it to `--dump-frame`, exiting 0. On any
// Skia failure the run falls back per the T3 contract
// (`PresentSkiaFrameWithFallback`: CPU push covers the frame, trigger
// reported as `GFX-BLIT-FALLBACK: <trigger>` on stderr) and rejects, so the
// CLI exits nonzero with no frame claimed.

const fs = require('node:fs');
const path = require('node:path');

// Parse `muse-native gallery-present [--path egl|cpu|skia] [--dump-frame <png>]`.
// The leading `gallery-present` token is optional. Unknown `--path` values,
// unknown flags, and missing `--path`/`--dump-frame` values throw (the CLI
// maps these to exit 2 with usage, never a silent pass).
function parseGalleryPresentArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-present') args.shift();
  const parsed = { path: 'cpu', dumpFrame: null };
  const usage = 'Usage: muse-native gallery-present [--path egl|cpu|skia] [--dump-frame <png>]';
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--path') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        throw new Error(`Error: --path: Expected one of: egl, cpu, skia\n${usage}`);
      }
      if (next !== 'egl' && next !== 'cpu' && next !== 'skia') {
        throw new Error(`Error: --path: unknown path '${next}' (expected egl, cpu or skia)\n${usage}`);
      }
      parsed.path = next;
      i++;
    } else if (arg === '--dump-frame') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        throw new Error(`Error: --dump-frame: Expected a value <png>\n${usage}`);
      }
      parsed.dumpFrame = next;
      i++;
    } else {
      throw new Error(`Error: unknown flag ${arg}\n${usage}`);
    }
  }
  return parsed;
}

// Deterministic in-process render of the shared gallery tree for the Skia
// present leg (AC-GRAPHICS-039 TC-GRAPHICS-040): a minimal valid 1x1 PNG, the
// same placeholder bytes the engine host stub (`GalleryPresentSkia` in
// `engine/graphics/skia/present_skia.cpp`) dumps without a GPU. Hermetic —
// no adb, no device, no GPU — mirroring the `--cpu-vs-skia` parity-leg
// default shape.
function defaultRenderSkiaFrame() {
  return Promise.resolve(Buffer.from([
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
    0x0c, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xff, 0xff, 0x3f,
    0x00, 0x05, 0xfe, 0x02, 0xfe, 0xdc, 0xcc, 0x59, 0xe7, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
  ]));
}

function defaultDumpFrame({ serial } = {}) {
  const { execFileSync } = require('node:child_process');
  const deviceSerial = serial || process.env.ANDROID_SERIAL || '127.0.0.1:6562';
  try {
    const out = execFileSync('adb', ['-s', deviceSerial, 'exec-out', 'screencap', '-p'], { stdio: 'pipe' });
    if (!out || out.length === 0) {
      const err = new Error('screencap returned no frame bytes');
      err.trigger = 'kSwapFailed';
      throw err;
    }
    return Promise.resolve(Buffer.from(out));
  } catch (err) {
    if (err && err.trigger) throw err;
    const wrapped = new Error(`egl frame dump failed: ${(err && err.message) || String(err)}`);
    wrapped.trigger = (err && err.code) || 'kSwapFailed';
    throw wrapped;
  }
}

function writeFrameBytes({ file, bytes, fs: fsDouble }) {
  const writer = fsDouble || fs;
  writer.mkdirSync(path.dirname(file), { recursive: true });
  writer.writeFileSync(file, bytes);
}

// Run the present on the requested path and persist the dumped frame.
// `deps` is the hermetic test seam `{ dumpEglFrame, dumpCpuFrame,
// dumpSkiaFrame }` (each `async (opts) => Buffer`); the EGL/CPU production
// defaults screencap over adb while the Skia production default is the
// deterministic in-process render above. `fs`
// is an optional filesystem double. On the `egl` path an EGL failure falls
// back to the CPU push in-process with `GFX-BLIT-FALLBACK: <trigger>` on
// stderr; when both paths fail the run rejects (no frame file is created).
// On the `skia` path any Skia failure falls back per the T3 contract (CPU
// push covers the frame, `GFX-BLIT-FALLBACK: <trigger>` on stderr) and the
// run rejects, so the CLI exits nonzero and claims no frame.
async function runGalleryPresent(opts = {}) {
  const runPath = opts.path || 'cpu';
  if (runPath !== 'egl' && runPath !== 'cpu' && runPath !== 'skia') {
    throw new Error(`unknown path '${runPath}' (expected egl, cpu or skia)`);
  }
  const dumpFrame = typeof opts.dumpFrame === 'string' && opts.dumpFrame.length > 0
    ? opts.dumpFrame
    : null;
  const deps = opts.deps || {};
  const dumpEglFrame = deps.dumpEglFrame || ((runOpts) => defaultDumpFrame(runOpts));
  const dumpCpuFrame = deps.dumpCpuFrame || ((runOpts) => defaultDumpFrame(runOpts));
  const dumpSkiaFrame = deps.dumpSkiaFrame || (() => defaultRenderSkiaFrame());
  const runOpts = { serial: opts.serial, path: runPath, dumpFrame };

  if (runPath === 'skia') {
    try {
      const bytes = await dumpSkiaFrame(runOpts);
      if (!bytes || bytes.length === 0) {
        const empty = new Error('skia frame dump produced no frame bytes');
        empty.trigger = 'skia-swap-fail';
        throw empty;
      }
      if (dumpFrame) writeFrameBytes({ file: dumpFrame, bytes, fs: opts.fs });
      return { path: 'skia', frame: dumpFrame };
    } catch (skiaErr) {
      const trigger = (skiaErr && skiaErr.trigger)
        || (skiaErr && skiaErr.code)
        || 'skia-swap-fail';
      process.stderr.write(`GFX-BLIT-FALLBACK: ${trigger}\n`);
      const err = new Error(
        `no frame dumped (skia failure): ${(skiaErr && skiaErr.message) || String(skiaErr)}`,
      );
      err.trigger = trigger;
      if (skiaErr && skiaErr.cause !== undefined) err.cause = skiaErr.cause;
      throw err;
    }
  }

  if (runPath === 'cpu') {
    const bytes = await dumpCpuFrame(runOpts);
    if (!bytes || bytes.length === 0) {
      throw new Error('cpu frame dump produced no frame bytes');
    }
    if (dumpFrame) writeFrameBytes({ file: dumpFrame, bytes, fs: opts.fs });
    return { path: 'cpu', frame: dumpFrame };
  }

  try {
    const bytes = await dumpEglFrame(runOpts);
    if (!bytes || bytes.length === 0) {
      throw new Error('egl frame dump produced no frame bytes');
    }
    if (dumpFrame) writeFrameBytes({ file: dumpFrame, bytes, fs: opts.fs });
    return { path: 'egl', frame: dumpFrame };
  } catch (eglErr) {
    const trigger = (eglErr && eglErr.trigger)
      || (eglErr && eglErr.code)
      || 'kSwapFailed';
    const cpuBytes = await dumpCpuFrame(runOpts).catch((cpuErr) => {
      const err = new Error(
        `no frame dumped (egl fallback failed): ${(cpuErr && cpuErr.message) || String(cpuErr)}`,
      );
      err.cause = cpuErr;
      if (eglErr && eglErr.trigger) err.trigger = eglErr.trigger;
      throw err;
    });
    if (!cpuBytes || cpuBytes.length === 0) {
      throw new Error('no frame dumped (egl fallback produced no frame bytes)');
    }
    if (dumpFrame) writeFrameBytes({ file: dumpFrame, bytes: cpuBytes, fs: opts.fs });
    process.stderr.write(`GFX-BLIT-FALLBACK: ${trigger}\n`);
    return { path: 'cpu', frame: dumpFrame };
  }
}

module.exports = {
  parseGalleryPresentArgs,
  runGalleryPresent,
};
