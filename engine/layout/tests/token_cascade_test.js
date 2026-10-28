'use strict';
// TC-LAYOUT-020 (AC-LAYOUT-020) — Batch 2 T4.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T4 `Interface:` line, never to implementation.
// Module resolution: `../tokens/store.js` once it lands, else an inline
// fallback stub that throws, so every behavior case below is red until
// the store lands — and the harness itself never crashes.
//
// SEED ASSUMPTION (pinned in unit-tests/T4.md): the store ships a documented
// seed dataset so the cascade is observable through the public interface:
//   token 't.cascade.probe' defined at all four tiers for theme 'probe-dark',
//     role 'probe-role', component 'ProbeCard' (mapped to 'probe-role') with
//     values 'default-tier' / 'theme-tier' / 'role-tier' / 'component-tier';
//   component 'ProbePlain' mapped to 'probe-role' with NO component-level
//     entry (isolates the role tier); theme 'probe-light' also defines the
//     probe token ('light-tier') for theme-variance checks;
//   token 't.cascade.default-only' defined ONLY at the default tier
//     (value 'default-only-value').

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../tokens/store.js');
} catch {
  // Fallback stub: present so the harness loads; throws so every behavior
  // assertion below fails (red) until ../tokens/store.js lands.
  mod = {
    resolveToken: () => {
      throw new Error('store not implemented');
    },
  };
}
const { resolveToken } = mod;

const SOURCES = ['component', 'role', 'theme', 'default'];

// ---------------------------------------------------------------------------
// T4 contract:
// `resolveToken(name: string, component?: string, theme?: string):
//  { value: string, source: 'component' | 'role' | 'theme' | 'default' }`
// ---------------------------------------------------------------------------
describe('TC-LAYOUT-020 — token cascade resolves component -> role -> theme -> default (AC-LAYOUT-020)', () => {
  it('falls back to the default tier when no component/theme context is given', () => {
    assert.deepEqual(resolveToken('t.cascade.probe'), {
      value: 'default-tier',
      source: 'default',
    });
  });

  it('theme tier wins over default', () => {
    assert.deepEqual(resolveToken('t.cascade.probe', undefined, 'probe-dark'), {
      value: 'theme-tier',
      source: 'theme',
    });
  });

  it('role tier wins over theme (component mapped to a role, no component entry)', () => {
    assert.deepEqual(resolveToken('t.cascade.probe', 'ProbePlain', 'probe-dark'), {
      value: 'role-tier',
      source: 'role',
    });
  });

  it('component tier wins over role, theme, and default', () => {
    assert.deepEqual(resolveToken('t.cascade.probe', 'ProbeCard', 'probe-dark'), {
      value: 'component-tier',
      source: 'component',
    });
  });

  it('a token defined only at default resolves to default regardless of context', () => {
    assert.deepEqual(resolveToken('t.cascade.default-only', 'ProbeCard', 'probe-dark'), {
      value: 'default-only-value',
      source: 'default',
    });
  });

  it('an unknown token falls to default and never throws', () => {
    const out = resolveToken('t.cascade.no-such-token-xyz');
    assert.equal(out.source, 'default');
    assert.equal(typeof out.value, 'string');
  });

  it('an unknown token with full context still falls to default and never throws', () => {
    const out = resolveToken('t.cascade.no-such-token-xyz', 'ProbeCard', 'probe-dark');
    assert.equal(out.source, 'default');
    assert.equal(typeof out.value, 'string');
  });

  it('never throws for an empty token name', () => {
    const out = resolveToken('');
    assert.equal(out.source, 'default');
    assert.equal(typeof out.value, 'string');
  });

  it('source always names the winning tier', () => {
    const calls = [
      resolveToken('t.cascade.probe'),
      resolveToken('t.cascade.probe', undefined, 'probe-dark'),
      resolveToken('t.cascade.probe', 'ProbePlain', 'probe-dark'),
      resolveToken('t.cascade.probe', 'ProbeCard', 'probe-dark'),
      resolveToken('t.cascade.no-such-token-xyz'),
    ];
    for (const out of calls) {
      assert.ok(SOURCES.includes(out.source), `source must be a cascade tier, got ${out.source}`);
      assert.equal(typeof out.value, 'string');
    }
  });

  it('is deterministic: repeated calls return identical results', () => {
    assert.deepEqual(
      resolveToken('t.cascade.probe', 'ProbeCard', 'probe-dark'),
      resolveToken('t.cascade.probe', 'ProbeCard', 'probe-dark'),
    );
  });

  it('theme variance is observable through the cascade', () => {
    const dark = resolveToken('t.cascade.probe', undefined, 'probe-dark');
    const light = resolveToken('t.cascade.probe', undefined, 'probe-light');
    assert.equal(dark.source, 'theme');
    assert.equal(light.source, 'theme');
    assert.notEqual(dark.value, light.value);
  });
});
