# Play-Track Install + Asset-Pack Manual Evidence (T9)

Scope: NFR-BUILD-010 (BUILD-006). Covers AC-BUILD-030 via TC-BUILD-030
(manual). No callable surface — procedure/evidence record only. No results
are claimed here.

## Spec sources (verbatim)

Source: `manual-context.local/specs-build-toolchain.md`.

AC-BUILD-030:

> A release AAB installs on a Play-internal track with all splits resolved, mounts each asset-pack delivery mode, and the universal APK installs and passes smoke tests.

TC-BUILD-030 — AC-BUILD-030 — `toolchain/packaging` — Method `manual`:

> 1. Upload the release AAB to a Play-internal track. 2. Install on a reference device and confirm all splits resolve. 3. Mount an install-time, fast-follow and on-demand asset pack in turn. 4. Install the universal APK and run the smoke suite.

## Automated half (T8 gate)

The automated half of the packaging claim is covered by the T8 hermetic gate
(substitute for the TC-BUILD-026 `dist/universal-release.apk` file check;
binaries untracked by design):

- T8 (TC-BUILD-026 intent, AC-BUILD-026 fallback declaration): `node --test toolchain/packaging/universalApkGate.test.js`

## Manual half (to be recorded at release time)

- TC-BUILD-030 step 1: Upload the release AAB to a Play-internal track. Result: NOT RUN.
- TC-BUILD-030 step 2: Install on a reference device and confirm all splits resolve. Result: NOT RUN.
- TC-BUILD-030 step 3: Mount an install-time, fast-follow and on-demand asset pack in turn. Result: NOT RUN.
- TC-BUILD-030 step 4: Install the universal APK and run the smoke suite. Result: NOT RUN.

Traceability: every step above is traceable to the TC-030 quoted text; the T8
gate is the automated half. Close reconciliation decides Status, not this
record.
