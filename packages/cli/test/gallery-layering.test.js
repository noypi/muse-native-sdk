'use strict';
// Batch 2 IME-inset-safe sheet — FR-M3-010 / AC-M3-022 IME-inset-safe
// clause (checklist L4). Hermetic halves of the L4 contract, owned here
// per the plan Batch 2 row (`packages/cli/test/gallery-layering.test.js`
// hermetic halves); the device frame-capture proof belongs to L6/tester.
//
// Interface (plan L4 verbatim):
//   `BottomSheetProps { open: boolean; imeSafe: true; insetSource:
//   'FR-LAYOUT-012' }` — when `open && imeSafe`, the sheet consumes the
//   layout-owned animated inset per frame; any other `insetSource` value
//   fails the build. `none` beyond the prop contract — geometry proof is
//   frame captures, not a new callable.
//
// File choice: the two relocated `imeSafe` hermetic assertions (manifest
// schema + composite echo, recorded in the Batch 1 L4 stub) assert CLI- and
// gate-visible surface, and the Batch 2 plan row assigns hermetic halves to
// this CLI test file — so they are restored HERE, not by editing Batch 1's
// `packages/native/test/layering.test.ts` (which stays byte-identical).
// Hermetic: node:test + node:assert/strict + stdlib only. Reads committed
// sources from disk; no network, no secrets, no services, no device.
// ASSUMPTION: `insetSource` is pinned as the literal type `'FR-LAYOUT-012'`
//   so any other value is a build-time type error (the plan's "fails the
//   build"); the coder owns the exact gate wiring, this file pins the
//   literal contract.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const CHECKOUT_ROOT = path.join(CLI_DIR, '..', '..');
const NATIVE_ROOT = path.join(CHECKOUT_ROOT, 'packages', 'native');
const MANIFEST_PATH = path.join(NATIVE_ROOT, 'muse.manifest.json');
const BOTTOM_SHEET_PATH = path.join(NATIVE_ROOT, 'composites', 'BottomSheet.tsx');
const PRESENTER_PATH = path.join(
  CHECKOUT_ROOT, 'engine', 'runtime', 'src', 'gallery_present.cpp',
);

function read(p) {
  return fs.readFileSync(p, 'utf8');
}

describe('L4 — IME-inset-safe sheet prop contract (AC-M3-022)', () => {
  it('BottomSheet manifest schema exposes the imeSafe prop contract', () => {
    const manifest = JSON.parse(read(MANIFEST_PATH));
    const entry = Array.isArray(manifest.components)
      ? manifest.components.find((c) => c && c.name === 'BottomSheet')
      : (manifest.components?.BottomSheet ?? manifest.BottomSheet);
    assert.ok(entry, 'muse.manifest.json must declare a BottomSheet component entry');
    assert.equal(
      entry.props?.imeSafe?.type,
      'boolean',
      'BottomSheet manifest entry must declare `imeSafe: boolean`',
    );
  });

  it('BottomSheet composite echoes the imeSafe prop', () => {
    const src = read(BOTTOM_SHEET_PATH);
    assert.ok(
      src.includes('imeSafe'),
      'composites/BottomSheet.tsx must echo the `imeSafe` prop',
    );
  });

  it("BottomSheetProps pins insetSource to 'FR-LAYOUT-012'", () => {
    const src = read(BOTTOM_SHEET_PATH);
    assert.ok(
      src.includes('FR-LAYOUT-012'),
      "BottomSheetProps must name the layout-owned inset source 'FR-LAYOUT-012'",
    );
    assert.ok(
      src.includes('imeSafe') && src.includes('open'),
      'BottomSheetProps must carry { open, imeSafe } for the open && imeSafe gate',
    );
  });

  it('open && imeSafe consumes the animated inset per frame (no gallery copy)', () => {
    const presenter = read(PRESENTER_PATH);
    const composite = read(BOTTOM_SHEET_PATH);
    assert.ok(
      presenter.includes('anchorSheetToImeInset'),
      'engine must bind sheet Y via anchorSheetToImeInset per frame (FR-LAYOUT-012 consumption)',
    );
    assert.ok(
      composite.includes('insetSource'),
      'BottomSheet must declare the insetSource contract so open && imeSafe gates layout-owned inset consumption',
    );
    assert.ok(
      !composite.includes('sheetY') && !composite.includes('anchorSheet'),
      'BottomSheet composite must not copy inset into gallery state — consumption only, no geometry math here',
    );
  });

  it("any other insetSource value fails the build", () => {
    const src = read(BOTTOM_SHEET_PATH);
    assert.ok(
      src.includes("insetSource: 'FR-LAYOUT-012'") ||
        src.includes('insetSource: "FR-LAYOUT-012"'),
      "insetSource must be the literal type 'FR-LAYOUT-012' so any other value is a build-time type error",
    );
  });

  it('geometry proof stays frame captures — no new callable beyond the prop contract', () => {
    const src = read(BOTTOM_SHEET_PATH);
    assert.ok(
      !src.includes('getSheetGeometry') && !src.includes('computeSheetY'),
      'BottomSheet must expose no geometry callable — frame-capture proof only (L6/tester)',
    );
  });
});

