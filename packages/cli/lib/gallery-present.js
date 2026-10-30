'use strict';
// gallery-present device command — FR-GRAPHICS-012 (AC-GRAPHICS-033, TC-GRAPHICS-033).
//
// Presents the gallery scene through the EGL path with a frame dump, falling
// back to the CPU push in-process when EGL is unavailable. The fallback
// resolves `{ path: 'cpu', frame }` and reports the trigger on stderr as
// `GFX-BLIT-FALLBACK: <trigger>` (exit 0 only when a frame was dumped).

const fs = require('node:fs');
const path = require('node:path');

// Parse `muse-native gallery-present [--path egl|cpu] [--dump-frame <png>]`.
// The leading `gallery-present` token is optional. Unknown `--path` values,
// unknown flags, and missing `--path`/`--dump-frame` values throw (the CLI
// maps these to exit 2 with usage, never a silent pass).
function parseGalleryPresentArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-present') args.shift();
  const parsed = { path: 'cpu', dumpFrame: null };
  const usage = 'Usage: muse-native gallery-present [--path egl|cpu] [--dump-frame <png>]';
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--path') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        throw new Error(`Error: --path: Expected one of: egl, cpu\n${usage}`);
      }
      if (next !== 'egl' && next !== 'cpu') {
        throw new Error(`Error: --path: unknown path '${next}' (expected egl or cpu)\n${usage}`);
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
// `deps` is the hermetic test seam `{ dumpEglFrame, dumpCpuFrame }` (each
// `async (opts) => Buffer`); production defaults screencap over adb. `fs`
// is an optional filesystem double. On the `egl` path an EGL failure falls
// back to the CPU push in-process with `GFX-BLIT-FALLBACK: <trigger>` on
// stderr; when both paths fail the run rejects (no frame file is created).
async function runGalleryPresent(opts = {}) {
  const runPath = opts.path || 'cpu';
  if (runPath !== 'egl' && runPath !== 'cpu') {
    throw new Error(`unknown path '${runPath}' (expected egl or cpu)`);
  }
  const dumpFrame = typeof opts.dumpFrame === 'string' && opts.dumpFrame.length > 0
    ? opts.dumpFrame
    : null;
  const deps = opts.deps || {};
  const dumpEglFrame = deps.dumpEglFrame || ((runOpts) => defaultDumpFrame(runOpts));
  const dumpCpuFrame = deps.dumpCpuFrame || ((runOpts) => defaultDumpFrame(runOpts));
  const runOpts = { serial: opts.serial, path: runPath, dumpFrame };

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
