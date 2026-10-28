# Token Cascade (FR-LAYOUT-009 / AC-LAYOUT-020)

Cascade order is defined once, here (CONFLICT-M3-002). `resolveToken` in
`engine/layout/tokens/store.js` walks the tiers in this resolution order and
returns the nearest defined tier, reporting it as `source`.

## Resolution order

1. `component` — per-component override (e.g. `ProbeCard`).
2. `role` — the component's role mapping (e.g. `ProbeCard`/`ProbePlain` → `probe-role`).
3. `theme` — the active theme (e.g. `probe-dark`, `probe-light`).
4. `default` — the fallback tier. Unknown or empty token names always resolve
   here and never throw.

## Seed dataset (observable through the public interface)

- Token `t.cascade.probe` is defined at all four tiers: `default-tier` /
  `theme-tier` (`probe-dark`) / `role-tier` (`probe-role`) / `component-tier`
  (`ProbeCard`). Theme `probe-light` defines it as `light-tier` so theme
  variance is observable.
- Component `ProbeCard` maps to `probe-role` and has a component-level entry;
  component `ProbePlain` maps to `probe-role` with no component-level entry
  (isolates the role tier).
- Token `t.cascade.default-only` is defined only at `default`
  (`default-only-value`).

## Manual walk (TC-LAYOUT-020)

1. Open this doc and confirm the tiers above read, in order: component, role,
   theme, default.
2. Call `resolveToken('t.cascade.probe')` → `default`; add theme `'probe-dark'`
   → `theme`; add component `'ProbePlain'` → `role`; switch to `'ProbeCard'`
   → `component`.
3. Call `resolveToken('t.cascade.no-such-token-xyz')` → resolves with
   `source: 'default'`, no throw.

## Token snapshot pin (Q-LAYOUT-001 / A3)

Seed snapshot: `source: layout-seed`, `version: 1`. Upstream M3 Expressive
token updates are pinned snapshots, not live fetches; cadence is reviewed per
release and recorded here when the pin changes.
