'use strict';
// FR-M3-011 Batch 2 — T3 contract.
//
// Proves in-place interactivity through the contract: tap, focus,
// caret/selection, scroll, switch/scrub, dialog/sheet/menu dismissal, and
// state binding per AC-M3-012/023/024 on real-component pages, with
// shell-chrome-only paging preserved (FR-SAMPLE-014).
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only.
// Synthetic pageId strings only; no network, no secrets, no application
// configuration, no device, no live service, no shared mutable state.
// `process.env` is never read.
//
// Binds ONLY to the plan interface (reproduced verbatim in
// unit-tests/T3.md):
//   `runInteractiveAssertions(pageId: string): InteractiveReport — pageId names a gallery page; returns InteractiveReport{tap: boolean, focus: boolean, caret: boolean, scroll: boolean, dismiss: boolean}`
//
// ASSUMPTIONS (the plan Interface is genuinely silent on module spelling —
// the coder reconciles layout per GR-08 T1.9; behavior pinned, spelling
// reconcilable):
//   A1 (module path): tests probe `packages/cli/lib/gallery-real.js` first
//       (it owns the `gallery-real --assert-interactive` gate per the plan
//       Acceptance/TC-M3-027, next to `assertNoMockPainter`), then
//       `packages/cli/src/gallery-real.js`, then
//       `examples/m3-gallery/src/galleryInteractive.js`. Any location
//       exporting `runInteractiveAssertions` satisfies the contract.
//   A2 (pageId fixtures): valid pageIds are gallery page names. Tests use
//       representative ids (`controls`, `text`, `slider`, `dialog`,
//       `disabled-controls`, `scroll-list`); the coder maps them to the
//       real gallery page ids. Expectations describe per-kind behavior,
//       not the fixture spelling.
//   A3 (unknown-page denial): `pageId` naming no gallery page throws an
//       Error with `code === 'unknown-page'` (fail closed, never an
//       all-true/all-false silent report). Mirrors the throwing style of
//       `assertNoMockPainter` in the same module.
//   A4 (empty/hostile denial): empty, blank, null, undefined, non-string,
//       and oversized pageIds throw (fail closed) instead of returning a
//       report; a 1MB input rejects in well under 5s.
//   A5 (boolean-report scope): clamp magnitudes, pixel positions, and
//       binding write-through values are device-lane proof (EXTERNAL,
//       `tester` via `gallery-real --assert-interactive` on device). The
//       unit report pins which interaction kinds are live per page kind.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync, existsSync } = require('node:fs');
const { join, resolve } = require('node:path');

const REPO_ROOT = resolve(__dirname, '..', '..', '..');
const RUN_INTERACTIVE_CANDIDATES = [
  join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-real.js'),
  join(REPO_ROOT, 'packages', 'cli', 'src', 'gallery-real.js'),
  join(__dirname, '..', 'src', 'galleryInteractive.js'),
];
const SHOWCASE_PATH = join(__dirname, '..', 'src', 'Showcase.tsx');
const NAV_PATH = '../src/galleryNav.js';

const REPORT_FIELDS = ['tap', 'focus', 'caret', 'scroll', 'dismiss'];

function loadRunInteractive() {
  for (const candidate of RUN_INTERACTIVE_CANDIDATES) {
    if (!existsSync(candidate)) continue;
    try {
      const mod = require(candidate);
      return { mod, candidate, error: null };
    } catch (err) {
      return { mod: null, candidate, error: err };
    }
  }
  return { mod: null, candidate: RUN_INTERACTIVE_CANDIDATES[0], error: new Error('runInteractiveAssertions module missing') };
}

function requireRunInteractive(loaded) {
  assert.ok(
    loaded.mod,
    `runInteractiveAssertions module missing (tried ${RUN_INTERACTIVE_CANDIDATES.join(', ')}): ${loaded.error && loaded.error.message}`,
  );
  assert.equal(
    typeof loaded.mod.runInteractiveAssertions,
    'function',
    'module must export runInteractiveAssertions(pageId) (T3 Interface)',
  );
  return loaded.mod.runInteractiveAssertions;
}

function requireReportShape(report, pageId) {
  assert.equal(typeof report, 'object', `report for ${pageId} must be an object`);
  assert.ok(report !== null, `report for ${pageId} must not be null`);
  for (const field of REPORT_FIELDS) {
    assert.equal(
      typeof report[field],
      'boolean',
      `InteractiveReport.${field} for ${pageId} must be boolean`,
    );
  }
}