// ─── Batch 3 — L5 executable gate (TC-M3-023) ───
// FR-M3-010 / AC-M3-022 / TC-M3-023 (checklist L5). APPENDED — the 6 green L4
// cases above are untouched. Hermetic: node:test + node:assert/strict +
// stdlib only. Spawns `node bin/muse-native.js` (commands/gallery-diff
// pattern); no device, no network, no secrets, no live services.
// Interface (plan L5 verbatim):
//   `muse-native gallery-layering --assert-zorder --scrim-dismiss --ime-safe
//   [--apk <path>] [--out <dir>]` — exit 0 with `gallery-layering ok:
//   zorder=<n> scrim=<dismissed|none> ime=<safe|unsafe>` on pass; nonzero
//   exit with `gallery-layering failed: <stage>: <reason>` naming the failing
//   assert stage (zorder | scrim | ime). Hermetic mode (no device) executes
//   the CPU-mock/retained-emitter halves; device mode appends screenshot +
//   logcat artifacts under --out.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-L5-1 (hermetic seam): hermetic halves resolve the checkout from the
//   process cwd by default (gallery-diff `--code-root` default convention)
//   and detect (a) scrim-before-content submission order in the LayeringPage
//   scene + `paintLayeringPage` emitter, (b) the `onScrimTap` dismiss routing
//   (L3), (c) the `imeSafe`/`insetSource: 'FR-LAYOUT-012'` contract (L4).
//   Fixtures are exact copies of the run-checkout sources with one stage
//   perturbed; the coder owns detection internals — reconcile paths, not
//   verdicts, at implementation time.
// - A-L5-2 (fail closed): bare `gallery-layering` with no assert flag asserts
//   nothing, so it is a usage error (nonzero), never a silent pass
//   (RULE-RUNTIME-001 style, cf. gallery-diff missing---tolerance).
// - A-L5-3 (device lane): a present `--apk` selects device mode; with no
//   reachable device it fails closed nonzero without hanging. Screenshot +
//   logcat capture itself is tester/L6 device-proof territory (EXTERNAL).

const CLI_BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
// CHECKOUT_ROOT is declared by the L4 block above and reused here.

let galleryLayering = null;
try {
  // eslint-disable-next-line global-require
  galleryLayering = require('../lib/gallery-layering.js');
} catch {
  galleryLayering = null;
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

function runGate(args, options = {}) {
  const env = { ...process.env };
  delete env.ANDROID_SERIAL;
  return spawnSync(process.execPath, [CLI_BIN_NATIVE, ...args], {
    encoding: 'utf8',
    cwd: CHECKOUT_ROOT,
    env,
    ...options,
  });
}

const OK_LINE =
  /^gallery-layering ok: zorder=\d+ scrim=(dismissed|none) ime=(safe|unsafe)/m;

// Fixture checkout: exact copies of the sources the hermetic halves read;
// `mutate` perturbs exactly one stage. Never touches production code.
const LAYERING_SOURCE_FILES = [
  path.join('examples', 'm3-gallery', 'src', 'Showcase.tsx'),
  path.join('examples', 'm3-gallery', 'src', 'App.tsx'),
  path.join('packages', 'native', 'muse.manifest.json'),
  path.join('packages', 'native', 'composites', 'BottomSheet.tsx'),
  path.join('packages', 'native', 'composites', 'Dialog.tsx'),
  path.join('engine', 'runtime', 'src', 'gallery_present.cpp'),
  path.join('engine', 'runtime', 'include', 'runtime', 'gallery_present.h'),
];

function writeFixtureRoot(mutate) {
  const root = mkTmp('muse-glayer-');
  for (const rel of LAYERING_SOURCE_FILES) {
    const dst = path.join(root, rel);
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    fs.copyFileSync(path.join(CHECKOUT_ROOT, rel), dst);
  }
  if (mutate) mutate(root);
  return root;
}

function replaceFirstInFile(root, rel, from, to) {
  const p = path.join(root, rel);
  const text = fs.readFileSync(p, 'utf8');
  assert.ok(
    text.includes(from),
    `fixture precondition: ${rel} must contain '${from}'`,
  );
  fs.writeFileSync(p, text.replace(from, to));
}

function listFilesRecursive(dir) {
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...listFilesRecursive(p));
    else out.push(p);
  }
  return out;
}

