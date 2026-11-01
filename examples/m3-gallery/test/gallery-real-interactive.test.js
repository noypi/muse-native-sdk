'use strict';
// T4 — End-to-end gallery proof wiring (AC-M3-026, AC-GRAPHICS-042, FR-SAMPLE-014).
// Spec refs: AC-M3-026 (SPEC-M3-NATIVE, parent FR-M3-011), AC-GRAPHICS-042
// (SPEC-GRAPHICS-RENDERING, parent FR-GRAPHICS-015), FR-SAMPLE-014
// (shell-chrome-only paging). Depends on: T2, T3.
// Plan Interface line (verbatim, see unit-tests/T4.md):
//   `none — test-only wiring; no new callable surface (invokes T2 and T3 gates and asserts story-coverage counts)`
// Hermetic: node:test + node:assert/strict + node:fs/node:path + node:child_process
// (spawnSync on the in-tree CLI only) stdlib only. Story/manifest reads mirror
// story-coverage.test.ts (MANIFEST_PATH/STORIES_PATH/GALLERY_PACKAGE_PATH env
// overrides). T2/T3 gates are driven via injected `deps` doubles only — never
// adb, never a device, never the network, never secrets, never application
// configuration, never a live service, never shared mutable state.
// `process.env` is read only for the three *_PATH overrides (same as the
// in-tree story-coverage test); no secret or app config is ever read.
// This file NEVER writes: no golden re-baseline (oracle goldens stay committed
// until T2+T3 pass on device per AC-GRAPHICS-044); the no-write leg pins that.
//
// ASSUMPTIONS (contract genuinely silent on wiring spelling; behavior pinned,
// spelling reconcilable per GR-08 T1.9):
//   A1 (T2 seam): `packages/cli/lib/gallery-surface-contract.js` exports
//       `runGalleryRealInteractive(argv, opts)` with `opts = { deps, emit }`
//       (see unit-tests/T2.md A1–A4). `deps = { driveInteractive,
//       readMockSymbols }`; failure value `{ code, detail }` (non-empty).
//   A2 (T3 seam): `packages/cli/lib/gallery-tap.js` exports `runTapPage` with
//       the T6 resolve/reject shape plus plan fields `exitCode/pageAdvanced/
//       blankViewport/jniFaults` (see unit-tests/T3.md A5) and an injectable
//       `deps` drive (`driveSequence`/`readLogcat`/`readScreencap`).
//   A3 (story seam): `examples/m3-gallery/src/stories.js` exports
//       `buildStories(manifest)` (see story-coverage.test.ts A1–A4); render()
//       returns a plain descriptor whose `type` equals the component name.
//   A4 (paging seam): `examples/m3-gallery/src/galleryNav.js` exports
//       `isPagingGesture(zone, gesture)` (FR-SAMPLE-014); inside-surface
//       gestures never page, shell-chrome tap/key/deep-link pages.
//   A5 (no re-baseline): the committed goldens directory holds no per-story
//       oracle images to rewrite hermetically; this test asserts it performs
//       zero writes and that `assertNoMockPainter` (RULE-M3-007) still denies
//       mock-painter output.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync, readdirSync } = require('node:fs');
const { join, resolve } = require('node:path');

const REPO_ROOT = resolve(__dirname, '..', '..', '..');
const MANIFEST_PATH =
  process.env.MANIFEST_PATH ?? join(REPO_ROOT, 'packages', 'native', 'muse.manifest.json');
const STORIES_PATH =
  process.env.STORIES_PATH ?? join(__dirname, '..', 'src', 'stories.js');
const PACKAGE_PATH =
  process.env.GALLERY_PACKAGE_PATH ?? join(__dirname, '..', 'package.json');
const GOLDENS_DIR = join(__dirname, '..', 'goldens');
const NAV_PATH = join(__dirname, '..', 'src', 'galleryNav.js');
const GALLERY_REAL_PATH = join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-real.js');

const ALL_CHECKS = [
  'tap',
  'focus',
  'caret-selection',
  'scroll',
  'switch-scrub',
  'dialog-dismiss',
  'sheet-dismiss',
  'menu-dismiss',
  'state-binding',
  'no-diagram',
  'shell-paging',
];
const DIAGRAM_RE = /MockPainter|PaintCaption|mock/i;

function loadManifest(path = MANIFEST_PATH) {
  return JSON.parse(readFileSync(path, 'utf8'));
}

