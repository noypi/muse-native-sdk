'use strict';
// BUG-SAMPLE-007 Batch 1 (T1) — shell-button→native bridge regression test.
// FR-SAMPLE-014, AC-SAMPLE-039 (TC-SAMPLE-039 regression).
//
// Hermetic: node:test + node:assert/strict only. No network, no secrets, no
// config, no device/adb, no shared mutable state (the module is freshly
// required per test and the emission sink is cleared per test).
//
// Binds ONLY to the plan interface:
//   T1: `requestShellAdvance(direction: -1 | 1, origin: PagingOrigin): boolean`
//       (new, in `examples/m3-gallery/src/galleryNav.js`; returns `true` iff a
//       native-observable advance was issued for an allowed shell-chrome/key/
//       deep-link origin, `false` for any surface or unknown origin).
//
// ASSUMPTIONS (contract genuinely silent — coder reconciles, behavior pinned):
//   A-T1a (emission seam): the test installs `globalThis.__museTestBroadcasts`
//       (an array) before requiring the module. The implementation MUST append
//       one record `{ action, direction, origin }` with
//       `action === 'muse.native.NATIVE_ADVANCE_PAGE'` to that array whenever
//       it is present and the call is allowed, and append nothing when the
//       call is denied. When the array is absent the implementation uses its
//       real bridge; hermetic tests always install it, so the native effect
//       is observable in-process without a device.
//   A-T1b (origin widening): the current `PagingOrigin` in `App.tsx` is
//       `'inside-surface' | 'shell-chrome'`; the plan allowlist is
//       `{shell-chrome, key, deep-link}`. The coder widens the origin set for
//       this bridge; `inside-surface` (and every other origin) stays denied.
//   A-T1c (index boundary): boundary clamp (Prev at index 0 / Next at last →
//       `false`, no emit, disabled states unchanged) is owned by the coder
//       through the shared navigator position and the `App.tsx` disabled
//       states. These tests pin the gate (allowed → true + one emit; denied
//       → false + no emit) and the navigator boundary model; the coder wires
//       the position check so a boundary call emits nothing.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const NAV_PATH = '../src/galleryNav.js';
const EXPECTED_ACTION = 'muse.native.NATIVE_ADVANCE_PAGE';

function freshRequireNav() {
  const resolved = require.resolve(NAV_PATH);
  delete require.cache[resolved];
  return require(NAV_PATH);
}

function installSink() {
  globalThis.__museTestBroadcasts = [];
  return globalThis.__museTestBroadcasts;
}

function clearSink() {
  delete globalThis.__museTestBroadcasts;
}

function clearHost() {
  delete globalThis.__museHostAdvance;
}

function installHostStub(impl) {
  const calls = [];
  globalThis.__museHostAdvance = (direction, origin) => {
    calls.push({ direction, origin });
    return impl ? impl(direction, origin) : undefined;
  };
  return calls;
}

