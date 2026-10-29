# TC-SAMPLE-038 Result — FR-SAMPLE-013 hands-free 5-step flow (T4)

Method: `verifyHandsFreeFlow()` from `examples/muse-demo/src/abilities/voiceRouter.js`
(T3, staged) against the stub-free direct-first local path via T2
`createMuseMcp({ abilities, control, capabilities })` — no server, no socket, no key.
Date: 2026-10-29. Each step driven by voice utterance alone.

## Steps (voice-only, in order)

| step | utterance | ability | turnId | echo | touchEquivalent (screen/action) | result |
|------|-----------|---------|--------|------|----------------------------------|--------|
| 1 | "open profile settings" | profile.open-settings | turn-flow-001 | numbered-voice+tappable-fallback | profile / tap-settings | pass |
| 2 | "open gallery" | gallery.open | turn-flow-002 | numbered-voice+tappable-fallback | gallery / tap-open | pass |
| 3 | "select this gallery" | gallery.select | turn-flow-003 | numbered-voice+tappable-fallback | gallery / tap-select-gallery | pass |
| 4 | "select this image" | gallery.image.select | turn-flow-004 | numbered-voice+tappable-fallback | gallery / tap-select-image | pass |
| 5 | "close the app" | app.close | turn-flow-005 | numbered-voice+tappable-fallback | home / tap-close | pass |

Machine record:

```json
{
  "steps": [
    { "step": 1, "ability": "profile.open-settings", "turnId": "turn-flow-001", "echo": "numbered-voice+tappable-fallback", "touchEquivalent": true },
    { "step": 2, "ability": "gallery.open", "turnId": "turn-flow-002", "echo": "numbered-voice+tappable-fallback", "touchEquivalent": true },
    { "step": 3, "ability": "gallery.select", "turnId": "turn-flow-003", "echo": "numbered-voice+tappable-fallback", "touchEquivalent": true },
    { "step": 4, "ability": "gallery.image.select", "turnId": "turn-flow-004", "echo": "numbered-voice+tappable-fallback", "touchEquivalent": true },
    { "step": 5, "ability": "app.close", "turnId": "turn-flow-005", "echo": "numbered-voice+tappable-fallback", "touchEquivalent": true }
  ]
}
```

Each step confirmed: `routeUtterance` returned `{ ok: true, ability, turnId }`
matching the calling turn, echo `numbered-voice+tappable-fallback`, and
`touchEquivalent('<ability>')` returned a non-null `{ screen, action }`.

## Unknown utterance (reason fallthrough, noted)

`"blargle wumpus xyz"` → `{ "ok": true, "ability": "reason-fallback", "turnId": "turn-probe-001" }` — pass (never silent).

## Denial check (ungranted ability, named code)

Spoke `"close the app"` (`app.close`) with capabilities `["gallery.open"]`:

```json
{ "ok": false, "code": "capability-denied", "ability": "app.close", "turnId": "turn-probe-002" }
```

Recorded denial: `{ "ok": false, "code": "capability-denied" }` — pass (named denial, no action performed).

## Verdict

```json
{ "denial": { "ok": false, "code": "capability-denied" }, "verdict": "pass" }
```

verdict: **pass** — all 5 voice steps pass with echoes + touch equivalents, unknown utterance falls through to `reason-fallback`, ungranted utterance denied with `capability-denied`.
