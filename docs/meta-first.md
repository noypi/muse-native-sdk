# Meta-first upstream policy (FR-MUSE-007)

## 1. Exclusivity (RULE-MUSE-006, AC-MUSE-019, AC-MUSE-021)

Per RULE-MUSE-006, every model upstream used by this SDK is Muse-exclusive:
no route, template, example, or document in this repository may declare, route
to, or fall back to non-Muse models. Where this doc needs to refer to anything
outside the allow-list below, it does so only with the generic phrase
"non-Muse models" — never by naming an outside model, vendor, or endpoint.

This exclusivity rule exists for the five FR-MUSE-007 reasons:

1. **Swap-test integrity** — the swap test is only meaningful when every route
   under test resolves to a known Muse upstream; any outside upstream would
   invalidate the comparison.
2. **Security surface** — a closed set of upstreams keeps the egress and
   credential surface reviewable: one vendor, known endpoints, no extra keys.
3. **Provable privacy** — with Muse-only upstreams, the data-flow statement
   ("user media and prompts go to Muse upstreams only") is auditable from
   `proxy/proxy.config.json` plus this doc.
4. **Deterministic contract** — proxy routing, retries, timeouts, and streaming
   modes are specified per allow-listed model, so behavior is reproducible.
5. **Honest story** — the SDK is presented as Meta-first, and the shipped
   configuration matches that claim with no silent substitution.

## 2. Closed allow-list (AC-MUSE-019, AC-MUSE-020)

The only permitted upstream model IDs are these four:

| Model ID | Used by route(s) |
|----------|------------------|
| `sam-3.1` | `segment` |
| `muse-spark-1.3` | `reason`, `structure` |
| `muse-voice-transcribe-1.0` | `transcribe` |
| `muse-image` | `generate-image` |

CI enforces this list: the audit test (`proxy/test/upstream.test.js`)
asserts that every `proxy.config.json` route model is one of the four IDs
above, that the `muse-native.yaml` template (when present) declares only
allow-listed Muse IDs, and that no non-Muse model string appears anywhere in
the scanned config/docs surface (`proxy.config.json`, this doc, and the
template when present). Any occurrence fails the gate closed.

## 3. Fallback rule: spec-change-only (AC-MUSE-021)

There is no runtime fallback to a non-Muse model. If none of the four
allow-listed models can serve a route, the route degrades explicitly (error or
disabled path) instead of substituting another upstream. Adding, removing, or
replacing an upstream model is a spec-change-only operation: it requires an
explicit spec revision and review — never a config edit alone.

## 4. Performance-budget disposition (AC-MUSE-022, SPEC-PERFORMANCE)

Budgets are defined normatively by SPEC-PERFORMANCE and are referenced here by
reference rather than duplicated, so this doc cannot drift from the spec. Per
AC-MUSE-022, every route is recorded in the measured-or-degraded table below:
a route is either measured against its SPEC-PERFORMANCE budget in the
device lab, or it is marked degraded until such measurement exists. At the
time of writing, no device-lab measurements have been recorded, so all routes
are marked unmeasured/degraded pending device-lab.

| Route | Budget (SPEC-PERFORMANCE, by reference) | Status |
|-------|------------------------------------------|--------|
| `segment` | SPEC-PERFORMANCE route budget, by reference | unmeasured — degraded pending device-lab |
| `reason` | SPEC-PERFORMANCE route budget, by reference | unmeasured — degraded pending device-lab |
| `structure` | SPEC-PERFORMANCE route budget, by reference | unmeasured — degraded pending device-lab |
| `transcribe` | SPEC-PERFORMANCE route budget, by reference | unmeasured — degraded pending device-lab |
| `generate-image` | SPEC-PERFORMANCE route budget, by reference | unmeasured — degraded pending device-lab |