describe('T1 requestShellAdvance shell-chrome→native bridge (AC-SAMPLE-039)', () => {
  beforeEach(() => {
    installSink();
  });

  it('exports requestShellAdvance as a function', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(
        typeof nav.requestShellAdvance,
        'function',
        'galleryNav.js must export requestShellAdvance(direction, origin)',
      );
    } finally {
      clearSink();
    }
  });

  it('shell-chrome origin with direction -1 returns true and emits one advance', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      const ok = nav.requestShellAdvance(-1, 'shell-chrome');
      assert.equal(ok, true);
      assert.equal(sink.length, 1, 'allowed call must issue exactly one native-observable advance');
      assert.equal(sink[0].action, EXPECTED_ACTION);
      assert.equal(sink[0].direction, -1);
      assert.equal(sink[0].origin, 'shell-chrome');
    } finally {
      clearSink();
    }
  });

  it('shell-chrome origin with direction +1 returns true and emits one advance', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      const ok = nav.requestShellAdvance(1, 'shell-chrome');
      assert.equal(ok, true);
      assert.equal(sink.length, 1);
      assert.equal(sink[0].action, EXPECTED_ACTION);
      assert.equal(sink[0].direction, 1);
      assert.equal(sink[0].origin, 'shell-chrome');
    } finally {
      clearSink();
    }
  });

  it('key origin returns true and emits one advance', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      const ok = nav.requestShellAdvance(1, 'key');
      assert.equal(ok, true);
      assert.equal(sink.length, 1);
      assert.equal(sink[0].action, EXPECTED_ACTION);
      assert.equal(sink[0].origin, 'key');
    } finally {
      clearSink();
    }
  });

  it('deep-link origin returns true and emits one advance', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      const ok = nav.requestShellAdvance(-1, 'deep-link');
      assert.equal(ok, true);
      assert.equal(sink.length, 1);
      assert.equal(sink[0].action, EXPECTED_ACTION);
      assert.equal(sink[0].origin, 'deep-link');
    } finally {
      clearSink();
    }
  });

  it('story-surface origin returns false and emits nothing (FR-SAMPLE-014 ban)', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      assert.equal(nav.requestShellAdvance(-1, 'story-surface'), false);
      assert.equal(nav.requestShellAdvance(1, 'story-surface'), false);
      assert.equal(sink.length, 0, 'surface origin must never emit');
    } finally {
      clearSink();
    }
  });

  it('inside-surface origin returns false and emits nothing', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      assert.equal(nav.requestShellAdvance(1, 'inside-surface'), false);
      assert.equal(sink.length, 0);
    } finally {
      clearSink();
    }
  });

  it('missing or unknown origin returns false and emits nothing', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      assert.equal(nav.requestShellAdvance(1, undefined), false);
      assert.equal(nav.requestShellAdvance(1, null), false);
      assert.equal(nav.requestShellAdvance(1, ''), false);
      assert.equal(nav.requestShellAdvance(1, 'broadcast'), false);
      assert.equal(nav.requestShellAdvance(1, 'tap'), false);
      assert.equal(sink.length, 0, 'unknown origin must never emit (fail-closed)');
    } finally {
      clearSink();
    }
  });

  it('invalid direction returns false and emits nothing even for allowed origin', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      assert.equal(nav.requestShellAdvance(0, 'shell-chrome'), false);
      assert.equal(nav.requestShellAdvance(2, 'shell-chrome'), false);
      assert.equal(nav.requestShellAdvance('next', 'shell-chrome'), false);
      assert.equal(nav.requestShellAdvance(NaN, 'shell-chrome'), false);
      assert.equal(sink.length, 0);
    } finally {
      clearSink();
    }
  });

  it('allowed call emits exactly once (no duplicate advance per tap)', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      assert.equal(nav.requestShellAdvance(1, 'shell-chrome'), true);
      assert.equal(nav.requestShellAdvance(1, 'shell-chrome'), true);
      assert.equal(sink.length, 2, 'one tap issues exactly one advance');
    } finally {
      clearSink();
    }
  });

  it('navigator boundary model: Prev at index 0 denies with boundary code', () => {
    const nav = freshRequireNav();
    try {
      const stories = [{ id: 'a' }, { id: 'b' }, { id: 'c' }];
      const n = nav.createNavigator(stories);
      assert.equal(n.prevDisabled(), true);
      assert.deepEqual(n.navigateStory('prev'), { code: 'boundary' });
      assert.deepEqual(n.current(), { index: 0, storyId: 'a' });
    } finally {
      clearSink();
    }
  });

  it('navigator boundary model: Next at last story denies with boundary code', () => {
    const nav = freshRequireNav();
    try {
      const stories = [{ id: 'a' }, { id: 'b' }];
      const n = nav.createNavigator(stories);
      assert.deepEqual(n.navigateStory('next'), { index: 1, storyId: 'b' });
      assert.equal(n.nextDisabled(), true);
      assert.deepEqual(n.navigateStory('next'), { code: 'boundary' });
      assert.deepEqual(n.current(), { index: 1, storyId: 'b' });
    } finally {
      clearSink();
    }
  });

  it('surface denial helpers stay false (ban intact alongside the new bridge)', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(nav.handleOutsideTap(), false);
      assert.equal(nav.handleBroadcast('muse.native.NATIVE_ADVANCE_PAGE'), false);
      assert.equal(nav.isPagingGesture('story-surface', 'tap'), false);
      assert.equal(nav.isPagingGesture('inside-surface', 'tap'), false);
    } finally {
      clearSink();
    }
  });

  it('sink absent + host stub present: host called once with (direction, origin), returns true', () => {
    clearSink();
    clearHost();
    const nav = freshRequireNav();
    const calls = installHostStub();
    try {
      const ok = nav.requestShellAdvance(1, 'shell-chrome');
      assert.equal(ok, true);
      assert.equal(calls.length, 1, 'host stub must be called exactly once');
      assert.equal(calls[0].direction, 1);
      assert.equal(calls[0].origin, 'shell-chrome');
    } finally {
      clearSink();
      clearHost();
    }
  });

  it('sink absent + no host: returns false fail-closed', () => {
    clearSink();
    clearHost();
    const nav = freshRequireNav();
    try {
      assert.equal(nav.requestShellAdvance(1, 'shell-chrome'), false);
      assert.equal(nav.requestShellAdvance(-1, 'key'), false);
    } finally {
      clearSink();
      clearHost();
    }
  });

  it('sink absent + host throws: returns false', () => {
    clearSink();
    clearHost();
    const nav = freshRequireNav();
    installHostStub(() => {
      throw new Error('host boom');
    });
    try {
      assert.equal(nav.requestShellAdvance(1, 'shell-chrome'), false);
    } finally {
      clearSink();
      clearHost();
    }
  });

  it('sink absent + host returns false: returns false', () => {
    clearSink();
    clearHost();
    const nav = freshRequireNav();
    const calls = installHostStub(() => false);
    try {
      assert.equal(nav.requestShellAdvance(-1, 'deep-link'), false);
      assert.equal(calls.length, 1);
    } finally {
      clearSink();
      clearHost();
    }
  });

  it('sink absent + validation denial with host present: returns false, host never called', () => {
    clearSink();
    clearHost();
    const nav = freshRequireNav();
    const calls = installHostStub();
    try {
      assert.equal(nav.requestShellAdvance(1, 'story-surface'), false);
      assert.equal(nav.requestShellAdvance(0, 'shell-chrome'), false);
      assert.equal(nav.requestShellAdvance(1, 'broadcast'), false);
      assert.equal(calls.length, 0, 'validation denial must never invoke the host');
    } finally {
      clearSink();
      clearHost();
    }
  });

  it('string/boolean direction denied even for allowed origin (strict +-1)', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      assert.equal(nav.requestShellAdvance('1', 'shell-chrome'), false);
      assert.equal(nav.requestShellAdvance('-1', 'key'), false);
      assert.equal(nav.requestShellAdvance(true, 'shell-chrome'), false);
      assert.equal(nav.requestShellAdvance(undefined, 'shell-chrome'), false);
      assert.equal(sink.length, 0, 'non-numeric direction must never emit');
    } finally {
      clearSink();
    }
  });

  it('prototype-pollution origins denied with no emit (hasOwnProperty-safe lookup)', () => {
    const nav = freshRequireNav();
    try {
      assert.equal(typeof nav.requestShellAdvance, 'function');
      const sink = globalThis.__museTestBroadcasts;
      assert.equal(nav.requestShellAdvance(1, '__proto__'), false);
      assert.equal(nav.requestShellAdvance(1, 'constructor'), false);
      assert.equal(nav.requestShellAdvance(1, 'hasOwnProperty'), false);
      assert.equal(sink.length, 0, 'pollution-shaped origin must never emit');
    } finally {
      clearSink();
    }
  });

  it('sink absent + pollution origin with host present: denied, host never called', () => {
    clearSink();
    clearHost();
    const nav = freshRequireNav();
    const calls = installHostStub();
    try {
      assert.equal(nav.requestShellAdvance(1, '__proto__'), false);
      assert.equal(nav.requestShellAdvance(1, 'constructor'), false);
      assert.equal(calls.length, 0, 'pollution-shaped denial must never invoke the host');
    } finally {
      clearSink();
      clearHost();
    }
  });

  it('App.tsx keyboard path returns before emit at both boundaries (static pin)', () => {
    const { readFileSync } = require('node:fs');
    const { join } = require('node:path');
    const appSrc = readFileSync(join(__dirname, '..', 'src', 'App.tsx'), 'utf8');
    const keyBlock = appSrc.slice(appSrc.indexOf('onKeyDown'), appSrc.indexOf("window.addEventListener('keydown'"));
    assert.ok(keyBlock.length > 0, 'App.tsx must define onKeyDown keyboard handler');
    const nextEmit = keyBlock.indexOf("requestShellAdvance(1, 'key')");
    const prevEmit = keyBlock.indexOf("requestShellAdvance(-1, 'key')");
    assert.ok(nextEmit !== -1 && prevEmit !== -1, 'keyboard handler must emit via requestShellAdvance for both arrows');
    assert.ok(/if\s*\(\s*nextDisabled\s*\)\s*\{\s*return;/.test(keyBlock), 'ArrowRight must return-before-emit when nextDisabled');
    assert.ok(/if\s*\(\s*prevDisabled\s*\)\s*\{\s*return;/.test(keyBlock), 'ArrowLeft must return-before-emit when prevDisabled');
    assert.ok(keyBlock.indexOf('nextDisabled') < nextEmit, 'nextDisabled guard must precede the key +1 emit');
    assert.ok(keyBlock.indexOf('prevDisabled') < prevEmit, 'prevDisabled guard must precede the key -1 emit');
    assert.ok(
      /\},\s*\[stories\.length,\s*clampedIndex\]\)/.test(appSrc),
      'keyboard effect deps must include clampedIndex (stale-closure fix pin)',
    );
  });

  it('App.tsx deep-link path no-emit on same-index and unknown-id (static pin)', () => {
    const { readFileSync } = require('node:fs');
    const { join } = require('node:path');
    const appSrc = readFileSync(join(__dirname, '..', 'src', 'App.tsx'), 'utf8');
    const dlStart = appSrc.indexOf('onOpenStoryLink');
    assert.ok(dlStart !== -1, 'App.tsx must define onOpenStoryLink');
    const dlBlock = appSrc.slice(dlStart, appSrc.indexOf('setSelectedIndex(found)'));
    assert.ok(dlBlock.length > 0, 'deep-link handler body must be readable');
    assert.ok(
      /if\s*\(\s*found\s*!==\s*clampedIndex\s*\)/.test(dlBlock),
      'same-index deep link must skip the emit (found !== clampedIndex guard)',
    );
    const guardPos = dlBlock.search(/if\s*\(\s*found\s*!==\s*clampedIndex\s*\)/);
    const emitPos = dlBlock.indexOf("requestShellAdvance(");
    assert.ok(emitPos !== -1 && guardPos < emitPos, 'deep-link emit must sit inside the same-index guard');
    assert.ok(/if\s*\(\s*found\s*<\s*0\s*\)\s*\{\s*return\s*\{\s*code:\s*'unknown-story'/.test(dlBlock), 'unknown id must return unknown-story before any emit');
    const unknownPos = dlBlock.search(/if\s*\(\s*found\s*<\s*0/);
    assert.ok(unknownPos !== -1 && unknownPos < emitPos, 'unknown-id return must precede the deep-link emit');
  });
});