describe('L5 — gallery-layering module surface (TC-M3-023)', () => {
  it('packages/cli/lib/gallery-layering.js exists and loads', () => {
    assert.ok(
      galleryLayering !== null,
      'lib/gallery-layering.js must exist (thin wrapper mirroring gallery-diff.js)',
    );
  });

  it('module exports a callable gate entry point', () => {
    assert.ok(galleryLayering, 'lib/gallery-layering.js must load');
    const exported =
      typeof galleryLayering === 'function'
        ? galleryLayering
        : galleryLayering.run;
    assert.equal(
      typeof exported,
      'function',
      'gallery-layering module must export a callable entry point',
    );
  });

  it('wrapper covers the three AC-M3-022 stages (zorder | scrim | ime)', () => {
    assert.ok(galleryLayering, 'lib/gallery-layering.js must load');
    const parts = [];
    if (typeof galleryLayering === 'function') parts.push(galleryLayering.toString());
    else {
      for (const key of Object.keys(galleryLayering)) {
        parts.push(key);
        if (typeof galleryLayering[key] === 'function') {
          parts.push(galleryLayering[key].toString());
        }
      }
    }
    const src = parts.join('\n');
    for (const stage of ['zorder', 'scrim', 'ime']) {
      assert.ok(
        src.toLowerCase().includes(stage),
        `wrapper must reference the '${stage}' assert stage`,
      );
    }
    assert.ok(
      /usage/i.test(src),
      'wrapper must print usage on misuse (gallery-diff structure)',
    );
  });
});

describe('L5 — cli.js COMMANDS + dispatch registration', () => {
  const CLI_PATH = path.join(CLI_DIR, 'lib', 'cli.js');

  it("COMMANDS includes 'gallery-layering'", () => {
    const src = fs.readFileSync(CLI_PATH, 'utf8');
    assert.ok(
      src.includes('gallery-layering'),
      "cli.js COMMANDS must include 'gallery-layering'",
    );
  });

  it('dispatch wires argv to lib/gallery-layering.js', () => {
    const src = fs.readFileSync(CLI_PATH, 'utf8');
    assert.ok(
      src.includes('gallery-layering.js'),
      'cli.js must require ./gallery-layering.js in dispatch',
    );
  });

  it('help names the gallery-layering command', () => {
    const src = fs.readFileSync(CLI_PATH, 'utf8');
    assert.ok(
      /gallery-layering/.test(src) && /Usage: muse-native/.test(src),
      'cli.js help/usage must name gallery-layering',
    );
  });
});

describe('L5 — gate exit contract (TC-M3-023)', () => {
  it('exit 0 with the ok line on the correct hermetic scene', () => {
    const res = runGate([
      'gallery-layering',
      '--assert-zorder',
      '--scrim-dismiss',
      '--ime-safe',
    ]);
    assert.equal(
      res.status,
      0,
      `expected exit 0 on the run checkout, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`,
    );
    assert.ok(
      OK_LINE.test(res.stdout || ''),
      `stdout must match 'gallery-layering ok: zorder=<n> scrim=<dismissed|none> ime=<safe|unsafe>'; got: ${(res.stdout || '').slice(0, 300)}`,
    );
  });

  it('exact-copy fixture checkout also passes (seam control)', () => {
    const root = writeFixtureRoot(null);
    const res = runGate(
      ['gallery-layering', '--assert-zorder', '--scrim-dismiss', '--ime-safe'],
      { cwd: root },
    );
    assert.equal(
      res.status,
      0,
      `expected exit 0 on the unperturbed fixture, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`,
    );
    assert.ok(
      OK_LINE.test(res.stdout || ''),
      `fixture stdout must carry the ok line; got: ${(res.stdout || '').slice(0, 300)}`,
    );
  });
});

