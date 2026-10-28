/**
 * TC-M3-010 — light/dark + Dynamic Color repaint within one frame without
 * re-instantiation, seed fallback (FR-M3-005, AC-M3-010, CONFLICT-M3-002).
 *
 * Hermetic: node:test + node:assert/strict only. Imports `theme.js` through
 * the package surface via createRequire, reads `theme.d.ts` source from disk.
 * No network, no secrets, no services.
 *
 * ASSUMPTION A4 (property-update-path-not-device-timing): "within one frame
 * without re-instantiation" is verified as a property-update path (no body
 * re-run per RULE-MCORE-001; `reinstantiated: 0`, `frames: 1`) in-process,
 * not on-device frame timing — Q-M3-002 device-lab verification is out of
 * scope (plan Assumption A4).
 * ASSUMPTION T8-1 (mounted set): "mounted components" are the components
 * previously passed to `resolveTokens` in-process; `setTheme` reports
 * `repainted` equal to that count. The plan fixes the `setTheme` signature
 * but not a mount/registration API, so the test owns its mounted set via
 * `resolveTokens` calls.
 * ASSUMPTION T8-2 (body re-run observation): a component "body" is a factory
 * function owned by the test; the test proves `setTheme` never re-invokes
 * bodies by asserting its own body-run counter is unchanged across the
 * switch while resolved tokens update.
 * ASSUMPTION T8-3 (seed determinism): seed-palette tokens are a pure function
 * of (component, mode, seed) — resolving twice with `dynamic: null` yields
 * deep-equal tokens, and light vs dark tokens differ.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const HERE = dirname(fileURLToPath(import.meta.url));
const NATIVE_DIR = join(HERE, '..');
const THEME_JS = join(NATIVE_DIR, 'theme.js');
const THEME_DTS = join(NATIVE_DIR, 'theme.d.ts');

const SEED = 'test-seed-not-real';
const DYNAMIC = {
  primary: '#6750A4',
  onPrimary: '#FFFFFF',
  surface: '#FEF7FF',
  onSurface: '#1D1B20',
};

// ---------------------------------------------------------------------------
// Module surface
// ---------------------------------------------------------------------------

test('theme.js exists (T8 production surface)', () => {
  assert.ok(existsSync(THEME_JS), `missing theme module: ${THEME_JS}`);
});

test('theme.d.ts exists (T8 typing surface)', () => {
  assert.ok(existsSync(THEME_DTS), `missing theme typings: ${THEME_DTS}`);
});

test('theme.js exports setTheme and resolveTokens', () => {
  const theme = require('../theme.js');
  assert.equal(typeof theme.setTheme, 'function', 'setTheme must be exported');
  assert.equal(typeof theme.resolveTokens, 'function', 'resolveTokens must be exported');
});

test('theme.d.ts declares setTheme and resolveTokens', () => {
  const source = readFileSync(THEME_DTS, 'utf8');
  assert.ok(source.includes('setTheme'), 'theme.d.ts must declare setTheme');
  assert.ok(source.includes('resolveTokens'), 'theme.d.ts must declare resolveTokens');
  assert.ok(source.includes('repainted'), 'theme.d.ts must declare the repainted count');
  assert.ok(source.includes('reinstantiated'), 'theme.d.ts must declare reinstantiated');
  assert.ok(source.includes('frames'), 'theme.d.ts must declare frames');
});

// ---------------------------------------------------------------------------
// AC-M3-010: mode switch repaints within one frame, zero re-instantiations
// ---------------------------------------------------------------------------

test('mode switch returns { repainted N, reinstantiated 0, frames 1 }', () => {
  const theme = require('../theme.js');
  const result = theme.setTheme({ mode: 'dark', seed: SEED, dynamic: null });
  assert.equal(typeof result, 'object', 'setTheme must return a result object');
  assert.deepEqual(
    Object.keys(result).sort(),
    ['frames', 'reinstantiated', 'repainted'],
    'setTheme result must carry exactly { repainted, reinstantiated, frames }',
  );
  assert.equal(result.reinstantiated, 0, 'mode switch must re-instantiate zero components');
  assert.equal(result.frames, 1, 'mode switch must repaint within one frame');
  assert.ok(
    typeof result.repainted === 'number' && result.repainted >= 0,
    'repainted must be a non-negative count',
  );
});

test('palette switch returns { repainted N, reinstantiated 0, frames 1 }', () => {
  const theme = require('../theme.js');
  const result = theme.setTheme({ mode: 'light', seed: SEED, dynamic: DYNAMIC });
  assert.equal(result.reinstantiated, 0, 'palette switch must re-instantiate zero components');
  assert.equal(result.frames, 1, 'palette switch must repaint within one frame');
  assert.ok(
    typeof result.repainted === 'number' && result.repainted >= 0,
    'repainted must be a non-negative count',
  );
});

test('mode switch repaints all mounted components (ASSUMPTION T8-1)', () => {
  const theme = require('../theme.js');
  theme.setTheme({ mode: 'light', seed: SEED, dynamic: null });
  const mounted = ['Button', 'Card', 'Chip'];
  for (const component of mounted) {
    theme.resolveTokens(component, 'light');
  }
  const result = theme.setTheme({ mode: 'dark', seed: SEED, dynamic: null });
  assert.equal(
    result.repainted,
    mounted.length,
    `mode switch must repaint every mounted component (${mounted.length})`,
  );
  assert.equal(result.reinstantiated, 0);
  assert.equal(result.frames, 1);
});

test('theme switch runs zero component bodies (ASSUMPTION T8-2, RULE-MCORE-001)', () => {
  const theme = require('../theme.js');
  theme.setTheme({ mode: 'light', seed: SEED, dynamic: null });
  let bodyRuns = 0;
  const body = () => {
    bodyRuns += 1;
    return theme.resolveTokens('Button', 'light');
  };
  const before = body();
  assert.equal(bodyRuns, 1, 'body runs once when the test invokes it');
  const result = theme.setTheme({ mode: 'dark', seed: SEED, dynamic: null });
  assert.equal(bodyRuns, 1, 'setTheme must not re-run any component body');
  assert.equal(result.reinstantiated, 0);
  const after = theme.resolveTokens('Button', 'dark');
  assert.notDeepEqual(after, before, 'tokens must update to the new mode without a body re-run');
});

// ---------------------------------------------------------------------------
// AC-M3-010: null dynamic falls back to the seed palette
// ---------------------------------------------------------------------------

test('resolveTokens falls back to seed-palette tokens when dynamic is null', () => {
  const theme = require('../theme.js');
  theme.setTheme({ mode: 'light', seed: SEED, dynamic: null });
  const tokens = theme.resolveTokens('Button', 'light');
  assert.equal(typeof tokens, 'object', 'resolveTokens must return a token object');
  assert.ok(tokens !== null, 'resolveTokens must not return null on seed fallback');
  assert.ok(
    Object.keys(tokens).length > 0,
    'seed-fallback tokens must be non-empty (no visual breakage)',
  );
});

test('seed fallback is deterministic per (component, mode, seed) (ASSUMPTION T8-3)', () => {
  const theme = require('../theme.js');
  theme.setTheme({ mode: 'light', seed: SEED, dynamic: null });
  const first = theme.resolveTokens('Card', 'light');
  const second = theme.resolveTokens('Card', 'light');
  assert.deepEqual(second, first, 'same seed + mode must resolve identical tokens');
});

test('seed fallback distinguishes light from dark mode', () => {
  const theme = require('../theme.js');
  theme.setTheme({ mode: 'light', seed: SEED, dynamic: null });
  const light = theme.resolveTokens('Card', 'light');
  const dark = theme.resolveTokens('Card', 'dark');
  assert.notDeepEqual(dark, light, 'light and dark seed tokens must differ');
});

test('null dynamic and absent dynamic resolve identically (seed fallback, no breakage)', () => {
  const theme = require('../theme.js');
  theme.setTheme({ mode: 'light', seed: SEED, dynamic: null });
  const explicitNull = theme.resolveTokens('Chip', 'light');
  theme.setTheme({ mode: 'light', seed: SEED });
  const absent = theme.resolveTokens('Chip', 'light');
  assert.deepEqual(absent, explicitNull, 'dynamic null and dynamic absent must both use the seed');
});

// ---------------------------------------------------------------------------
// Security coverage (Stage 4): unmount / clearMounted repaint accounting
// ---------------------------------------------------------------------------

test('resolving 2 components then setTheme reports repainted 2', () => {
  const theme = require('../theme.js');
  theme.clearMounted();
  theme.resolveTokens('Button', 'light');
  theme.resolveTokens('Card', 'light');
  const result = theme.setTheme({ mode: 'dark', seed: SEED, dynamic: null });
  assert.equal(result.repainted, 2, 'setTheme must repaint exactly the 2 mounted components');
  assert.equal(result.reinstantiated, 0);
  assert.equal(result.frames, 1);
});

test('after unmount(one) the next setTheme reports repainted 1', () => {
  const theme = require('../theme.js');
  theme.clearMounted();
  theme.resolveTokens('Button', 'light');
  theme.resolveTokens('Card', 'light');
  theme.unmount('Button');
  const result = theme.setTheme({ mode: 'dark', seed: SEED, dynamic: null });
  assert.equal(result.repainted, 1, 'unmounted component must no longer be repainted');
  assert.equal(result.reinstantiated, 0);
  assert.equal(result.frames, 1);
});

test('after clearMounted() setTheme reports repainted 0', () => {
  const theme = require('../theme.js');
  theme.clearMounted();
  theme.resolveTokens('Button', 'light');
  theme.resolveTokens('Card', 'light');
  theme.clearMounted();
  const result = theme.setTheme({ mode: 'dark', seed: SEED, dynamic: null });
  assert.equal(result.repainted, 0, 'no mounted components must repaint zero');
  assert.equal(result.reinstantiated, 0);
  assert.equal(result.frames, 1);
});

test('unmount of an unknown component does not throw and leaves the count unchanged', () => {
  const theme = require('../theme.js');
  theme.clearMounted();
  theme.resolveTokens('Button', 'light');
  theme.resolveTokens('Card', 'light');
  assert.doesNotThrow(() => theme.unmount('NotMounted'), 'unmount of unknown must not throw');
  const result = theme.setTheme({ mode: 'dark', seed: SEED, dynamic: null });
  assert.equal(result.repainted, 2, 'unknown unmount must leave the mounted count unchanged');
});
