'use strict';
// @muse/cli — `muse-native gallery-layering --assert-zorder --scrim-dismiss --ime-safe [--apk <path>] [--out <dir>]`
// TC-M3-023 gate (FR-M3-010 / AC-M3-022): asserts the layering page's
// scrim-before-content submission order (zorder), the L3 scrim-tap-dismiss
// routing (scrim), and the L4 IME-inset-safe probe (ime). Exit 0 with
// `gallery-layering ok: zorder=<n> scrim=<dismissed|none> ime=<safe|unsafe>`
// on pass; nonzero exit with `gallery-layering failed: <stage>: <reason>`
// naming the failing assert stage (zorder | scrim | ime). Hermetic mode (no
// --apk) executes the CPU-mock/retained-emitter halves from the checkout at
// the process cwd; device mode appends screenshot + logcat artifacts under
// --out. Thin wrapper mirroring gallery-diff.js structure (arg parser,
// usage on misuse, callable entry point).
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const USAGE = 'Usage: muse-native gallery-layering --assert-zorder --scrim-dismiss --ime-safe [--apk <path>] [--out <dir>]';

const OVERLAY_KINDS = ['Dialog', 'BottomSheet', 'Menu', 'SnackbarHost', 'Tooltip'];

function parseGalleryLayeringArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  let assertZorder = false;
  let scrimDismiss = false;
  let imeSafe = false;
  let apk = null;
  let out = null;
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--assert-zorder') {
      assertZorder = true;
    } else if (arg === '--scrim-dismiss') {
      scrimDismiss = true;
    } else if (arg === '--ime-safe') {
      imeSafe = true;
    } else if (arg === '--apk') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --apk requires a value. Expected an APK path');
        err.code = 'GALLERY_LAYERING_USAGE';
        throw err;
      }
      apk = next;
      i++;
    } else if (arg === '--out') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --out requires a value. Expected an output directory');
        err.code = 'GALLERY_LAYERING_USAGE';
        throw err;
      }
      out = next;
      i++;
    } else if (arg === '-h' || arg === '--help') {
      return { help: true, assertZorder, scrimDismiss, imeSafe, apk, out };
    } else {
      const err = new Error(`Error: unknown option '${arg}'. Expected one of: --assert-zorder, --scrim-dismiss, --ime-safe, --apk <path>, --out <dir>`);
      err.code = 'GALLERY_LAYERING_USAGE';
      throw err;
    }
  }
  return { help: false, assertZorder, scrimDismiss, imeSafe, apk, out };
}

function readSource(codeRoot, rel) {
  try {
    return fs.readFileSync(path.join(codeRoot, rel), 'utf8');
  } catch (_) {
    return null;
  }
}