describe('L5 — each flag independently falsifiable (AC-M3-022)', () => {
  it('broken zorder fails naming the zorder stage', () => {
    const root = writeFixtureRoot((r) =>
      replaceFirstInFile(
        r,
        path.join('examples', 'm3-gallery', 'src', 'Showcase.tsx'),
        'scrim',
        'no-scrim-here-broken',
      ),
    );
    const res = runGate(
      ['gallery-layering', '--assert-zorder', '--scrim-dismiss', '--ime-safe'],
      { cwd: root },
    );
    assert.notEqual(res.status, 0, 'broken zorder must exit nonzero');
    assert.ok(
      /gallery-layering failed: zorder: /.test(res.stderr || ''),
      `stderr must name the failing stage 'gallery-layering failed: zorder: <reason>'; got: ${(res.stderr || '').slice(0, 300)}`,
    );
  });

  it('broken scrim-dismiss fails naming the scrim stage', () => {
    const root = writeFixtureRoot((r) => {
      for (const rel of [
        path.join('examples', 'm3-gallery', 'src', 'Showcase.tsx'),
        path.join('engine', 'runtime', 'src', 'gallery_present.cpp'),
      ]) {
        const p = path.join(r, rel);
        const text = fs.readFileSync(p, 'utf8');
        if (text.includes('onScrimTap')) {
          fs.writeFileSync(p, text.split('onScrimTap').join('onScrimTapBroken'));
        }
      }
      const presenter = path.join(
        r,
        'engine',
        'runtime',
        'src',
        'gallery_present.cpp',
      );
      const cpp = fs.readFileSync(presenter, 'utf8');
      if (cpp.includes('DismissResult')) {
        fs.writeFileSync(presenter, cpp.split('DismissResult').join('DismissResultBroken'));
      }
    });
    const res = runGate(
      ['gallery-layering', '--assert-zorder', '--scrim-dismiss', '--ime-safe'],
      { cwd: root },
    );
    assert.notEqual(res.status, 0, 'broken scrim-dismiss must exit nonzero');
    assert.ok(
      /gallery-layering failed: scrim: /.test(res.stderr || ''),
      `stderr must name the failing stage 'gallery-layering failed: scrim: <reason>'; got: ${(res.stderr || '').slice(0, 300)}`,
    );
  });

  it('broken ime-safe fails naming the ime stage', () => {
    const root = writeFixtureRoot((r) =>
      replaceFirstInFile(
        r,
        path.join('packages', 'native', 'composites', 'BottomSheet.tsx'),
        'FR-LAYOUT-012',
        'BROKEN-INSET-SOURCE',
      ),
    );
    const res = runGate(
      ['gallery-layering', '--assert-zorder', '--scrim-dismiss', '--ime-safe'],
      { cwd: root },
    );
    assert.notEqual(res.status, 0, 'broken ime-safe must exit nonzero');
    assert.ok(
      /gallery-layering failed: ime: /.test(res.stderr || ''),
      `stderr must name the failing stage 'gallery-layering failed: ime: <reason>'; got: ${(res.stderr || '').slice(0, 300)}`,
    );
  });
});

describe('L5 — help and misuse (usage + nonzero, no side effects)', () => {
  it('--help prints usage and exits nonzero without side effects', () => {
    const root = mkTmp('muse-glayer-help-');
    const res = runGate(['gallery-layering', '--help'], { cwd: root });
    assert.notEqual(res.status, 0, '--help must exit nonzero per the L5 contract');
    assert.ok(
      /Usage: muse-native gallery-layering/.test(`${res.stdout || ''}\n${res.stderr || ''}`),
      `help must print 'Usage: muse-native gallery-layering ...'; got stdout: ${(res.stdout || '').slice(0, 200)} stderr: ${(res.stderr || '').slice(0, 200)}`,
    );
    assert.deepEqual(
      fs.readdirSync(root),
      [],
      '--help must not write side effects into the cwd',
    );
  });

  it('unknown flag exits 2 with usage and no side effects', () => {
    const root = mkTmp('muse-glayer-unknown-');
    const outDir = path.join(root, 'out');
    const res = runGate(['gallery-layering', '--bogus-flag', '--out', outDir], {
      cwd: root,
    });
    assert.equal(
      res.status,
      2,
      `unknown flag must exit 2 (usage error), got ${res.status}`,
    );
    assert.ok(
      /Usage: muse-native gallery-layering/.test(`${res.stdout || ''}\n${res.stderr || ''}`),
      'misuse must print usage naming gallery-layering',
    );
    assert.ok(
      !fs.existsSync(outDir) ||
        listFilesRecursive(outDir).filter((f) => f.endsWith('.png')).length === 0,
      'misuse must not produce artifacts under --out',
    );
  });

  it('--apk without a value exits 2 (usage error)', () => {
    const res = runGate([
      'gallery-layering',
      '--assert-zorder',
      '--scrim-dismiss',
      '--ime-safe',
      '--apk',
    ]);
    assert.equal(res.status, 2, `--apk without a value must exit 2, got ${res.status}`);
  });

  it('--out without a value exits 2 (usage error)', () => {
    const res = runGate([
      'gallery-layering',
      '--assert-zorder',
      '--scrim-dismiss',
      '--ime-safe',
      '--out',
    ]);
    assert.equal(res.status, 2, `--out without a value must exit 2, got ${res.status}`);
  });

  it('bare gallery-layering with no assert flag exits nonzero (fail closed)', () => {
    const res = runGate(['gallery-layering']);
    assert.notEqual(
      res.status,
      0,
      'no assert flags means nothing to assert — must not pass silently',
    );
  });
});

