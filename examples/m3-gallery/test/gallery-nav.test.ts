'use strict';
// FR-SAMPLE-014 Batch 1 (T1+T2 contract).
//
// Hermetic: node:test + node:assert/strict only. Synthetic in-memory story
// lists plus tiny synthetic manifests through buildStories(manifest); no
// network, no secrets, no config, no device, no shared mutable state.
//
// Binds ONLY to the plan interfaces:
//   T1: `navigateStory(direction: 'prev' | 'next'): { index, storyId }` +
//       `openStoryLink(link: string): { index, storyId }`
//       (link shape `gallery://story/<id>[?seed=<n>]`, unknown id →
//       `{ code: 'unknown-story', storyId }` rejection, never blank screen);
//       shell exposes `PrevButton(disabled: boolean): void`,
//       `NextButton(disabled: boolean): void`, `StoryIndex(label: string): void`;
//       `ArrowLeft`/`ArrowRight` dispatch the same `navigateStory` path.
//       Error/denial shape: `{ code: 'unknown-story' | 'boundary', storyId? }`.
//   T2: `isPagingGesture(origin, gesture): boolean` — true only for
//       shell-chrome button/key/deep-link invocations; every inside-surface
//       gesture returns false. No new error shape (pure predicate).
//
// ASSUMPTIONS:
//   A1 (module path): the gallery navigation entry lives at
//       `src/galleryNav.js` (CommonJS) exporting `navigateStory`,
//       `openStoryLink`, `isPagingGesture`, `PrevButton`, `NextButton`,
//       `StoryIndex`, `handleKey`, and `createNavigator`. The coder reconciles
//       the exact file layout; the behavior below is what must hold.
//   A2 (navigator factory): tests drive navigation through
//       `createNavigator(stories)` returning an isolated navigator so each
//       test arranges its own state. If the coder exposes module-level
//       functions over buildStories(manifest) instead, the call sites adapt
//       but expectations do not change.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const NAV_PATH = '../src/galleryNav.js';
const STORIES_PATH = '../src/stories.js';

let navError = null;
let nav = null;
try {
  nav = require(NAV_PATH);
} catch (err) {
  navError = err;
}

let storiesError = null;
let buildStories = null;
try {
  ({ buildStories } = require(STORIES_PATH));
} catch (err) {
  storiesError = err;
}

function requireNav() {
  assert.equal(
    navError,
    null,
    `gallery navigation module missing at ${NAV_PATH}: ${navError && navError.message}`,
  );
  for (const fn of ['navigateStory', 'openStoryLink', 'isPagingGesture']) {
    assert.equal(typeof nav[fn], 'function', `galleryNav must export ${fn}()`);
  }
}

function demoStories() {
  return [
    { id: 'buttons', state: { count: 0 } },
    { id: 'textinput', state: { value: 'hello', focused: false } },
    { id: 'cards', state: { selected: 1 } },
  ];
}

function navigatorFor(stories) {
  requireNav();
  assert.equal(typeof nav.createNavigator, 'function', 'galleryNav must export createNavigator(stories)');
  return nav.createNavigator(stories);
}

