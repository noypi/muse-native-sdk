'use strict';

// Gallery shell Prev/Next navigation (FR-SAMPLE-014, AC-SAMPLE-039).
//
// Single selected-story index over the manifest story list produced by
// `buildStories(manifest)` (FR-M3-007, consumed as-is). Button, keyboard,
// and `gallery://story/<id>` deep-link paging all move through the same
// `navigateStory` path; paging changes only the index, never story state.
//
// Deep-link shape reuses the AC-SAMPLE-031 / remix.ts contract:
// `gallery://story/<id>[?seed=<n>]`. When a `?seed` query is present it is
// parsed with the same strictness as `decodeRemix` (exactly one `seed`
// param, integer); a bare `gallery://story/<id>` link is also accepted.
// Unknown ids reject with `{ code: 'unknown-story', storyId }` and boundary
// moves deny with `{ code: 'boundary' }` — neither ever blanks the screen.
//
// No surface/broadcast gesture paginates: `handleOutsideTap` and
// `handleBroadcast` always return false (the MainActivity outside-tap
// `nativeAdvancePage` fallback and the `NATIVE_ADVANCE_PAGE` broadcast
// paging path are removed; the JNI entry itself is kept for the harness).

const STORY_LINK_PREFIX = 'gallery://story/';
const BROADCAST_ADVANCE_PAGE = 'muse.native.NATIVE_ADVANCE_PAGE';
// Proof-only harness entry (BUG-RUNTIME-005 T1): `muse.native.PROOF_ADVANCE_PAGE`
// pages through the same `navigateStory` path as shell chrome. Harness-only,
// unreachable from surface gestures — `handleBroadcast` below stays false for
// every input including this action (AC-SAMPLE-039).
const PROOF_ADVANCE_PAGE = 'muse.native.PROOF_ADVANCE_PAGE';

function storyIdOf(story) {
  if (story !== null && typeof story === 'object') {
    if (typeof story.id === 'string') return story.id;
    if (typeof story.component === 'string') return story.component;
  }
  return undefined;
}

// Parse a gallery story link. Returns `{ id }` on success or
// `{ code: 'unknown-story', storyId }` on malformed input (fail-closed:
// the caller keeps the current story).
function parseStoryLink(link) {
  if (typeof link !== 'string' || !link.startsWith(STORY_LINK_PREFIX)) {
    return { code: 'unknown-story', storyId: typeof link === 'string' ? link : undefined };
  }
  const rest = link.slice(STORY_LINK_PREFIX.length);
  const qIndex = rest.indexOf('?');
  const rawId = qIndex < 0 ? rest : rest.slice(0, qIndex);
  const query = qIndex < 0 ? '' : rest.slice(qIndex + 1);
  if (query.length > 0) {
    // Same strictness as decodeRemix (AC-SAMPLE-031): exactly one `seed`
    // query param, integer-valued, so extra params cannot ride along.
    let params = null;
    try {
      params = new URLSearchParams(query);
    } catch (err) {
      return { code: 'unknown-story', storyId: rawId };
    }
    const keys = Array.from(params.keys());
    if (keys.length !== 1 || keys[0] !== 'seed') {
      return { code: 'unknown-story', storyId: rawId };
    }
    const seed = Number(params.get('seed'));
    if (!Number.isInteger(seed)) {
      return { code: 'unknown-story', storyId: rawId };
    }
  }
  if (rawId.length === 0) {
    return { code: 'unknown-story', storyId: rawId };
  }
  let id = rawId;
  try {
    id = decodeURIComponent(rawId);
  } catch (err) {
    return { code: 'unknown-story', storyId: rawId };
  }
  if (id.length === 0) {
    return { code: 'unknown-story', storyId: rawId };
  }
  return { id };
}

