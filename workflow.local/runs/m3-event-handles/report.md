# Endorsement Report — FR-M3-013 Button Event Handles (Batch 2 + Batch 3 T7/T8a)

- Run: `run/m3-event-handles` (post-merge: Batch 2 merged via PR #274 @ `3c8bd73`)
- Scope: AC-M3-032–037 (T6 bridge HALTED by design — excluded, routed to specs-creator)
- Verdict: **ENDORSED for merge so far EXCEPT the T8b gate-green log + this report, both landed here.**

## Batch 2 (merged)

- T4 (event props + fail-closed + parity): `test:manifest` 48/48, `test:events` 11/11.
- T5 (rollout, no `pressed`): `test:tier2` 143/143, `test:button-press` 11/11.
- Reviews: code CHANGES REQUIRED → 5 MAJORS fixed → re-review APPROVE; security CLEARED (0/0/0/0).
- Merge: PR #274 squash @ `3c8bd73`.

## Batch 3 T7 (verified, no changes)

Canonical `button.js` shipped in Batch 2 already implements the AC-M3-036 contract; T7 is verification-only:
`button-press` 11/11 (replay-100x→1, forgery→TypeError, arbitration exactly 1, parity fold==press),
`events` 11/11, `manifest` 33/48 (schema file variants), `tier2` 143/143, real-effect fired.

## Batch 3 T8a (gate now load-bearing)

`button-press.test.ts` +10 TC-M3-038 verts (AC-derived, each `resetButtonTapState()` first, `{ skip: !GATE }`):

| invocation | tests | pass | fail | skipped |
|---|---|---|---|---|
| flag off | 21 | 11 | 0 | 10 |
| `CONFORMANCE_GATE=1` + `--conformance-gate` | 21 | 21 | 0 | 0 |

Full `npm test` 78/78 green. Runner note: `node --test` swallows a bare trailing flag
(child `process.argv` never contains it), so the suite arms on argv OR `CONFORMANCE_GATE=1/true`
OR `npm_config_conformance_gate` — gate invocations must set `CONFORMANCE_GATE=1` alongside the flag.

## T6 status (unchanged)

HALTED, routed to specs-creator: winner-to-handle signature + tapId spelling (BRIDGE-1)
unsettled; no production bridge edit; TC-INPUT-023 device-drag leg EXTERNAL.

## JNI follow-ups triage (FR-INPUT-012 Notes)

- Hermetic-ownable: lease-identity passing shape, GC-lifetime unit pins (compiled-out RED scaffolds exist).
- Device-gated (named unblocks): JNI lease-identity end-to-end (needs live bridge + single-tenant device leg),
  drag/slop/CANCEL tap-path (TC-INPUT-023, needs glass + finger/slop rig).

## Endorsement

AC-M3-037: this report lands now; the conformance suite it mandates is green above
(21/21 gate-armed, 78/78 full). T6/device legs remain explicitly excluded per plan.
