# Release-Symbols + QuickJS Parity Manual Evidence (T14)

Scope: NFR-BUILD-018 (BUILD-014). Covers AC-BUILD-071 via TC-BUILD-071
(manual). No callable surface — procedure/evidence record only. No results
are claimed here.

## Spec sources (verbatim)

Source: `manual-context.local/specs-build-toolchain.md`.

AC-BUILD-071:

> The release artifact contains no validation-layer symbols, and the QuickJS variant boots the same smoke test as the Hermes variant.

TC-BUILD-071 — AC-BUILD-071 — `toolchain/variants` — Method `manual`:

> 1. Install the release artifact and inspect it for validation-layer symbols. 2. Boot the QuickJS variant. 3. Run the same smoke test as the Hermes variant and compare results.

## Automated halves (T10 + T11 gates)

The automated halves of the release-symbols + QuickJS parity claim are
covered by the hermetic gates (repoints of the stale `./gradlew` checks; no
Gradle wrapper in repo):

- T10 (TC-BUILD-067 intent, AC-BUILD-067 symbols audit): `node --test toolchain/variants/releaseVariantAudit.test.js`
- T11 (TC-BUILD-068 intent, AC-BUILD-068 QuickJS smoke): `node --test toolchain/variants/quickJsVariantSmoke.test.js`

## Manual half (to be recorded at release time)

- TC-BUILD-071 step 1: Install the release artifact and inspect it for validation-layer symbols. Result: NOT RUN.
- TC-BUILD-071 step 2: Boot the QuickJS variant. Result: NOT RUN.
- TC-BUILD-071 step 3: Run the same smoke test as the Hermes variant and compare results. Result: NOT RUN.

Traceability: every step above is traceable to the TC-071 quoted text; the
T10 + T11 gates are the automated halves. Close reconciliation decides
Status, not this record.
