'use strict';
// TC-LAYOUT-023 (AC-LAYOUT-023) — Batch 2 T7.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services, no dynamic-color hardware —
// palettes are injected/seeded (T6), never read from a device.
// Binds ONLY to the pinned T7 `Interface:` line, never to implementation.
// Module resolution: `../tokens/theme.js` (the T5 subscription-graph owner;
// "built on T5 subscription graph" per plan) once it exports `repaintTheme`,
// else an inline fallback stub that exceeds the frame budget, so every
// behavior case below is red until the implementation lands — and the
// harness itself never crashes.
//
// SEED ASSUMPTION (pinned in unit-tests/T7.md, shared with T5): the theme
// store ships tokens 't.theme.changing' (light 'change-light', dark
// 'change-dark') and 't.theme.stable' ('stable-value' on both themes), and a
// component registry where 'ChangedCard' references a changed token and
// 'StableLabel' references only unchanged tokens. Seasonal theme 'seasonal'
// changes at least the changing token.
//
// SCOPE: this file runs the unit-runnable half of TC-LAYOUT-023
// (frames + reresolved set with injected palettes). The on-device visual
// half ("no visual breakage without dynamic-color support") is manual/
// EXTERNAL and is recorded in unit-tests/T7.md, not executed here.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../tokens/theme.js');
} catch {
  mod = null;
}
const repaintTheme =
  mod && typeof mod.repaintTheme === 'function'
    ? mod.repaintTheme
    : () => ({ frames: 99, reresolved: ['everything'] });

// ---------------------------------------------------------------------------
// T7 contract:
// `repaintTheme(from: string, to: string):
//  { frames: number, reresolved: string[] }`
// ---------------------------------------------------------------------------
describe('TC-LAYOUT-023 — light/dark/seasonal repaint stays within one frame and re-resolves only changed-token components (AC-LAYOUT-023)', () => {
  it('light -> dark completes within one frame', () => {
    const out = repaintTheme('light', 'dark');
    assert.ok(Number.isInteger(out.frames), 'frames must be an integer');
    assert.ok(out.frames >= 0, 'frames must be non-negative');
    assert.ok(out.frames <= 1, `must repaint within one frame, took ${out.frames}`);
    assert.ok(Array.isArray(out.reresolved));
  });

  it('reresolved lists only components referencing a changed token', () => {
    const out = repaintTheme('light', 'dark');
    assert.ok(out.reresolved.includes('ChangedCard'), 'must re-resolve the changed-token component');
    assert.ok(!out.reresolved.includes('StableLabel'), 'must not re-resolve the unchanged-token component');
  });

  it('dark -> light completes within one frame with a symmetric reresolved set', () => {
    const out = repaintTheme('dark', 'light');
    assert.ok(out.frames <= 1, `must repaint within one frame, took ${out.frames}`);
    assert.ok(out.reresolved.includes('ChangedCard'));
    assert.ok(!out.reresolved.includes('StableLabel'));
  });

  it('seasonal switch completes within one frame and re-resolves only changed-token components', () => {
    const out = repaintTheme('light', 'seasonal');
    assert.ok(out.frames <= 1, `must repaint within one frame, took ${out.frames}`);
    assert.ok(out.reresolved.includes('ChangedCard'));
    assert.ok(!out.reresolved.includes('StableLabel'));
  });

  it('same-theme repaint re-resolves nothing and stays within one frame', () => {
    const out = repaintTheme('dark', 'dark');
    assert.ok(out.frames <= 1, `must stay within one frame, took ${out.frames}`);
    assert.deepEqual(out.reresolved, []);
  });

  it('seed-fallback (no dynamic color) path repaints without breakage: unknown themes either throw or return a bounded empty result', () => {
    let out = null;
    let threw = false;
    try {
      out = repaintTheme('light', 'no-such-theme-xyz');
    } catch {
      threw = true;
    }
    assert.ok(
      threw || (out.frames <= 1 && Array.isArray(out.reresolved)),
      'must either throw or return within one frame — never hang or break',
    );
  });
});