describe('T1 — shell Prev/Next navigation over the manifest story list', () => {
  it('exports navigateStory/openStoryLink plus shell Prev/Next/index entry points', () => {
    requireNav();
    for (const fn of ['PrevButton', 'NextButton', 'StoryIndex', 'handleKey']) {
      assert.equal(typeof nav[fn], 'function', `galleryNav must export ${fn}()`);
    }
  });

  it('buildStories(manifest) drives the story list (manifest-generated surface)', () => {
    assert.equal(storiesError, null, `stories module failed to load: ${storiesError && storiesError.message}`);
    assert.equal(typeof buildStories, 'function', 'stories module must export buildStories(manifest)');
    const manifest = {
      components: [
        { name: 'Alpha', tier: 1, props: { variant: { type: 'string', values: ['a'] } } },
        { name: 'Beta', tier: 1, props: { variant: { type: 'string', values: ['b'] } } },
      ],
    };
    const stories = buildStories(manifest);
    assert.ok(stories.length >= 2, 'expected manifest-driven stories');
  });

  it('next advances the index and reports { index, storyId }', () => {
    const n = navigatorFor(demoStories());
    const out = n.navigateStory('next');
    assert.deepEqual(out, { index: 1, storyId: 'textinput' });
  });

  it('prev retreats the index and reports { index, storyId }', () => {
    const n = navigatorFor(demoStories());
    n.navigateStory('next');
    const out = n.navigateStory('prev');
    assert.deepEqual(out, { index: 0, storyId: 'buttons' });
  });

  it('next at the last story is a boundary denial (never blank screen)', () => {
    const n = navigatorFor(demoStories());
    n.navigateStory('next');
    n.navigateStory('next');
    const out = n.navigateStory('next');
    assert.equal(out && out.code, 'boundary');
    assert.equal(n.current().index, 2);
    assert.equal(n.current().storyId, 'cards');
  });

  it('prev at the first story is a boundary denial (never blank screen)', () => {
    const n = navigatorFor(demoStories());
    const out = n.navigateStory('prev');
    assert.equal(out && out.code, 'boundary');
    assert.equal(n.current().index, 0);
  });

  it('PrevButton is disabled on the first story, NextButton on the last', () => {
    const n = navigatorFor(demoStories());
    assert.equal(n.prevDisabled(), true);
    assert.equal(n.nextDisabled(), false);
    n.navigateStory('next');
    n.navigateStory('next');
    assert.equal(n.prevDisabled(), false);
    assert.equal(n.nextDisabled(), true);
  });

  it('StoryIndex label reads "N of M" for the selected story', () => {
    const n = navigatorFor(demoStories());
    assert.equal(n.indexLabel(), '1 of 3');
    n.navigateStory('next');
    assert.equal(n.indexLabel(), '2 of 3');
  });

  it('ArrowRight/ArrowLeft dispatch the same navigateStory path', () => {
    const n = navigatorFor(demoStories());
    requireNav();
    assert.equal(typeof nav.handleKey, 'function', 'galleryNav must export handleKey()');
    assert.deepEqual(n.handleKey('ArrowRight'), { index: 1, storyId: 'textinput' });
    assert.deepEqual(n.handleKey('ArrowLeft'), { index: 0, storyId: 'buttons' });
  });

  it('openStoryLink(gallery://story/<id>) jumps to that story', () => {
    const n = navigatorFor(demoStories());
    const out = n.openStoryLink('gallery://story/cards');
    assert.deepEqual(out, { index: 2, storyId: 'cards' });
  });

  it('openStoryLink honors the ?seed=<n> shape without changing the target', () => {
    const n = navigatorFor(demoStories());
    const out = n.openStoryLink('gallery://story/textinput?seed=42');
    assert.deepEqual(out, { index: 1, storyId: 'textinput' });
  });

  it('openStoryLink with an unknown id rejects with { code: unknown-story } and keeps the current story', () => {
    const n = navigatorFor(demoStories());
    const out = n.openStoryLink('gallery://story/nope');
    assert.equal(out && out.code, 'unknown-story');
    assert.equal(out && out.storyId, 'nope');
    assert.equal(n.current().index, 0);
  });

  it('openStoryLink with a 1MB rawId rejects fail-closed without throwing or hanging', () => {
    const n = navigatorFor(demoStories());
    requireNav();
    assert.equal(typeof nav.parseStoryLink, 'function', 'galleryNav must export parseStoryLink()');
    const huge = 'a'.repeat(1024 * 1024);
    const link = `gallery://story/${huge}`;
    const start = Date.now();
    let out;
    assert.doesNotThrow(() => {
      out = n.openStoryLink(link);
    }, '1MB link must not throw');
    assert.ok(
      out && (out.code === 'unknown-story' || out.code === 'boundary'),
      'oversized link must reject with unknown-story/boundary',
    );
    assert.equal(n.current().index, 0, 'oversized-link rejection must keep the current story');
    assert.ok(Date.now() - start < 5000, 'oversized-link rejection must not hang');
    let parsed;
    assert.doesNotThrow(() => {
      parsed = nav.parseStoryLink(link);
    }, 'parseStoryLink must not throw on a 1MB link');
    assert.ok(parsed && (parsed.code === 'unknown-story' || typeof parsed.id === 'string'),
      'parseStoryLink must return { id } or { code: unknown-story } without throwing');
  });

  it('App onOpenStoryLink agrees with galleryNav.openStoryLink (single storyIdOf rule, id !== component)', () => {
    requireNav();
    const { readFileSync } = require('node:fs');
    const { join } = require('node:path');
    const appSrc = readFileSync(join(__dirname, '..', 'src', 'App.tsx'), 'utf8');
    assert.ok(
      /from\s+['"]\.\/galleryNav['"]/.test(appSrc) && /storyIdOf/.test(appSrc) && /parseStoryLink/.test(appSrc),
      'App.tsx must delegate deep-link movement to galleryNav (import parseStoryLink + storyIdOf)',
    );
    assert.ok(
      /storyIdOf\(stor\w*\)\s*===\s*id/.test(appSrc),
      'App onOpenStoryLink must look up stories with the single storyIdOf rule (storyIdOf(story) === id), not component-only',
    );
    assert.ok(
      /code:\s*['"]unknown-story['"]/.test(appSrc),
      'App onOpenStoryLink must surface { code: \'unknown-story\' } for unknown ids',
    );
    // Behavioral parity through the shared rule: a story whose id differs
    // from its component is reachable by id, and unknown ids reject.
    const stories = [
      { id: 'story-alpha-1', component: 'Button', state: {} },
      { id: 'story-beta-2', component: 'Card', state: {} },
    ];
    const n = navigatorFor(stories);
    assert.deepEqual(n.openStoryLink('gallery://story/story-beta-2'), { index: 1, storyId: 'story-beta-2' });
    const unknown = n.openStoryLink('gallery://story/no-such-story');
    assert.equal(unknown && unknown.code, 'unknown-story');
    assert.equal(n.current().index, 1, 'unknown-id rejection must keep the current story');
  });

  it('paging changes only the story, never mutates story state', () => {
    const n = navigatorFor(demoStories());
    const before = JSON.stringify(demoStories().map((s) => s.state));
    n.navigateStory('next');
    n.openStoryLink('gallery://story/cards');
    n.navigateStory('prev');
    const after = JSON.stringify(n.storyStates());
    assert.equal(after, before, 'navigation must change the index, never story state');
  });

  it('outside-tap / broadcast paging paths never paginate (removed fallback)', () => {
    const n = navigatorFor(demoStories());
    requireNav();
    assert.equal(typeof nav.handleOutsideTap, 'function', 'galleryNav must export handleOutsideTap()');
    assert.equal(typeof nav.handleBroadcast, 'function', 'galleryNav must export handleBroadcast()');
    assert.equal(n.handleOutsideTap({ x: -1, y: 5, dragged: false }), false);
    assert.equal(n.handleBroadcast('muse.native.NATIVE_ADVANCE_PAGE'), false);
    assert.equal(n.current().index, 0);
  });
});

describe('T2 — inside-surface gestures never page (AC-M3-012 text path interactive)', () => {
  const INSIDE = ['tap', 'double-tap', 'drag', 'long-press', 'scroll'];

  it('exports the isPagingGesture predicate', () => {
    requireNav();
    assert.equal(typeof nav.isPagingGesture, 'function', 'galleryNav must export isPagingGesture()');
  });

  for (const gesture of INSIDE) {
    it(`inside-surface ${gesture} never pages`, () => {
      requireNav();
      assert.equal(nav.isPagingGesture('inside-surface', gesture), false);
    });
  }

  it('inside-surface key events never page', () => {
    requireNav();
    assert.equal(nav.isPagingGesture('inside-surface', 'key'), false);
  });

  it('shell-chrome button/key/deep-link invocations page', () => {
    requireNav();
    assert.equal(nav.isPagingGesture('shell-chrome', 'tap'), true);
    assert.equal(nav.isPagingGesture('shell-chrome', 'key'), true);
    assert.equal(nav.isPagingGesture('shell-chrome', 'deep-link'), true);
  });

  it('text story: tap focuses + caret per AC-M3-012 with no paging callback', () => {
    const n = navigatorFor(demoStories());
    requireNav();
    assert.equal(typeof n.tapTextField, 'function', 'navigator must expose tapTextField()');
    let paged = 0;
    const result = n.tapTextField({ onPage: () => { paged += 1; } });
    assert.equal(result.focused, true);
    assert.equal(result.caret, true);
    assert.equal(paged, 0);
  });

  it('text story: double-tap/drag selects and scroll pans with no paging', () => {
    const n = navigatorFor(demoStories());
    let paged = 0;
    const onPage = () => { paged += 1; };
    assert.equal(n.textGesture('double-tap', { onPage }).selected, true);
    assert.equal(n.textGesture('drag', { onPage }).selected, true);
    assert.equal(n.textGesture('scroll', { onPage }).panned, true);
    assert.equal(paged, 0);
  });

  it('text story: control activation fires in place with no paging', () => {
    const n = navigatorFor(demoStories());
    let paged = 0;
    let activated = 0;
    n.textGesture('tap', { onPage: () => { paged += 1; }, onActivate: () => { activated += 1; } });
    assert.equal(activated, 1);
    assert.equal(paged, 0);
  });
});
