'use strict';
// TC-LAYOUT-025 (AC-LAYOUT-025) — Batch 3 T9.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T9 `Interface:` line, never to implementation.
// Module resolution: `../m3_states.js` once it lands, else an inline fallback
// stub that throws a GENERIC error so every behavior case below is red until
// the module lands — and the harness itself never crashes.
//
// ASSUMPTION (component sample, pinned in unit-tests/T9.md): 'Button' and
// 'Card' are native matrix components, 'Dialog' is a tier-3 composite; the
// full per-component sweep runs against the T11 matrix once published.
// ASSUMPTION (token-ref shape): a token reference is a dotted path such as
// `color.primary` or `state-layer.opacity.hover`; any literal (`#fff`,
// `rgb(...)`, `16px`, bare numbers/words) fails the shape check.
// ASSUMPTION (consistency): for one state, the sampled components share at
// least one common role token value (same state role → same role token).
// ASSUMPTION (errors): unknown component and unknown state both throw.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../m3_states.js');
} catch {
  // Fallback stub: present so the harness loads; throws a generic error so
  // every behavior assertion below fails (red) until ../m3_states.js lands.
  mod = {
    stateTokens: () => {
      throw new Error('m3 states not implemented');
    },
  };
}
const { stateTokens } = mod;

const STATES = ['enabled', 'hovered', 'focused', 'pressed', 'dragged', 'disabled', 'error'];
const SAMPLE_COMPONENTS = ['Button', 'Card', 'Dialog'];

// Dotted token-reference path: at least one dot, lowercase segments.
// Rejects literals: '#fff', 'rgb(…)', '16px', '0.5', 'red'.
const TOKEN_REF_RE = /^[a-z][a-z0-9-]*(\.[a-z0-9-]+)+$/;

function assertTokenMap(map, component, state) {
  assert.equal(typeof map, 'object', `${component}/${state}: must return a record`);
  const keys = Object.keys(map);
  assert.ok(keys.length > 0, `${component}/${state}: must map at least one token`);
  for (const [k, v] of Object.entries(map)) {
    assert.equal(typeof v, 'string', `${component}/${state}.${k}: value must be a string`);
    assert.match(v, TOKEN_REF_RE, `${component}/${state}.${k}: must be a token reference, got: ${v}`);
  }
}

// ---------------------------------------------------------------------------
// T9 contract:
// `stateTokens(component: string, state: 'enabled' | 'hovered' | 'focused' |
//  'pressed' | 'dragged' | 'disabled' | 'error'): Record<string, string>`
// ---------------------------------------------------------------------------
describe('TC-LAYOUT-025 — states are token-mapped and consistent (AC-LAYOUT-025)', () => {
  for (const component of SAMPLE_COMPONENTS) {
    for (const state of STATES) {
      it(`maps ${component}/${state} to token references only`, () => {
        assertTokenMap(stateTokens(component, state), component, state);
      });
    }
  }

  it('covers all seven states for every sampled component', () => {
    assert.equal(STATES.length, 7);
    for (const component of SAMPLE_COMPONENTS) {
      for (const state of STATES) {
        assertTokenMap(stateTokens(component, state), component, state);
      }
    }
  });

  it('uses role tokens consistently: one state shares a common token across components (ASSUMPTION)', () => {
    for (const state of STATES) {
      const sets = SAMPLE_COMPONENTS.map((c) => new Set(Object.values(stateTokens(c, state))));
      const [first, ...rest] = sets;
      const shared = [...first].filter((t) => rest.every((s) => s.has(t)));
      assert.ok(
        shared.length > 0,
        `state '${state}': expected a shared role token across components, found none`
      );
    }
  });

  it('is deterministic: repeated calls deep-equal', () => {
    assert.deepEqual(stateTokens('Button', 'hovered'), stateTokens('Button', 'hovered'));
    assert.deepEqual(stateTokens('Dialog', 'error'), stateTokens('Dialog', 'error'));
  });

  it('never returns a literal value for any sampled component/state', () => {
    const LITERAL_RE = /^(#|rgb|rgba|hsl|[\d.]+(px|dp|sp|%)?$|[A-Z])/;
    for (const component of SAMPLE_COMPONENTS) {
      for (const state of STATES) {
        for (const v of Object.values(stateTokens(component, state))) {
          assert.ok(!LITERAL_RE.test(v), `${component}/${state}: literal leaked: ${v}`);
        }
      }
    }
  });

  // Guard `!/not implemented/`: the file-local fallback stub throws
  // 'm3 states not implemented' for every input. A contract throw must be
  // distinguishable from "module missing", so these validators reject the
  // stub's message and stay red until ../m3_states.js lands.
  it('throws for an unknown component (ASSUMPTION)', () => {
    assert.throws(
      () => stateTokens('NoSuchComponent', 'hovered'),
      (err) => err instanceof Error && !/not implemented/.test(err.message)
    );
  });

  it('throws for an unknown state (ASSUMPTION)', () => {
    assert.throws(
      () => stateTokens('Button', 'melted'),
      (err) => err instanceof Error && !/not implemented/.test(err.message)
    );
  });

  it('throws for an empty state (ASSUMPTION)', () => {
    assert.throws(
      () => stateTokens('Button', ''),
      (err) => err instanceof Error && !/not implemented/.test(err.message)
    );
  });
});
