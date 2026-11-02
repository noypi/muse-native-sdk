'use strict';
// T4b — Graphic-shape Button Prev/Next chrome (test-first, RED pre-implementation).
// Plan item T4b FINAL 2026-11-02 per user answers A1/A2/A3 (plan.md lines 88-96):
// Buttons go in the App.tsx shell section (~lines 312-320) as COMPILE-TIME tree
// source (compiled into tree.mui.bin; App.tsx never executes on-device — the
// tree does). Chrome: `<Button variant="tonal" label="" enabled={!prevDisabled} />`
// / Next (`label=""` — schema-legal empty string per specs-creator Q-label
// resolution; icon OMITTED entirely — the arrow-back/arrow-forward decision is
// SUPERSEDED, no icon assets, no text captions; graphic container shape with no
// text and no icon assets). Tap stays on the outer `View onTouchStart` wrapper
// → unchanged `onPrev`/`onNext(SHELL_ORIGIN)` → `isPagingGesture` →
// `requestShellAdvance` → `setSelectedIndex`; Next-only, Prev forced disabled.
// (Specs-creator Q-tap: no Button press prop exists in the manifest — the
// wrapper routing is the specified contract; tests pin the WRAPPER path, never
// a Button press prop.)
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. No
// network, no secrets, no config, no device/adb, no downloads, no shared
// mutable state. Static source pins read App.tsx from disk (no execution, no
// bundle); behavioral pins drive galleryNav.js in-process with an in-memory
// story list and the hermetic __museTestBroadcasts sink.
//
// Binds ONLY to the plan T4b Interface line (plan.md line 90, reproduced
// verbatim in unit-tests/T4b.md) and the FR-SAMPLE-014 Notes v2.45 behavior
// (specs-creator resolutions Q-SAMPLE-004/Q-SAMPLE-005, 2026-11-02).
//
// ASSUMPTIONS (marked per skill; contract genuinely silent → most defensible):
//   A-T4b1 (Q-label): manifest `Button.label` is REQUIRED but typed plain
//       `string` with no values/minLength constraint, so `label=""` satisfies
//       build presence; text-freeness is presentational (container `Surface`
//       shape visible/tappable when glyphs fail), not descriptor-level. Pinned
//       by asserting `label=""` on both Button tags.
//   A-T4b2 (Q-tap): the `Button` manifest schema declares only
//       variant/label/enabled/icon — NO press/event prop exists — so the tap
//       handler stays on the outer `View onTouchStart` wrapper; moving it onto
//       the Button would invent a prop. Pinned by asserting no
//       onTouchStart/onPress/onClick appears on any Button tag.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const SRC_DIR = join(__dirname, '..', 'src');
const APP_PATH = join(SRC_DIR, 'App.tsx');
const NAV_PATH = '../src/galleryNav.js';
const EXPECTED_ACTION = 'muse.native.NATIVE_ADVANCE_PAGE';

function readApp() {
  return readFileSync(APP_PATH, 'utf8');
}

function freshRequireNav() {
  const resolved = require.resolve(NAV_PATH);
  delete require.cache[resolved];
  return require(NAV_PATH);
}

function buttonTags(appSrc) {
  const tags = appSrc.match(/<Button[\s\S]*?\/>/g) || [];
  return tags;
}

