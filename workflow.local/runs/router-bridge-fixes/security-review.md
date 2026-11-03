# Security Review — 2026-11-04
Scope: Batch 2 production diff on branch `run/router-bridge-fixes` (45e8df6 on top of 1235d13): `engine/navigation/include/navigation/router_bridge.h` (+1: `GUARD_DENIED`), `engine/navigation/src/router_bridge.cpp` (+7: guard evaluation in `invoke`, lens descriptor). Code review APPROVED (`review.md`). Test source files NOT opened per role constraint; coverage judged from `unit-tests/T1.md`, `unit-tests/T2.md` Security Coverage sections only.
Verdict: CLEARED

## Attack Surface Introduced or Changed
| Entry point | Reachable by | Sinks reached | Guarded by |
|-------------|--------------|---------------|------------|
| `RouterBridge::invoke("navigate"\|"push", jsonArgs)` — new `guard_` evaluation (`router_bridge.cpp:291-293`) | Any caller that can call `invoke` (JSI bridge caller; trust boundary unchanged — no new transport, route, or privilege level) | `NavGraph::apply` (state commit) — NOT reached on deny | `validate_` hook → `validateParams(DescriptorFor)` → `guard_(route)` fail-closed with `GUARD_DENIED`, commit only after all gates pass |
| `DescriptorFor("lens")` — new required-string-`id` schema (`router_bridge.cpp:247-249`) | Same `invoke` caller via untrusted `jsonArgs.params.id` | None — validated `id` is type/presence-checked then discarded (`op` carries only `routeId`; `graph_.apply` never receives `id`) | `validateParams` deterministic `BAD_ARGS` + no-commit on missing/wrong-typed; mirrors existing `profile` pattern |

No new entry point, no new sink, no new privilege. The diff narrows the attack surface (deny-by-default guard + strict schema where an empty schema previously committed anything / rejected everything for the wrong reason).

## Findings
None — no CRITICAL, HIGH, MEDIUM, LOW, or INFO findings warranted. What was checked to conclude this is under Checks Performed and Clean. Two pre-existing design limitations are recorded under Residual Risk, not as findings against this diff.

## Dependency Audit
| Package | Version | Advisory | Reachable | Note |
|---------|---------|----------|-----------|------|
| — | — | — | — | No dependency added, removed, or bumped in this diff (enum + two inline branches only). No audit command run — nothing to audit. |

## Security Tests Requested
| Control | Requirement | Why it needs a test | Owner |
|---------|-------------|---------------------|-------|
| Guard deny fails closed with `GUARD_DENIED` and zero state commit (navigate + push, allow-commits, unset-allows) | AC-NAV-006/040/042 | Contract pins exist in Batch 1 RED (`unit-tests/T1.md` Security Coverage) but Green run post-fix not yet evidenced in this review's scope | `code-unit-tester` |
| Lens boundary validation: missing-`id` → `BAD_ARGS` no-commit; wrong-typed `id` → `BAD_ARGS` no-commit; valid `id` → commit; profile byte-identical | AC-NAV-017/042 | Contract pins exist in Batch 1 RED (`unit-tests/T2.md` Security Coverage) but Green run post-fix not yet evidenced in this review's scope | `code-unit-tester` |

Count: 2 coverage requirements (both pre-pinned contracts awaiting Green confirmation — not missing-coverage findings against the author).

## Checks Performed and Clean
- **Guard bypass:** denying guard returns `{false,"",GUARD_DENIED}` at `router_bridge.cpp:291-293` *before* `NavOp` construction and `graph_.apply` — no commit path around it on navigate/push. `if (guard_ && !guard_(route))` means null/unset hook allows, which preserves documented pre-gate behavior (header comment `router_bridge.h:39-42`), not a bypass introduced here. Gate order (validate → typed params → guard → commit) matches `RouterFacadeGate::commit` convention cited in review.
- **Untrusted `id` tracing:** `jsonArgs` → `FindParamsBody` → `ParseParamsArgs` (string literal only; int `7` typed as Int) → `validateParams(DescriptorFor("lens"), args)` requires string `id` → deny `BAD_ARGS` on missing/wrong-typed. On valid input the `args` map is dropped — `op.routeId = route` is the only committed value. `id` reaches no filesystem path, shell, SQL, URL fetch, template, log, or outbound sink. No injection class is expressible.
- **Lens/id injection:** schema declares exactly one param; `validateParams` rejects unknown params (`UNKNOWN_PARAM` per `typed_routes.h` contract), so smuggled extra fields deny rather than pass through. No mass-assignment sink exists downstream anyway.
- **State-commit integrity:** every denial (`BAD_ARGS`, `GUARD_DENIED`, `NAV_ERROR`) returns before `graph_.apply`. Deny commits nothing by construction, not by cleanup.
- **Route reflection:** success echoes `"{\"route\":\"" + route + "\"}"` — pre-existing line untouched by this diff; Batch 2 adds no new reflection of attacker input (`id` is never echoed). No new XSS/JSON-injection sink.
- **SAST patterns:** no `system`/`exec`/`popen`, no SQL string building, no path join, no URL fetch/redirect, no deserialization beyond the pre-existing hand-rolled JSON literal parser (untouched), no crypto/randomness, no secrets/tokens in diff, no cookie/session handling, no CORS/header/config change. Hand-rolled parser edge cases (escapes, depth) predate this diff and are unmodified.
- **DoS:** `id` length is bounded by input size with no amplification (single-pass accumulation, same pattern as pre-existing `profile.userId`); the diff adds O(1) branches on the navigate/push path only. No unbounded loop, recursion, regex, decompression, or rate-limit-relevant endpoint introduced.
- **AuthN/authZ placement:** hooks are app-supplied (`setValidateHook`/`setGuardHook`); the bridge enforces them on every navigate/push call site in `invoke` with no TOCTOU window (synchronous check-then-commit, no await between). `restoreFromLifeSave` path untouched.
- **Escalation check on code review:** review raised zero findings; nothing to escalate. The noted throwing-guard propagation matches the existing `RouterFacadeGate::commit` convention (review §Checks) — an exception unwinds without commit (fail-closed on state), so no vulnerability to escalate.

## Residual Risk
- **Guard granularity is route-only (pre-existing design, newly relevant):** `GuardFn = bool(const std::string& routeId)` (`snapshot.h:40`) never receives `params`/`id`, so a guard cannot authorize per-`lens`-`id`. A caller that allows route `lens` allows *every* `id` value. This predates Batch 2 and Batch 2 does not widen it (no signature change), but the new `id`-bearing route makes the limitation matter for the first time: per-object authorization for lens ids, if ever required, must live above this bridge. Confirm with the spec owner whether lens ids need per-value entitlement; if so, that is a follow-up design item, not a defect in this diff.
- **Throwing guard surfaces as exception, not `GUARD_DENIED` (pre-existing convention):** neither this diff nor `RouterFacadeGate::commit` wraps `guard_()` in try/catch. State stays fail-closed (no commit on unwind), but a throwing guard yields a different caller-visible contract than a denying guard. Confirm the JSI boundary translates C++ exceptions safely; no evidence of unsafety found in this diff's scope.
- **Upstream caller trust:** who can reach `invoke` (JS bundle vs. privileged native caller) and whether the JS-side guard re-run can be skipped by calling the bridge directly were not re-verified here — the AC-NAV-042 fail-closed C++ re-run is exactly the control that makes that question moot for state integrity, and it is correctly placed.