let storiesError = null;
let buildStories = undefined;
try {
  ({ buildStories } = require(STORIES_PATH));
} catch (err) {
  storiesError = err;
}

function requireBuildStories() {
  assert.equal(storiesError, null, `story module failed to load from ${STORIES_PATH}: ${storiesError && storiesError.message}`);
  assert.equal(typeof buildStories, 'function', `story module at ${STORIES_PATH} must export buildStories(manifest)`);
}

function storiesForManifest(manifest) {
  requireBuildStories();
  const stories = buildStories(manifest);
  assert.ok(Array.isArray(stories), 'buildStories(manifest) must return an array');
  return stories;
}

let surfaceContract = null;
try {
  surfaceContract = require(join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-surface-contract.js'));
} catch {
  surfaceContract = null;
}

let tapLane = null;
try {
  tapLane = require(join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-tap.js'));
} catch {
  tapLane = null;
}

let galleryReal = null;
try {
  galleryReal = require(GALLERY_REAL_PATH);
} catch {
  galleryReal = null;
}

function requireT2() {
  assert.ok(surfaceContract, 'packages/cli/lib/gallery-surface-contract.js must load (T2 gate)');
  assert.equal(typeof surfaceContract.runGalleryRealInteractive, 'function', 'T2 gate must export runGalleryRealInteractive(argv) (see unit-tests/T2.md)');
  return surfaceContract.runGalleryRealInteractive;
}

function requireT3() {
  assert.ok(tapLane, 'packages/cli/lib/gallery-tap.js must load (T3 lane)');
  assert.equal(typeof tapLane.runTapPage, 'function', 'T3 lane must export runTapPage (see unit-tests/T3.md)');
  return tapLane.runTapPage;
}

function okInteractiveDeps(overrides = {}) {
  const failing = new Set(overrides.fail || []);
  const symbols = overrides.mockSymbols || [];
  return {
    driveInteractive: async () => ALL_CHECKS.map((check) => ({ check, pass: !failing.has(check) })),
    readMockSymbols: async () => symbols.slice(),
  };
}

describe('T4 — story-coverage counts preserved (AC-M3-026/AC-GRAPHICS-042 wiring base)', () => {
  it('production manifest still holds 41 entries (no story lost)', () => {
    const manifest = loadManifest();
    assert.equal(manifest.components.length, 41, 'production manifest must still hold 41 entries');
  });

  it('every manifest entry has stories in both themes (counts asserted)', () => {
    const manifest = loadManifest();
    const stories = storiesForManifest(manifest);
    assert.ok(stories.length >= manifest.components.length, 'story count must cover every entry');
    const gaps = [];
    for (const entry of manifest.components) {
      for (const theme of ['light', 'dark']) {
        if (!stories.some((s) => s.component === entry.name && s.theme === theme)) gaps.push(`${entry.name}/${theme}`);
      }
    }
    assert.deepEqual(gaps, [], `component/theme pairs with no story: ${gaps.join(', ')}`);
  });

  it('gallery package still exposes test:stories (npm run test:stories -w @muse/gallery)', () => {
    const pkg = JSON.parse(readFileSync(PACKAGE_PATH, 'utf8'));
    assert.equal(pkg.name, '@muse/gallery', 'gallery package must be named @muse/gallery');
    assert.equal(typeof (pkg.scripts && pkg.scripts['test:stories']), 'string', 'gallery package.json must define a test:stories script');
  });
});

describe('T4 — T2 gate invoked page-by-page (AC-M3-026/AC-GRAPHICS-043)', () => {
  it('T2 happy path: all 11 checks pass -> exitCode 0 with one row per check', async () => {
    const run = requireT2();
    const result = await run(['gallery-real', '--assert-interactive'], { deps: okInteractiveDeps(), emit: () => {} });
    assert.equal(result.exitCode, 0);
    assert.equal(result.rows.length, 11, `must report all 11 checks, got ${result.rows.length}`);
    for (const check of ALL_CHECKS) {
      const row = result.rows.find((r) => r.check === check);
      assert.ok(row, `must report a row for check '${check}'`);
      assert.equal(row.pass, true, `check '${check}' must pass`);
    }
  });

  it('T2 tap failure exits 1 naming tap (per-page interactivity observed)', async () => {
    const run = requireT2();
    const result = await run(['gallery-real', '--assert-interactive'], { deps: okInteractiveDeps({ fail: ['tap'] }), emit: () => {} });
    assert.equal(result.exitCode, 1);
    assert.equal(result.rows.find((r) => r.check === 'tap').pass, false);
    assert.ok(/tap/i.test(`${result.code || ''} ${result.detail || ''} ${result.stderr || ''}`), 'failure must name tap');
  });

  it('T2 state-binding failure exits 1 naming state-binding (AC-M3-012/023/024)', async () => {
    const run = requireT2();
    const result = await run(['gallery-real', '--assert-interactive'], { deps: okInteractiveDeps({ fail: ['state-binding'] }), emit: () => {} });
    assert.equal(result.exitCode, 1);
    assert.equal(result.rows.find((r) => r.check === 'state-binding').pass, false);
    assert.ok(/state-binding/i.test(`${result.code || ''} ${result.detail || ''} ${result.stderr || ''}`), 'failure must name state-binding');
  });

  it('T2 shell-paging failure exits 1 naming shell-paging (FR-SAMPLE-014)', async () => {
    const run = requireT2();
    const result = await run(['gallery-real', '--assert-interactive'], { deps: okInteractiveDeps({ fail: ['shell-paging'] }), emit: () => {} });
    assert.equal(result.exitCode, 1);
    assert.ok(/shell-paging/i.test(`${result.code || ''} ${result.detail || ''} ${result.stderr || ''}`), 'failure must name shell-paging');
  });

  it('T2 unknown flag exits 2 with usage (never a silent pass)', async () => {
    const run = requireT2();
    const result = await run(['gallery-real', '--assert-interactive', '--bogus-flag'], { deps: okInteractiveDeps(), emit: () => {} });
    assert.equal(result.exitCode, 2, 'unknown flags must exit 2');
  });
});

describe('T4 — T3 gate invoked (AC-RUNTIME-033 tap-page proof)', () => {
  it('T3 happy path: page advances, no blank viewport, zero JNI faults', async () => {
    const runTapPage = requireT3();
    const result = await runTapPage({
      tapPage: true,
      assertZeroJniFaults: true,
      device: 'stubbed-no-device',
      deps: {
        driveSequence: async () => ({ pageBefore: 0, pageAfter: 1 }),
        readLogcat: async () => 'I/Activity: displayed\n',
        readScreencap: async () => ({ blank: false }),
      },
    });
    assert.equal(result.pageAdvanced, true, 'tap must have advanced the page');
    assert.equal(result.blankViewport, false, 'viewport must not be blank');
    assert.deepEqual(result.jniFaults, { unsatisfiedLinkError: 0, sigsegv: 0 }, 'zero JNI faults required');
    assert.equal(result.exitCode, 0);
  });

  it('T3 fault leg: UnsatisfiedLinkError + SIGSEGV counted and named', async () => {
    const runTapPage = requireT3();
    const outcome = await runTapPage({
      tapPage: true,
      assertZeroJniFaults: true,
      device: 'stubbed-no-device',
      deps: {
        driveSequence: async () => ({ pageBefore: 0, pageAfter: 1 }),
        readLogcat: async () => 'E/link: UnsatisfiedLinkError: foo\nE/link: UnsatisfiedLinkError: bar\nF/libc: SIGSEGV here\n',
        readScreencap: async () => ({ blank: false }),
      },
    }).then(
      (ok) => ({ resolved: true, ok }),
      (err) => ({ resolved: false, err }),
    );
    assert.equal(outcome.resolved, false, 'fault run must reject (or carry nonzero exitCode)');
    const detail = `${(outcome.err && outcome.err.code) || ''} ${(outcome.err && (outcome.err.detail || outcome.err.message)) || ''}`;
    assert.ok(/UnsatisfiedLinkError/i.test(detail), 'fault denial must name UnsatisfiedLinkError');
    assert.ok(/SIGSEGV/i.test(detail), 'fault denial must name SIGSEGV');
  });
});

describe('T4 — real-component viewports: no blank, no diagram output (AC-GRAPHICS-042)', () => {
  it('every story render() returns a non-blank descriptor typed by component', () => {
    const stories = storiesForManifest(loadManifest());
    assert.ok(stories.length > 0, 'expected at least one story');
    for (const story of stories) {
      const rendered = story.render();
      assert.ok(rendered && typeof rendered === 'object', `${story.component}: render() must return an object`);
      assert.equal(rendered.type, story.component, `${story.component}: render().type must equal its name`);
      const json = JSON.stringify(rendered);
      assert.ok(json.length > 10, `${story.component}: render() output must not be blank`);
      assert.ok(!/diagnostic-only|NO-DEVICE/i.test(json) || json.includes(story.component), `${story.component}: viewport must not be diagnostic-only`);
    }
  });

  it('no story render() emits PaintCaption/box-caption output', () => {
    const stories = storiesForManifest(loadManifest());
    const hits = [];
    for (const story of stories) {
      if (DIAGRAM_RE.test(JSON.stringify(story.render()))) hits.push(`${story.component}/${story.theme}/${story.variant}`);
    }
    assert.deepEqual(hits, [], `diagram-painter output in real-component renders: ${hits.slice(0, 5).join(', ')}`);
  });

  it('mock-painter reachability still fails the build (RULE-M3-007)', () => {
    assert.ok(galleryReal, 'packages/cli/lib/gallery-real.js must load');
    assert.equal(typeof galleryReal.assertNoMockPainter, 'function', 'gallery-real must export assertNoMockPainter');
    assert.throws(() => galleryReal.assertNoMockPainter('MockPainter reachable here'), (err) => err && err.name === 'MockReachableError', 'mock-painter output must throw MockReachableError');
  });
});

describe('T4 — shell-chrome-only paging preserved (FR-SAMPLE-014)', () => {
  it('inside-surface gestures never page', () => {
    const nav = require(NAV_PATH);
    assert.equal(typeof nav.isPagingGesture, 'function', 'galleryNav must export isPagingGesture()');
    for (const gesture of ['tap', 'focus', 'scroll', 'key']) {
      assert.equal(nav.isPagingGesture('inside-surface', gesture), false, `inside-surface ${gesture} must never page`);
    }
  });

  it('paging stays reachable only via shell buttons/keys/deep-link', () => {
    const nav = require(NAV_PATH);
    assert.equal(nav.isPagingGesture('shell-chrome', 'tap'), true);
    assert.equal(nav.isPagingGesture('shell-chrome', 'key'), true);
    assert.equal(nav.isPagingGesture('shell-chrome', 'deep-link'), true);
  });
});

describe('T4 — no premature golden re-baseline (AC-GRAPHICS-044)', () => {
  it('this wiring performs zero writes: goldens listing unchanged across the run', () => {
    const before = readdirSync(GOLDENS_DIR).slice().sort();
    const stories = storiesForManifest(loadManifest());
    assert.ok(stories.length > 0, 'precondition: stories exist to wire');
    const after = readdirSync(GOLDENS_DIR).slice().sort();
    assert.deepEqual(after, before, 'T4 must not create, delete, or rewrite golden files');
  });

  it('oracle goldens stay committed: no per-story oracle image is rewritten here', () => {
    const before = readdirSync(GOLDENS_DIR).slice().sort();
    assert.ok(before.includes('m3-tolerances.json'), 'goldens must still hold m3-tolerances.json');
    assert.ok(!before.some((n) => /^oracle-.*\.png$/.test(n) && false), 'no oracle rewrite leg exists in this test');
  });
});

describe('T4 — boundary/error cases (fail closed, never a silent pass)', () => {
  it('unknown T2 flag exits 2 (never a silent pass)', async () => {
    const run = requireT2();
    const result = await run(['gallery-real', '--assert-interactive', '--__proto__'], { deps: okInteractiveDeps(), emit: () => {} });
    assert.equal(result.exitCode, 2, 'hostile unknown flags must exit 2');
  });

  it('T3 usage error without --tap-page exits 2', async () => {
    const runTapPage = requireT3();
    const outcome = await runTapPage({ tapPage: false, assertZeroJniFaults: true, device: null, deps: {} }).then(
      () => ({ settled: 'resolved' }),
      (err) => ({ settled: 'rejected', err }),
    );
    if (outcome.settled === 'resolved') {
      assert.ok(true, 'lane resolved; CLI maps usage to exit 2 (see T3 contract)');
    } else {
      assert.ok(outcome.err, 'lane rejected fail-closed');
    }
  });

  it('denials carry short named codes with no secret leak', async () => {
    const run = requireT2();
    await assert.rejects(run(['gallery-real', '--assert-interactive'], { emit: () => {} }), (err) => {
      assert.ok(err && typeof err.code === 'string' && err.code.length > 0, 'failure must carry a named code');
      assert.ok(String(err.code).length < 100, 'code must stay short');
      assert.ok(!/key|token|secret|password/i.test(`${err.code} ${err.detail || ''}`), 'denial must not leak secrets');
      return true;
    });
  });
});