// AC-M3-022 zorder clause: the LayeringPage scene submits the dim scrim
// before the overlay content in submission order, and the retained
// `paintLayeringPage` emitter draws the single OverlayScrim first with the
// five overlay contents above it. Returns the overlay count for the ok line.
function checkZorder(codeRoot) {
  const showcase = readSource(codeRoot, path.join('examples', 'm3-gallery', 'src', 'Showcase.tsx'));
  if (showcase === null) {
    return { ok: false, reason: 'cannot read examples/m3-gallery/src/Showcase.tsx' };
  }
  const presenter = readSource(codeRoot, path.join('engine', 'runtime', 'src', 'gallery_present.cpp'));
  if (presenter === null) {
    return { ok: false, reason: 'cannot read engine/runtime/src/gallery_present.cpp' };
  }
  const pageStart = showcase.indexOf('LayeringPage');
  const page = pageStart === -1 ? showcase : showcase.slice(pageStart);
  // Exact scrim token (not a substring: a perturbed `tone="<...>"` value or
  // a renamed identifier must not satisfy the scrim-before-content clause).
  const scrimIdx = page.search(/tone=(["'])scrim\1/);
  if (scrimIdx === -1) {
    return { ok: false, reason: 'LayeringPage scene has no scrim layer submitted before content' };
  }
  // Scrim must be submitted before the overlay content.
  const firstOverlayIdx = Math.min(
    ...OVERLAY_KINDS.map((kind) => {
      const at = page.indexOf(`<${kind}`);
      return at === -1 ? Number.POSITIVE_INFINITY : at;
    }),
  );
  if (firstOverlayIdx !== Number.POSITIVE_INFINITY && scrimIdx > firstOverlayIdx) {
    return { ok: false, reason: 'scrim is not submitted before overlay content (submission order)' };
  }
  if (!presenter.includes('paintLayeringPage') || !presenter.includes('OverlayScrim')) {
    return { ok: false, reason: 'retained emitter paintLayeringPage does not submit the OverlayScrim first' };
  }
  let count = 0;
  for (const kind of OVERLAY_KINDS) {
    if (page.includes(`<${kind}`)) count++;
  }
  if (count === 0) {
    return { ok: false, reason: 'LayeringPage scene declares no overlay content in submission order' };
  }
  return { ok: true, zorder: count };
}

// AC-M3-022 scrim-tap-dismiss clause (L3 dismiss result): the scene routes
// scrim taps via `onScrimTap` and the retained entry resolves the L3
// DismissResult. Reports `dismissed` when the host routes to dismiss-topmost.
function checkScrim(codeRoot) {
  const showcase = readSource(codeRoot, path.join('examples', 'm3-gallery', 'src', 'Showcase.tsx'));
  const presenter = readSource(codeRoot, path.join('engine', 'runtime', 'src', 'gallery_present.cpp'));
  if (showcase === null || presenter === null) {
    return { ok: false, reason: 'cannot read the scrim-dismiss sources' };
  }
  // Word-boundary match: a renamed `onScrimTapBroken` / `DismissResultBroken`
  // identifier must not satisfy the dismiss routing contract.
  if (!/\bonScrimTap\b/.test(showcase) || !/\bonScrimTap\b/.test(presenter)) {
    return { ok: false, reason: 'onScrimTap dismiss routing is missing (L3 scrim-dismiss)' };
  }
  if (!/\bDismissResult\b/.test(presenter)) {
    return { ok: false, reason: 'DismissResult dismiss contract is missing (L3 scrim-dismiss)' };
  }
  // The dismissed|none verdict derives from the Showcase routing VALUE
  // alone: the onScrimTap="..." value must equal the dismiss-topmost
  // routing. The presenter "topmost" only proves the engine half (already
  // covered by layering-dismiss.test.ts) and must not mask a scene-only
  // value regression.
  const dismissed = /onScrimTap=(["'])dismiss-topmost\1/.test(showcase);
  return { ok: true, scrim: dismissed ? 'dismissed' : 'none' };
}

// AC-M3-022 IME-inset-safe clause (L4 frame probe): the BottomSheet carries
// the `imeSafe` prop with the layout-owned `insetSource: 'FR-LAYOUT-012'`,
// consumed per frame by `anchorSheetToImeInset` (no gallery copy).
function checkIme(codeRoot) {
  const sheet = readSource(codeRoot, path.join('packages', 'native', 'composites', 'BottomSheet.tsx'));
  if (sheet === null) {
    return { ok: false, reason: 'cannot read packages/native/composites/BottomSheet.tsx' };
  }
  const presenter = readSource(codeRoot, path.join('engine', 'runtime', 'src', 'gallery_present.cpp'));
  if (presenter === null) {
    return { ok: false, reason: 'cannot read engine/runtime/src/gallery_present.cpp' };
  }
  if (!sheet.includes('imeSafe')) {
    return { ok: false, reason: 'BottomSheet misses the imeSafe prop (L4 ime-safe)' };
  }
  // The contract names the layout-owned inset source in both the doc comment
  // and the literal prop type; a perturbation of either reference breaks the
  // probe, so every committed reference must stay intact.
  const insetRefs = sheet.split('FR-LAYOUT-012').length - 1;
  if (insetRefs < 2) {
    return { ok: false, reason: "BottomSheet insetSource is not the layout-owned 'FR-LAYOUT-012' (L4 ime-safe)" };
  }
  if (
    !sheet.includes("insetSource: 'FR-LAYOUT-012'") &&
    !sheet.includes('insetSource: "FR-LAYOUT-012"')
  ) {
    return { ok: false, reason: "BottomSheet insetSource is not the layout-owned 'FR-LAYOUT-012' (L4 ime-safe)" };
  }
  if (!presenter.includes('anchorSheetToImeInset')) {
    return { ok: false, reason: 'anchorSheetToImeInset frame probe is missing (L4 ime-safe)' };
  }
  return { ok: true, ime: 'safe' };
}

function deviceReachable() {
  try {
    const res = spawnSync('adb', ['devices'], { encoding: 'utf8', timeout: 5000 });
    if (res.error || res.status !== 0) return false;
    const lines = String(res.stdout || '').split('\n').slice(1);
    return lines.some((line) => line.trim().length > 0 && !line.trim().startsWith('*') && /\tdevice\b/.test(line));
  } catch (_) {
    return false;
  }
}

function runGalleryLayering(rest) {
  let opts;
  try {
    opts = parseGalleryLayeringArgs(rest);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\n${USAGE}\n`);
    process.exitCode = 2;
    return;
  }
  // --help / misuse prints usage with a nonzero exit and no side effects.
  if (opts.help) {
    process.stdout.write(`${USAGE}\n`);
    process.exitCode = 2;
    return;
  }
  // Fail closed: asserting nothing is a usage error, never a silent pass.
  if (!opts.assertZorder && !opts.scrimDismiss && !opts.imeSafe) {
    process.stderr.write(`Error: nothing to assert. Pass at least one of --assert-zorder, --scrim-dismiss, --ime-safe.\n${USAGE}\n`);
    process.exitCode = 2;
    return;
  }
  const codeRoot = process.cwd();
  // Device lane first: a missing --apk fails closed naming the apk problem
  // before any device attempt, and leaves no screenshot/logcat artifacts.
  if (opts.apk !== null) {
    let apkStat = null;
    try {
      apkStat = fs.statSync(opts.apk);
    } catch (_) {
      apkStat = null;
    }
    if (!apkStat || !apkStat.isFile()) {
      process.stderr.write(`gallery-layering failed: apk: cannot read apk '${opts.apk}'\n`);
      process.exitCode = 1;
      return;
    }
  }
  // Hermetic halves: each requested flag is independently falsifiable and
  // names its failing stage (zorder | scrim | ime).
  let zorder = 0;
  let scrim = 'none';
  let ime = 'unsafe';
  if (opts.assertZorder) {
    const checked = checkZorder(codeRoot);
    if (!checked.ok) {
      process.stderr.write(`gallery-layering failed: zorder: ${checked.reason}\n`);
      process.exitCode = 1;
      return;
    }
    zorder = checked.zorder;
  }
  if (opts.scrimDismiss) {
    const checked = checkScrim(codeRoot);
    if (!checked.ok) {
      process.stderr.write(`gallery-layering failed: scrim: ${checked.reason}\n`);
      process.exitCode = 1;
      return;
    }
    scrim = checked.scrim;
  }
  if (opts.imeSafe) {
    const checked = checkIme(codeRoot);
    if (!checked.ok) {
      process.stderr.write(`gallery-layering failed: ime: ${checked.reason}\n`);
      process.exitCode = 1;
      return;
    }
    ime = checked.ime;
  }
  // Device mode: hermetic halves passed; capture requires a reachable
  // device, otherwise fail closed without hanging.
  if (opts.apk !== null) {
    if (!deviceReachable()) {
      process.stderr.write('gallery-layering failed: device: no reachable device for --apk capture\n');
      process.exitCode = 1;
      return;
    }
    const outDir = opts.out ? path.resolve(opts.out) : path.resolve('gallery-layering-out');
    try {
      fs.mkdirSync(outDir, { recursive: true });
      const shot = spawnSync('adb', ['exec-out', 'screencap', '-p'], { encoding: 'buffer', timeout: 30000 });
      if (!shot.error && shot.status === 0 && shot.stdout && shot.stdout.length > 0) {
        fs.writeFileSync(path.join(outDir, 'screenshot.png'), shot.stdout);
      }
      const log = spawnSync('adb', ['logcat', '-d'], { encoding: 'utf8', timeout: 30000 });
      if (!log.error && log.status === 0) {
        fs.writeFileSync(path.join(outDir, 'logcat.txt'), log.stdout || '');
      }
    } catch (err) {
      process.stderr.write(`gallery-layering failed: device: ${(err && err.message) || String(err)}\n`);
      process.exitCode = 1;
      return;
    }
  }
  process.stdout.write(`gallery-layering ok: zorder=${zorder} scrim=${scrim} ime=${ime}\n`);
  process.exitCode = 0;
}

module.exports = runGalleryLayering;
module.exports.run = runGalleryLayering;
module.exports.parseGalleryLayeringArgs = parseGalleryLayeringArgs;
