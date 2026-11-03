# Code Review — 2026-11-04
Scope: Batch 2 production diff on branch `run/router-bridge-fixes` (45e8df6 on top of 1235d13): `engine/navigation/include/navigation/router_bridge.h` (+1), `engine/navigation/src/router_bridge.cpp` (+7); test pins in `engine/navigation/tests/router_bridge_test.cpp` (+108, Batch 1 RED — quality only)
Verdict: APPROVE

## Findings
None — no BLOCKER, no MAJOR, no MINOR, no NIT warranted. Details under Plan Adherence / checks below.

## Plan Adherence
| Item | Satisfied | Note |
|------|-----------|------|
| T1 (BUG-NAV-001: guard hook, AC-NAV-006/040/042) | yes | `GUARD_DENIED` enumerator added (`router_bridge.h:14`); denying guard returns `{false,"",GUARD_DENIED}` before `graph_.apply` so nothing commits (`router_bridge.cpp:291-293`); null/unset hook allows (`if (guard_ && ...)`), preserving pre-gate behavior |
| T2 (BUG-NAV-002: descriptors, AC-NAV-017/042) | yes | `lens` descriptor `{id: String, required}` added in `DescriptorFor` (`router_bridge.cpp:247-249`), mirroring the existing `profile` pattern; missing-required → `BAD_ARGS` no-commit, valid → commit, wrong-typed → `BAD_ARGS` no-commit, profile byte-identical — exactly the T2 contract |

## Checks
- **Guard-evaluation order (gate convention):** `invoke` runs validate-hook → `validateParams(DescriptorFor)` → guard → `graph_.apply` (`router_bridge.cpp:282-297`). Matches `RouterFacadeGate::commit` order (validateParams → guard, `router_facade_gate.cpp:23-28`). Validate-then-guard confirmed.
- **Spec fidelity:** T1 fail-closed with no commit + exact `GUARD_DENIED` code (AC-NAV-006/040/042); T2 deterministic `BAD_ARGS` + no-commit on missing/wrong-typed, commit on valid, profile preservation (AC-NAV-017/042). No deviation.
- **GR-12 production integrity:** (1) stub scan clean — no TODO/FIXME/placeholder in prod diff; (2) no prod imports from test trees, no `Fake*/Mock*/Stub*` in prod; (3) no new Port; (4) no new dependency/adapter/route — `guard_` member and setters pre-existed, enum + branches wire inline; (5) build/boot log is the tester's lane — unit-green alone not claimed as pass here. No GR-12 gap in this diff.
- **Efficiency / accuracy / durability:** O(1) additions on the navigate/push path only; every return path deterministic (`BAD_ARGS` → `GUARD_DENIED` → `NAV_ERROR` → ok); fail-closed on deny; throwing-guard propagation matches the existing `RouterFacadeGate::commit` convention (direct `guard_(route)` call, neither wraps in try/catch — not a new defect, so no finding).
- **Scope creep:** none — 8 added lines, strictly the two contracts. No signature reshapes, no extra routes, no unrelated refactors.
- **Test quality (Batch 1 RED pins, authorship not judged):** 8 cases cover deny-navigate, deny-push, allow-commits, unset-allows (T1) + missing-denied, valid-commits, wrong-typed-denied (pinned stay-denying), profile-preserved (T2); every denial asserts snapshot unchanged; hermetic (C++17 + navigation headers, in-process lambdas, no network/env/fs/secrets); assumptions (GuardFn signature, `GUARD_DENIED` spelling, `lens` route) are documented inline and match the T1/T2 contracts. No implementation-mirror assertions, no secret fixtures.

## Good
- Minimal surgical diff (+8 prod lines) reusing the existing `DescriptorFor`/`validateParams`/`guard_` seams instead of inventing new ones.
- Deny paths assert graph-unchanged via snapshot comparison — the strongest no-commit evidence available at the public boundary.
- Wrong-typed case honestly pinned as already-denying (wrong reason pre-fix) rather than mislabeled RED.
