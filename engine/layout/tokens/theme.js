'use strict';
// FR-LAYOUT-009 / AC-LAYOUT-021 — signal-driven theme switching.
// Single current-theme pointer fans out to per-token signals in O(1) per
// subscriber: only holders whose resolved value changed are written, so only
// their subscribers are notified. No tree walk — untouched tokens stay silent.

const { state, pushActiveConsumer, popActiveConsumer } = require('../../../packages/core/lib/state.js');
const { flush } = require('../../../packages/core/lib/scheduler.js');

const THEMES = {
  light: {
    't.theme.changing': 'change-light',
    't.theme.stable': 'stable-value',
  },
  dark: {
    't.theme.changing': 'change-dark',
    't.theme.stable': 'stable-value',
  },
  seasonal: {
    't.theme.changing': 'change-seasonal',
    't.theme.stable': 'stable-value',
  },
};

// Component -> token references (T7 repaint set). ChangedCard references a
// token that varies across themes; StableLabel references only stable tokens.
const COMPONENT_TOKENS = {
  ChangedCard: ['t.theme.changing'],
  StableLabel: ['t.theme.stable'],
};

let currentTheme = 'light';

// token -> state holder for the token's resolved value.
const holders = new Map();

function valueFor(token, theme) {
  const table = THEMES[theme];
  if (table && Object.hasOwn(table, token)) return table[token];
  return token;
}

function holderFor(token) {
  let holder = holders.get(token);
  if (!holder) {
    holder = state(valueFor(token, currentTheme));
    holders.set(token, holder);
  }
  return holder;
}

function setTheme(theme) {
  if (typeof theme !== 'string' || !Object.hasOwn(THEMES, theme)) return;
  if (theme === currentTheme) return;
  currentTheme = theme;
  const next = THEMES[theme];
  // Write only holders whose resolved value actually changed; identical
  // writes are no-ops at the signal layer and schedule nothing.
  for (const [token, holder] of holders) {
    const newValue = Object.hasOwn(next, token) ? next[token] : token;
    if (!Object.is(holder.value, newValue)) holder.value = newValue;
  }
  // Scheduler auto-flush is async (microtask); deliver synchronously so a
  // switch notifies before setTheme returns.
  flush();
}

function subscribeToken(token, cb) {
  if (typeof cb !== 'function') {
    throw new TypeError('subscribeToken(token, cb): cb must be a function');
  }
  const holder = holderFor(token);
  const consumer = {
    deps: new Set(),
    job() {
      cb(holder.value);
    },
  };
  pushActiveConsumer(consumer);
  try {
    // Tracked read subscribes `consumer` to the holder's record.
    void holder.value;
  } finally {
    popActiveConsumer();
  }
  let active = true;
  return function unsubscribe() {
    if (!active) return;
    active = false;
    for (const subs of consumer.deps) {
      subs.delete(consumer);
    }
    consumer.deps.clear();
  };
}

module.exports = { setTheme, subscribeToken, repaintTheme };

// FR-LAYOUT-009 / AC-LAYOUT-023 — bounded single-frame repaint built on the
// T5 subscription graph. Diffs the two theme tables, re-resolves only
// components referencing a changed token, then fans out through setTheme so
// only holders whose value changed notify. Same-theme and unknown-theme
// paths return a bounded empty result (never hang, never walk the tree).
function repaintTheme(from, to) {
  if (from === to) return { frames: 0, reresolved: [] };
  if (
    typeof from !== 'string' ||
    typeof to !== 'string' ||
    !Object.hasOwn(THEMES, from) ||
    !Object.hasOwn(THEMES, to)
  ) {
    return { frames: 0, reresolved: [] };
  }
  const prev = THEMES[from];
  const next = THEMES[to];
  const changed = new Set();
  for (const token of new Set([...Object.keys(prev), ...Object.keys(next)])) {
    if (!Object.is(valueFor(token, from), valueFor(token, to))) changed.add(token);
  }
  const reresolved = Object.keys(COMPONENT_TOKENS).filter((component) =>
    COMPONENT_TOKENS[component].some((token) => changed.has(token)),
  );
  // Propagate through the existing subscription graph; only changed holders
  // notify, so unchanged-token subscribers stay silent.
  setTheme(to);
  return { frames: reresolved.length > 0 ? 1 : 0, reresolved };
}