describe('L5 — device mode fails closed hermetically (no device)', () => {
  it('nonexistent --apk fails closed naming the apk problem, no artifacts', () => {
    const root = mkTmp('muse-glayer-apk-');
    const outDir = path.join(root, 'out');
    const res = runGate(
      [
        'gallery-layering',
        '--assert-zorder',
        '--scrim-dismiss',
        '--ime-safe',
        '--apk',
        path.join(root, 'no-such-gallery.apk'),
        '--out',
        outDir,
      ],
      { cwd: root },
    );
    assert.notEqual(res.status, 0, 'missing --apk must exit nonzero, never attempt adb');
    assert.ok(
      /apk/i.test(res.stderr || ''),
      `stderr must name the apk problem; got: ${(res.stderr || '').slice(0, 300)}`,
    );
    const artifacts = fs.existsSync(outDir) ? listFilesRecursive(outDir) : [];
    assert.ok(
      artifacts.filter((f) => f.endsWith('.png') || /logcat/i.test(f)).length === 0,
      'failed device lane must not leave screenshot/logcat artifacts',
    );
  });

  it('--apk present with no reachable device fails closed without hanging', () => {
    const root = mkTmp('muse-glayer-nodev-');
    const apk = path.join(root, 'gallery.apk');
    fs.writeFileSync(apk, 'apk-stand-in-not-a-real-package\n');
    const res = runGate(
      [
        'gallery-layering',
        '--assert-zorder',
        '--scrim-dismiss',
        '--ime-safe',
        '--apk',
        apk,
        '--out',
        path.join(root, 'out'),
      ],
      { cwd: root, timeout: 60000 },
    );
    assert.notEqual(
      res.status,
      0,
      'device mode with no reachable device must fail closed nonzero',
    );
  });
});

// ─── Batch 3 — L6 regression + device-proof wiring ───
// Interface (plan L6 verbatim):
//   `none — proof procedure and regression tests, no new production surface
//   beyond L1–L5`.
// The regression halves themselves (scrim-first, single-OverlayHost, dismiss
// contract, imeSafe prop, emitter order + inset-step probe) are already green
// in `packages/native/test/layering.test.ts`,
// `packages/native/test/layering-dismiss.test.ts`, and
// `engine/runtime/tests/gallery_layering_test.cpp` — no new cases there. The
// new TEST half below pins the `gallery_layering_test` executable +
// `add_test` CMake wiring L6 owns (planner decision); the coder implements
// that wiring. The `device-proof.md` procedure + evidence log (run-local) is
// coder-authored and tester-executed — marked EXTERNAL in the L6 contract, no
// unit-test impersonation of a device run.

describe('L6 — gallery_layering_test executable + add_test wiring', () => {
  const CMAKE_PATH = path.join(
    CHECKOUT_ROOT,
    'engine',
    'runtime',
    'CMakeLists.txt',
  );
  const LAYERING_CPP = path.join(
    CHECKOUT_ROOT,
    'engine',
    'runtime',
    'tests',
    'gallery_layering_test.cpp',
  );

  it('engine binary source exists', () => {
    assert.ok(
      fs.existsSync(LAYERING_CPP),
      'engine/runtime/tests/gallery_layering_test.cpp must exist',
    );
  });

  it('CMakeLists defines the gallery_layering_test executable', () => {
    const src = fs.readFileSync(CMAKE_PATH, 'utf8');
    assert.ok(
      /add_executable\(\s*gallery_layering_test\s+tests\/gallery_layering_test\.cpp\s*\)/.test(
        src,
      ),
      'engine/runtime/CMakeLists.txt must define add_executable(gallery_layering_test tests/gallery_layering_test.cpp)',
    );
  });

  it('CMakeLists registers the gallery_layering_test ctest', () => {
    const src = fs.readFileSync(CMAKE_PATH, 'utf8');
    assert.ok(
      /add_test\(\s*NAME\s+gallery_layering_test\s+COMMAND\s+gallery_layering_test\s*\)/.test(
        src,
      ),
      'engine/runtime/CMakeLists.txt must register add_test(NAME gallery_layering_test COMMAND gallery_layering_test)',
    );
  });
});
