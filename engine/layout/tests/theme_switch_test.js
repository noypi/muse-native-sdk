'use strict';
// TC-LAYOUT-021 (AC-LAYOUT-021) — Batch 2 T5.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T5 `Interface:` line, never to implementation.
// Module resolution: `../tokens/theme.js` once it lands, else an inline
// fallback stub that never notifies, so every notification assertion below
// is red until theme.js lands — and the harness itself never crashes.
//
// SEED ASSUMPTION (pinned in unit-tests/T5.md, shared with T7): the theme
// store ships two tokens across themes 'light' and 'dark':
//   't.theme.changing' — 'change-light' on light, 'change-dark' on dark;
//   't.theme.stable'   — 'stable-value' on both themes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../tokens/theme.js');
} catch {
  // Fallback stub: present so the harness loads; never notifies, so every
  // notification assertion below fails (red) until ../tokens/theme.js lands.
  mod = {
    setTheme: () => {},
    subscribeToken: () => () => {},
  };
}
const { setTheme, subscribeToken } = mod;

// ---------------------------------------------------------------------------
// T5 contract:
// `setTheme(theme: string): void;
//  subscribeToken(token: string, cb: (value: string) => void): () => void`
// ---------------------------------------------------------------------------
describe('TC-LAYOUT-021 — theme switching propagates through signals to subscribers only (AC-LAYOUT-021)', () => {
  it('notifies subscribers of a changed token with the new value', () => {
    setTheme('light');
    const seen = [];
    const unsub = subscribeToken('t.theme.changing', (v) => seen.push(v));
    try {
      setTheme('dark');
      assert.deepEqual(seen, ['change-dark']);
    } finally {
      unsub();
    }
  });

  it('does not notify subscribers of tokens unchanged by the switch', () => {
    setTheme('light');
    const seen = [];
    const unsub = subscribeToken('t.theme.stable', (v) => seen.push(v));
    try {
      setTheme('dark');
      assert.deepEqual(seen, []);
    } finally {
      unsub();
    }
  });

  it('notifies all subscribers of a changed token, exactly once each', () => {
    setTheme('light');
    let a = 0;
    let b = 0;
    const unsubA = subscribeToken('t.theme.changing', () => {
      a += 1;
    });
    const unsubB = subscribeToken('t.theme.changing', () => {
      b += 1;
    });
    try {
      setTheme('dark');
      assert.equal(a, 1);
      assert.equal(b, 1);
    } finally {
      unsubA();
      unsubB();
    }
  });

  it('unsubscribe stops notifications via the returned function', () => {
    setTheme('light');
    assert.equal(typeof subscribeToken('t.theme.changing', () => {}), 'function');
    let calls = 0;
    const unsub = subscribeToken('t.theme.changing', () => {
      calls += 1;
    });
    unsub();
    setTheme('dark');
    setTheme('light');
    assert.equal(calls, 0);
  });

  it('setting the same theme notifies nobody', () => {
    setTheme('dark');
    let calls = 0;
    const unsub = subscribeToken('t.theme.changing', () => {
      calls += 1;
    });
    try {
      setTheme('dark');
      assert.equal(calls, 0);
    } finally {
      unsub();
    }
  });

  it('switching back restores the previous value for subscribers', () => {
    setTheme('light');
    const seen = [];
    const unsub = subscribeToken('t.theme.changing', (v) => seen.push(v));
    try {
      setTheme('dark');
      setTheme('light');
      assert.deepEqual(seen, ['change-dark', 'change-light']);
    } finally {
      unsub();
    }
  });

  it('an unknown theme notifies nobody and leaves the store consistent', () => {
    setTheme('light');
    let calls = 0;
    const unsub = subscribeToken('t.theme.changing', () => {
      calls += 1;
    });
    try {
      try {
        setTheme('no-such-theme-xyz');
      } catch {
        // Either throwing or no-op is acceptable; what matters is no
        // notification escapes and the store still switches afterwards.
      }
      assert.equal(calls, 0);
      const seen = [];
      const unsub2 = subscribeToken('t.theme.changing', (v) => seen.push(v));
      try {
        setTheme('dark');
        assert.deepEqual(seen, ['change-dark']);
      } finally {
        unsub2();
      }
    } finally {
      unsub();
    }
  });
});