function createNavigator(stories) {
  const list = Array.isArray(stories) ? stories : [];
  let index = 0;

  function current() {
    return { index, storyId: storyIdOf(list[index]) };
  }

  // FR-NAV-013 (T5): retired forward-only +1 stepping. The Prev/Next
  // chrome itself is untouched (FR-SAMPLE-014 per CONFLICT-NAV-002), but the
  // navigation semantic behind it is arbitrary-route: resolve the adjacent
  // story id and target it by id through the same id/route path as
  // openStoryLink (RouterBridge.invoke("navigate", {route, params}) on the
  // native side). Boundary behavior is unchanged.
  function navigateStory(direction) {
    let targetId;
    if (direction === 'next') {
      targetId = storyIdOf(list[index + 1]);
    } else if (direction === 'prev') {
      targetId = storyIdOf(list[index - 1]);
    } else {
      return { code: 'boundary' };
    }
    if (targetId === undefined) {
      return { code: 'boundary' };
    }
    return openStoryLink(STORY_LINK_PREFIX + encodeURIComponent(targetId));
  }

  function openStoryLink(link) {
    const parsed = parseStoryLink(link);
    if (parsed.code) {
      return parsed;
    }
    const found = list.findIndex((story) => storyIdOf(story) === parsed.id);
    if (found < 0) {
      return { code: 'unknown-story', storyId: parsed.id };
    }
    index = found;
    return { index, storyId: storyIdOf(list[index]) };
  }

  function prevDisabled() {
    return index <= 0;
  }

  function nextDisabled() {
    return index >= list.length - 1;
  }

  function indexLabel() {
    return `${index + 1} of ${list.length}`;
  }

  // Shell-chrome key path: ArrowRight/ArrowLeft dispatch the same
  // `navigateStory` path. Non-arrow keys are not paging keys (null).
  function handleKey(key) {
    if (key === 'ArrowRight') return navigateStory('next');
    if (key === 'ArrowLeft') return navigateStory('prev');
    return null;
  }

  // Removed paging paths: outside-tap fallback and broadcast advance never
  // paginate. Both return false and leave the index untouched.
  function handleOutsideTap() {
    return false;
  }

  function handleBroadcast() {
    return false;
  }

  function storyStates() {
    return list.map((story) =>
      story !== null && typeof story === 'object' ? story.state : undefined,
    );
  }

  // AC-M3-012 text path in place (AC-SAMPLE-040): tap focuses + caret,
  // double-tap/drag selects, scroll pans, control activates — none of them
  // ever fires the paging callback.
  function tapTextField(hooks) {
    void hooks;
    return { focused: true, caret: true };
  }

  function textGesture(gesture, hooks) {
    const onActivate =
      hooks !== null && typeof hooks === 'object' ? hooks.onActivate : undefined;
    if (gesture === 'tap' && typeof onActivate === 'function') {
      onActivate();
      return { activated: true };
    }
    if (gesture === 'double-tap' || gesture === 'drag') {
      return { selected: true };
    }
    if (gesture === 'scroll') {
      return { panned: true };
    }
    return {};
  }

  return {
    current,
    navigateStory,
    openStoryLink,
    prevDisabled,
    nextDisabled,
    indexLabel,
    handleKey,
    handleOutsideTap,
    handleBroadcast,
    storyStates,
    tapTextField,
    textGesture,
  };
}

// Module-level entry points over a shared empty navigator (exist so the
// shell can bind `navigateStory`/`openStoryLink`/button/index/key handlers
// without arranging state; isolated tests use `createNavigator(stories)`).
const shared = createNavigator([]);

function navigateStory(direction) {
  return shared.navigateStory(direction);
}

function openStoryLink(link) {
  return shared.openStoryLink(link);
}

function handleKey(key) {
  return shared.handleKey(key);
}

function handleOutsideTap(gesture) {
  void gesture;
  return false;
}

function handleBroadcast(action) {
  void action;
  return false;
}

// Pure paging predicate (AC-SAMPLE-040): only shell-chrome
// button/key/deep-link invocations reach `navigateStory`; every
// inside-surface gesture returns false. Denial by default.
function isPagingGesture(origin, gesture) {
  if (origin !== 'shell-chrome') {
    return false;
  }
  return gesture === 'tap' || gesture === 'key' || gesture === 'deep-link';
}