describe('T4b graphic-shape Button Prev/Next chrome (static source pins — RED pre-implementation)', () => {
  it('App.tsx imports Button from @muse/native', () => {
    const appSrc = readApp();
    assert.ok(
      /from\s+['"]@muse\/native['"]/.test(appSrc) && /\bButton\b/.test(appSrc),
      'App.tsx must import Button from @muse/native',
    );
    assert.ok(
      /import\s*\{[^}]*\bButton\b[^}]*\}\s*from\s*['"]@muse\/native['"]/.test(appSrc),
      'Button must be a named import from @muse/native',
    );
  });

  it('Prev renders Button variant=tonal label="" enabled={!prevDisabled} with icon omitted', () => {
    const appSrc = readApp();
    const tags = buttonTags(appSrc);
    const prev = tags.filter((t) => /enabled=\{!prevDisabled\}/.test(t));
    assert.equal(prev.length, 1, 'exactly one Button with enabled={!prevDisabled} must exist');
    assert.ok(/variant="tonal"/.test(prev[0]), 'Prev Button must use variant="tonal"');
    assert.ok(/label=""/.test(prev[0]), 'Prev Button must use label="" (schema-legal empty string, no caption)');
    assert.ok(!/\bicon=/.test(prev[0]), 'Prev Button must omit icon entirely (no icon assets)');
    assert.ok(!/label="(Prev|Next)"/.test(prev[0]), 'Prev Button must not carry a text caption label');
  });

  it('Next renders Button variant=tonal label="" enabled={!nextDisabled} with icon omitted', () => {
    const appSrc = readApp();
    const tags = buttonTags(appSrc);
    const next = tags.filter((t) => /enabled=\{!nextDisabled\}/.test(t));
    assert.equal(next.length, 1, 'exactly one Button with enabled={!nextDisabled} must exist');
    assert.ok(/variant="tonal"/.test(next[0]), 'Next Button must use variant="tonal"');
    assert.ok(/label=""/.test(next[0]), 'Next Button must use label="" (schema-legal empty string, no caption)');
    assert.ok(!/\bicon=/.test(next[0]), 'Next Button must omit icon entirely (no icon assets)');
    assert.ok(!/label="(Prev|Next)"/.test(next[0]), 'Next Button must not carry a text caption label');
  });

  it('tap stays on the outer View onTouchStart wrapper (no tap prop on Button)', () => {
    const appSrc = readApp();
    assert.ok(
      /onTouchStart=\{prevDisabled\s*\?\s*undefined\s*:\s*\(\)\s*=>\s*onPrev\(SHELL_ORIGIN\)\}/.test(appSrc),
      'Prev tap must stay on the outer View: onTouchStart={prevDisabled ? undefined : () => onPrev(SHELL_ORIGIN)}',
    );
    assert.ok(
      /onTouchStart=\{nextDisabled\s*\?\s*undefined\s*:\s*\(\)\s*=>\s*onNext\(SHELL_ORIGIN\)\}/.test(appSrc),
      'Next tap must stay on the outer View: onTouchStart={nextDisabled ? undefined : () => onNext(SHELL_ORIGIN)}',
    );
    for (const tag of buttonTags(appSrc)) {
      assert.ok(!/onTouchStart|onPress|onClick/.test(tag), `Button tag must not carry a tap prop (would invent a prop): ${tag}`);
    }
  });

  it('counter text unchanged: `${clampedIndex + 1} of ${stories.length}`', () => {
    const appSrc = readApp();
    assert.ok(
      // eslint-disable-next-line no-template-curly-in-string
      appSrc.includes('`${clampedIndex + 1} of ${stories.length}`'),
      'counter text `${clampedIndex + 1} of ${stories.length}` must be unchanged',
    );
  });

  it('onPrev/onNext keep the SHELL_ORIGIN → isPagingGesture → requestShellAdvance → setSelectedIndex path', () => {
    const appSrc = readApp();
    const prevStart = appSrc.indexOf('const onPrev');
    const nextStart = appSrc.indexOf('const onNext');
    assert.ok(prevStart !== -1 && nextStart !== -1, 'App.tsx must define onPrev and onNext');
    const prevBlock = appSrc.slice(prevStart, nextStart);
    const nextEnd = appSrc.indexOf('ArrowLeft/ArrowRight', nextStart);
    const nextBlock = appSrc.slice(nextStart, nextEnd !== -1 ? nextEnd : nextStart + 800);
    for (const [name, block, dir] of [['onPrev', prevBlock, -1], ['onNext', nextBlock, 1]]) {
      assert.ok(/isPagingGesture\(origin,\s*'tap'\)/.test(block), `${name} must keep the isPagingGesture(origin, 'tap') guard`);
      assert.ok(
        block.includes(`requestShellAdvance(${dir}, 'shell-chrome')`),
        `${name} must keep requestShellAdvance(${dir}, 'shell-chrome')`,
      );
      assert.ok(/setSelectedIndex/.test(block), `${name} must keep setSelectedIndex`);
    }
  });

  it('disabled maps to undefined handler (fail-closed chrome)', () => {
    const appSrc = readApp();
    assert.ok(/prevDisabled\s*=\s*true/.test(appSrc), 'prevDisabled must stay forced true (forward-only)');
    assert.ok(
      /nextDisabled\s*=\s*stories\.length\s*===\s*0\s*\|\|\s*clampedIndex\s*>=\s*stories\.length\s*-\s*1/.test(appSrc),
      'nextDisabled must stay stories.length === 0 || clampedIndex >= stories.length - 1',
    );
  });

  it('Button props stay within the manifest schema (no new props, icon omitted)', () => {
    const appSrc = readApp();
    const manifest = JSON.parse(readFileSync(join(__dirname, '..', '..', '..', 'packages', 'native', 'muse.manifest.json'), 'utf8'));
    const button = manifest.components.find((c) => c.name === 'Button');
    assert.ok(button, 'manifest must declare a Button component entry');
    const allowed = new Set(Object.keys(button.props));
    for (const tag of buttonTags(appSrc)) {
      const names = Array.from(tag.matchAll(/(\w+)=/g)).map((m) => m[1]);
      for (const n of names) {
        assert.ok(allowed.has(n), `Button prop "${n}" must exist in the manifest schema (no new props)`);
      }
      assert.ok(!/\bicon=/.test(tag), `Button tag must omit icon (no icon-asset dependency): ${tag}`);
    }
    assert.ok(allowed.has('variant') && allowed.has('label') && allowed.has('enabled') && allowed.has('icon'));
    assert.ok(button.props.variant.values.includes('tonal'), 'manifest Button.variant must include tonal');
    assert.equal(button.props.label.required, true, 'manifest Button.label must be required (label="" satisfies presence)');
    assert.equal(button.props.label.type, 'string', 'manifest Button.label must be a plain string (no minLength/values gate)');
  });
});