describe('T3 — runInteractiveAssertions contract shape', () => {
  it('a candidate module exists for the interactive gate', () => {
    const loaded = loadRunInteractive();
    assert.ok(
      loaded.mod,
      `no candidate module loads (tried ${RUN_INTERACTIVE_CANDIDATES.join(', ')}): ${loaded.error && loaded.error.message}`,
    );
  });

  it('exports runInteractiveAssertions(pageId)', () => {
    requireRunInteractive(loadRunInteractive());
  });

  it('a controls page returns InteractiveReport with five booleans', () => {
    const run = requireRunInteractive(loadRunInteractive());
    const report = run('controls');
    requireReportShape(report, 'controls');
  });

  it('is deterministic: two calls for the same page agree', () => {
    const run = requireRunInteractive(loadRunInteractive());
    const first = run('controls');
    const second = run('controls');
    assert.deepEqual(second, first, 'same pageId must yield the same report');
    assert.notEqual(second, first, 'each call must return a fresh object (no shared mutable report)');
  });

  it('unknown pageId throws fail-closed with code unknown-page', () => {
    const run = requireRunInteractive(loadRunInteractive());
    assert.throws(
      () => run('no-such-gallery-page'),
      (err) => {
        assert.ok(err instanceof Error, 'denial must be an Error');
        assert.equal(err.code, 'unknown-page');
        assert.ok(String(err.message).includes('no-such-gallery-page'), 'denial must name the pageId');
        return true;
      },
      'unknown pageId must throw { code: unknown-page }',
    );
  });

  it('empty/blank pageId throws fail-closed, never an all-true report', () => {
    const run = requireRunInteractive(loadRunInteractive());
    for (const bad of ['', '   ']) {
      assert.throws(() => run(bad), Error, `pageId ${JSON.stringify(bad)} must throw`);
    }
  });

  it('null/undefined/non-string pageId throws fail-closed', () => {
    const run = requireRunInteractive(loadRunInteractive());
    for (const bad of [null, undefined, 42, {}, []]) {
      assert.throws(() => run(bad), Error, `pageId ${String(bad)} must throw`);
    }
  });

  it('a 1MB hostile pageId rejects fast without hanging', () => {
    const run = requireRunInteractive(loadRunInteractive());
    const huge = 'x'.repeat(1024 * 1024);
    const start = Date.now();
    assert.throws(() => run(huge), Error, '1MB pageId must reject');
    assert.ok(Date.now() - start < 5000, '1MB pageId rejection must not hang');
  });
});

describe('T3 — in-place interactivity per AC (AC-M3-023/024/012, AC-M3-022 dismissal)', () => {
  it('controls page: tap is live (AC-M3-023 enabled Checkbox/Radio/Switch flip/select)', () => {
    const run = requireRunInteractive(loadRunInteractive());
    const report = run('controls');
    requireReportShape(report, 'controls');
    assert.equal(report.tap, true, 'enabled selection controls must report tap live');
  });

  it('disabled page: every interaction kind is inert (AC-M3-023 disabled changes nothing)', () => {
    const run = requireRunInteractive(loadRunInteractive());
    const report = run('disabled-controls');
    requireReportShape(report, 'disabled-controls');
    for (const field of REPORT_FIELDS) {
      assert.equal(report[field], false, `disabled controls must report ${field} inert`);
    }
  });

  it('text page: focus and caret/selection are live (AC-M3-012)', () => {
    const run = requireRunInteractive(loadRunInteractive());
    const report = run('text');
    requireReportShape(report, 'text');
    assert.equal(report.focus, true, 'text page must report focus live');
    assert.equal(report.caret, true, 'text page must report caret/selection live');
  });

  it('scroll page: scroll is live in place (no paging side effect)', () => {
    const run = requireRunInteractive(loadRunInteractive());
    const report = run('scroll-list');
    requireReportShape(report, 'scroll-list');
    assert.equal(report.scroll, true, 'scrollable page must report scroll live');
  });

  it('slider page: scrub never fires the tap handler (AC-M3-024 drag-no-fire, AC-INPUT-020)', () => {
    const run = requireRunInteractive(loadRunInteractive());
    const report = run('slider');
    requireReportShape(report, 'slider');
    assert.equal(report.tap, false, 'a press-and-drag scrub must never report tap');
  });

  it('dialog page: dismissal is live (AC-M3-022 scrim tap dismisses via OverlayHost)', () => {
    const run = requireRunInteractive(loadRunInteractive());
    const report = run('dialog');
    requireReportShape(report, 'dialog');
    assert.equal(report.dismiss, true, 'dialog/sheet/menu page must report dismiss live');
  });

  it('thrown denials are Errors with a short string code (no internal leak)', () => {
    const run = requireRunInteractive(loadRunInteractive());
    let err = null;
    try {
      run('no-such-gallery-page');
    } catch (e) {
      err = e;
    }
    assert.ok(err instanceof Error, 'denial must be an Error');
    assert.equal(typeof err.code, 'string', 'denial must carry a string code');
    assert.ok(err.message.length < 500, 'denial message must stay short');
  });
});

describe('T3 — shell-chrome-only paging preserved during interaction (FR-SAMPLE-014)', () => {
  it('inside-surface tap/focus/scroll/key never page', () => {
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

  it('Showcase story surface carries no tap-to-advance path', () => {
    const src = readFileSync(SHOWCASE_PATH, 'utf8');
    assert.ok(
      !/navigateStory|openStoryLink|requestShellAdvance|tapToAdvance|advancePage/i.test(src),
      'Showcase.tsx must not page from the story surface (shell chrome only)',
    );
  });

  it('gallery-real CLI parses --assert-interactive (TC-M3-027 gate)', () => {
    const loaded = loadRunInteractive();
    assert.ok(loaded.mod, 'gallery-real module missing');
    assert.equal(
      typeof loaded.mod.parseGalleryRealArgs,
      'function',
      'gallery-real must export parseGalleryRealArgs()',
    );
    const parsed = loaded.mod.parseGalleryRealArgs(['gallery-real', '--assert-interactive'], {});
    assert.equal(parsed.assertInteractive, true, '--assert-interactive must set assertInteractive');
  });
});
