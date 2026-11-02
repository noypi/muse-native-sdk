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
    // T6 (engine-repair) M3 token resolution — FR-M3-005/007/008,
    // AC-M3-009/015/018. Committed role names from
    // engine/layout/m3_matrix.json token tables; hex baselines mirror
    // engine/runtime/src/gallery_present.cpp kLightPrimary et al.
    // Flat gray (#808080) is never a value here — it is an error, never
    // a pass. Elevation level-1 → 1dp, shape corner.medium → 12dp (M3).
    'md.sys.color.primary': '#6750A4',
    'md.sys.state.elevation.level-1': 1,
    'md.sys.shape.corner.medium': 12,
  },
  dark: {
    't.theme.changing': 'change-dark',
    't.theme.stable': 'stable-value',
    'md.sys.color.primary': '#D0BCFF',
    'md.sys.state.elevation.level-1': 1,
    'md.sys.shape.corner.medium': 12,
  },
  seasonal: {
    't.theme.changing': 'change-seasonal',
    't.theme.stable': 'stable-value',
    'md.sys.color.primary': '#7D5260',
    'md.sys.state.elevation.level-1': 1,
    'md.sys.shape.corner.medium': 12,
  },
};

// Component -> token references (T7 repaint set). ChangedCard references a
// token that varies across themes; StableLabel references only stable tokens.
const COMPONENT_TOKENS = {
  ChangedCard: ['t.theme.changing'],
  StableLabel: ['t.theme.stable'],
};

let currentTheme = 'light';

// Flat-gray fallback sentinel: an ERROR, never a silent pass.
const FLAT_GRAY = '#808080';

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

module.exports = { setTheme, subscribeToken, repaintTheme, resolveThemeTokens };

// T6 (engine-repair) — resolve M3 theme tokens to surface params
// (FR-M3-005/007/008, AC-M3-009/015/018). Returns
// `{ primary, elevationDp, shapeDp }` with a distinct opaque M3 primary
// per theme and nonzero elevation/shape dp; unknown themes (or a
// gray/unresolved primary) fail closed with `null` — flat gray is an
// error, never a pass. Never throws.
function resolveThemeTokens(theme) {
  if (typeof theme !== 'string' || !Object.hasOwn(THEMES, theme)) return null;
  const table = THEMES[theme];
  const primary = table['md.sys.color.primary'];
  const elevationDp = table['md.sys.state.elevation.level-1'];
  const shapeDp = table['md.sys.shape.corner.medium'];
  if (typeof primary !== 'string' || !/^#[0-9a-fA-F]{6}$/.test(primary)) {
    return null;
  }
  if (primary.toLowerCase() === FLAT_GRAY) return null;
  if (!(elevationDp > 0) || !(shapeDp > 0)) return null;
  return { primary, elevationDp, shapeDp };
}

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
