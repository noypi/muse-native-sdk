# Incremental-Adoption Playbook (FR-DX-006, AC-DX-027)

Scope: brownfield host teams adopting the engine one screen at a time.
Prose-only guide. No pviews code is written or changed by this playbook.

The only engine surface this playbook references is the brownfield-host
hosting interface by name: `EngineView` / `EngineActivity` — the host embeds
one engine-rendered screen inside the otherwise untouched native host app.
Everything else (codemod output, tokens, navigation) plugs into that
hosting interface. If a step cannot name its shippable-host exit criterion,
do not ship it — roll back instead.

Conventions used below:

- **Shippable-host exit criterion** — the observable condition that proves
  the host still builds, launches, and ships with the step in place.
- **Rollback note** — the single revert action that returns the host to the
  previous shippable state. Every step is independently revertible.

---

## Step 0 — Baseline: untouched host (starting point)

Keep the host exactly as it ships today. Record the release build passing
and the smoke path (launch → home screen → one navigation → back) working.

- **Shippable-host exit criterion:** Host builds and ships with zero engine
  screens; smoke path passes on a release build.
- **Rollback note:** No change to roll back; this is the fallback target for
  every later step.

Do not proceed until Step 0 is green.

## Step 1 — Host one EngineView screen (lowest-risk leaf screen)

Pick a single leaf screen with no shared navigation state (e.g. Settings or
About). Embed it via the `EngineView` / `EngineActivity` hosting interface:
the host launches the native host screen container as before, and that
container hosts one `EngineView` rendering the converted screen. All other
screens stay fully native. Convert the screen content with the migration
codemods and keep any unconverted construct on the codemod report path
(manual native fallback), never as a silent inline hack.

- **Shippable-host exit criterion:** Host ships with exactly one screen
  rendered through the `EngineView` / `EngineActivity` hosting interface;
  every other screen is byte-identical native behavior, and the smoke path
  including the converted screen passes on a release build.
- **Rollback note:** Remove the single `EngineView` host binding and restore
  the original native screen; no other screen is touched, so the host
  returns to Step 0 shippable state.

## Step 2 — Shared navigation (host stack owns the route)

Keep the host navigation stack as the owner of routes. The `EngineView`
screen participates as one route: host pushes/pops it like any native
screen, and deep links resolve through the host router first. Do not move
routing ownership into the engine yet — shared nav means the engine screen
reads route params from the host and reports back navigation (pop/result)
through the hosting interface boundary only.

- **Shippable-host exit criterion:** Host ships with mixed-stack navigation
  working: native → `EngineView` screen → native push/pop, back button, and
  one deep link into the engine screen all resolve through the host router
  on a release build.
- **Rollback note:** Deregister the engine route from the host router and
  point the entry points back at the Step 1 leaf (or the original native
  screen); the host stack is untouched otherwise, so navigation returns to
  the Step 1 shippable state.

## Step 3 — Shared tokens (one theme source, engine follows host)

Point the engine screen's tokens at the host theme. Map host colors, type
scale, and spacing to the shared token set so the `EngineView` screen
re-themes when the host theme changes (light/dark). No per-screen hardcoded
colors remain in the converted output; token mismatches are fixed in the
token mapping, never by forking the screen.

- **Shippable-host exit criterion:** Host ships with the `EngineView`
  screen matching host light and dark themes token-for-token; toggling the
  host theme re-themes the engine screen with no host code change and the
  release-build smoke path passes in both themes.
- **Rollback note:** Revert the token mapping to the Step 2 snapshot
  (engine screen keeps its last good hardcoded values); theming returns to
  the Step 2 shippable state with navigation unaffected.

## Step 4 — Expand screen by screen (repeat Steps 1–3 per screen)

Convert one additional screen at a time, in leaf-to-hub order, each repeating
the Step 1 → 2 → 3 sequence through the same `EngineView` /
`EngineActivity` hosting interface. Never convert two screens in one step;
each screen lands, ships, and bakes before the next begins.

- **Shippable-host exit criterion:** Host ships after every single-screen
  landing: full smoke path passes on a release build with N engine screens
  hosted via the `EngineView` / `EngineActivity` hosting interface and all
  remaining screens native.
- **Rollback note:** Revert only the most recent screen to its native
  version (remove its `EngineView` binding); earlier converted screens stay
  shipped, so the host falls back to the previous N−1 shippable state.

---

## Rules (all steps)

1. The host is shippable at the end of every step — that is the exit
   criterion, not a nice-to-have.
2. Reference only the `EngineView` / `EngineActivity` hosting interface by
   name; no pviews internals, no engine code changes.
3. Each step has exactly one rollback note; practice it before claiming the
   step is done.