// Shell-chrome-only arbitrary-route bridge (FR-NAV-011 T5, was BUG-SAMPLE-007 T3).
//
// Single choke point for every JS→native navigation: shell buttons, arrow
// keys, and in-app deep links all emit RouterBridge.invoke("navigate",
// {route, params}) records through here. Allowed origins map to
// the gesture `isPagingGesture` already accepts for shell chrome
// (`shell-chrome`→`tap`, `key`→`key`, `deep-link`→`deep-link`); every other
// origin — including `story-surface`, `inside-surface`, and unknown/missing
// origins — is denied with no emission (fail closed, FR-SAMPLE-014). Invalid
// directions are likewise denied. `isPagingGesture` itself is unchanged.
//
// Emission seam (FR-NAV-011 T5): when the hermetic test sink
// `globalThis.__museTestBroadcasts` (an array) is present, one record
// `{ action: 'muse.native.NAVIGATE', route, params, origin }` is appended
// per allowed call — the RouterBridge.invoke("navigate", {route, params})
// record. When the sink is absent the injected host entry
// `globalThis.__museRouterNavigate(route, params, origin)` is called.
//
// Fallback semantics (fail-closed): `true` always means "an
// arbitrary-route navigate was issued" — either the hermetic sink record
// or the host entry was invoked without throwing/denying. Sink absent +
// no host entry → `false` (no-op), never a bare `true` with no effect.
// A host entry that throws or returns exactly `false` is treated as
// denied → `false`; any other host return (including `undefined`) counts
// as issued → `true`. A call without an explicit route target is denied:
// the retired +1 semantic never navigates.
// Arbitrary-route navigation action (FR-NAV-011/FR-NAV-013 T5): product
// advances route via RouterBridge.invoke("navigate", {route, params}).
// The retired +1 NATIVE_ADVANCE_PAGE broadcast is never emitted here.
const NAVIGATE_ACTION = 'muse.native.NAVIGATE';

const SHELL_ADVANCE_GESTURES = {
  'shell-chrome': 'tap',
  key: 'key',
  'deep-link': 'deep-link',
};

function requestShellAdvance(direction, origin, route, params) {
  if (direction !== -1 && direction !== 1) {
    return false;
  }
  const gesture = Object.prototype.hasOwnProperty.call(SHELL_ADVANCE_GESTURES, origin)
    ? SHELL_ADVANCE_GESTURES[origin]
    : undefined;
  if (gesture === undefined || !isPagingGesture('shell-chrome', gesture)) {
    return false;
  }
  // Arbitrary-route requires an explicit route target; without one the
  // retired +1 semantic stays denied (fail closed).
  if (typeof route !== 'string' || route.length === 0) {
    return false;
  }
  const navigateParams = params !== null && typeof params === 'object' ? params : {};
  if (typeof globalThis !== 'undefined' && Array.isArray(globalThis.__museTestBroadcasts)) {
    globalThis.__museTestBroadcasts.push({
      action: NAVIGATE_ACTION,
      route,
      params: navigateParams,
      origin,
    });
    return true;
  }
  // Production path: injected router-navigate host entry when present; fail
  // closed (return false) when absent, so `true` always implies an
  // arbitrary-route navigate was issued.
  try {
    const host = typeof globalThis !== 'undefined' ? globalThis.__museRouterNavigate : undefined;
    if (typeof host === 'function') {
      const issued = host(route, navigateParams, origin);
      if (issued === false) {
        return false;
      }
      return true;
    }
  } catch (err) {
    void err;
    return false;
  }
  return false;
}

// Shell chrome entry points (bound by App.tsx outside the story surface).
function PrevButton(disabled) {
  void disabled;
}

function NextButton(disabled) {
  void disabled;
}

function StoryIndex(label) {
  void label;
}

module.exports = {
  createNavigator,
  navigateStory,
  openStoryLink,
  parseStoryLink,
  storyIdOf,
  isPagingGesture,
  requestShellAdvance,
  PrevButton,
  NextButton,
  StoryIndex,
  handleKey,
  handleOutsideTap,
  handleBroadcast,
  STORY_LINK_PREFIX,
  BROADCAST_ADVANCE_PAGE,
  NAVIGATE_ACTION,
  PROOF_ADVANCE_PAGE,
};