describe('T4b same paging path via galleryNav (behavioral pins — hold pre/post chrome)', () => {
  it('Prev/Next advance the index via navigateStory (same direction records)', () => {
    const nav = freshRequireNav();
    const n = nav.createNavigator([{ id: 'a' }, { id: 'b' }, { id: 'c' }]);
    assert.deepEqual(n.navigateStory('next'), { index: 1, storyId: 'b' });
    assert.deepEqual(n.navigateStory('prev'), { index: 0, storyId: 'a' });
  });

  it('requestShellAdvance emits same direction/origin records (shell-chrome tap leg)', () => {
    const nav = freshRequireNav();
    globalThis.__museTestBroadcasts = [];
    try {
      assert.equal(nav.requestShellAdvance(-1, 'shell-chrome'), true);
      assert.equal(nav.requestShellAdvance(1, 'shell-chrome'), true);
      assert.deepEqual(globalThis.__museTestBroadcasts, [
        { action: EXPECTED_ACTION, direction: -1, origin: 'shell-chrome' },
        { action: EXPECTED_ACTION, direction: 1, origin: 'shell-chrome' },
      ]);
    } finally {
      delete globalThis.__museTestBroadcasts;
    }
  });

  it('disabled states hold at first/last story (boundary denial, never blank)', () => {
    const nav = freshRequireNav();
    const first = nav.createNavigator([{ id: 'a' }, { id: 'b' }]);
    assert.equal(first.prevDisabled(), true);
    assert.equal(first.navigateStory('prev').code, 'boundary');
    assert.deepEqual(first.current(), { index: 0, storyId: 'a' });
    const last = nav.createNavigator([{ id: 'a' }, { id: 'b' }]);
    last.navigateStory('next');
    assert.equal(last.nextDisabled(), true);
    assert.equal(last.navigateStory('next').code, 'boundary');
    assert.deepEqual(last.current(), { index: 1, storyId: 'b' });
  });

  it('counter label reads "N of M" for the selected story', () => {
    const nav = freshRequireNav();
    const n = nav.createNavigator([{ id: 'a' }, { id: 'b' }, { id: 'c' }]);
    assert.equal(n.indexLabel(), '1 of 3');
    n.navigateStory('next');
    assert.equal(n.indexLabel(), '2 of 3');
  });

  it('non-shell origins still denied (denial-by-default preserved)', () => {
    const nav = freshRequireNav();
    globalThis.__museTestBroadcasts = [];
    try {
      for (const origin of ['inside-surface', 'story-surface', 'overlay', undefined, null, '']) {
        assert.equal(nav.isPagingGesture(origin, 'tap'), false, `origin ${String(origin)} must not page`);
        assert.equal(nav.requestShellAdvance(1, origin), false, `origin ${String(origin)} must not emit`);
      }
      assert.equal(globalThis.__museTestBroadcasts.length, 0, 'denied origins must emit nothing');
    } finally {
      delete globalThis.__museTestBroadcasts;
    }
  });
});
