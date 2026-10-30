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

  function navigateStory(direction) {
    let next = index;
    if (direction === 'next') {
      next = index + 1;
    } else if (direction === 'prev') {
      next = index - 1;
    } else {
      return { code: 'boundary' };
    }
    if (next < 0 || next >= list.length) {
      return { code: 'boundary' };
    }
    index = next;
    return { index, storyId: storyIdOf(list[index]) };
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
  isPagingGesture,
  PrevButton,
  NextButton,
  StoryIndex,
  handleKey,
  handleOutsideTap,
  handleBroadcast,
  STORY_LINK_PREFIX,
  BROADCAST_ADVANCE_PAGE,
};
