# 16 KB Page-Size Device-Matrix + Manual Install/Render Evidence (T4)

Scope: NFR-BUILD-008 (BUILD-004). Covers AC-BUILD-019 / AC-BUILD-020 via
TC-BUILD-019 (external device-matrix) and TC-BUILD-020 (manual). No callable
surface — procedure/evidence record only. No results are claimed here.

## Spec sources (verbatim)

AC-BUILD-019:

> A 16 KB page-size emulator image is part of the device matrix (TEST-003) and the app must launch, render its first frame and mount an asset pack there.

AC-BUILD-020:

> The CI alignment check reports 16 KB alignment for 100 percent of shipped .so files, and the release build installs and renders on a 16 KB page-size emulator.

TC-BUILD-019 — AC-BUILD-019 — `toolchain/page-size` — Method `external`:

> device-matrix

TC-BUILD-020 — AC-BUILD-020 — `toolchain/page-size` — Method `manual`:

> 1. Run the CI alignment check over the release AAB. 2. Confirm 100 percent of shipped .so files report 16 KB alignment. 3. Install the release build on a 16 KB page-size emulator and confirm it renders and mounts an asset pack.

## CI half of AC-BUILD-020 (automated gates)

The automated half of the AC-BUILD-020 claim is covered by the T1/T3 hermetic
gates (repoints of the stale `./gradlew` checks; no Gradle wrapper in repo):

- T1 (TC-BUILD-016 intent, final-artifact input): `node --test toolchain/page-size/verifyElfAlignment.test.js`
- T3 (TC-BUILD-018, final artifact incl. base APK + every dynamic feature module + every prebuilt addon): `node --test toolchain/page-size/artifactAlignmentGate.test.js`

## Manual / external half (to be recorded at release time)

- TC-BUILD-019: run the release build on a 16 KB page-size emulator image that
  is part of the device matrix (TEST-003); confirm the app launches, renders
  its first frame, and mounts an asset pack there. Result: NOT RUN (no device
  claimed).
- TC-BUILD-020 step 1: run the CI alignment check over the release AAB.
  Result: NOT RUN.
- TC-BUILD-020 step 2: confirm 100 percent of shipped .so files report 16 KB
  alignment. Result: NOT RUN.
- TC-BUILD-020 step 3: install the release build on a 16 KB page-size emulator
  and confirm it renders and mounts an asset pack. Result: NOT RUN.

Traceability: every step above is traceable to the TC-019/TC-020 quoted text;
T1+T3 gates are the automated half. Close reconciliation decides Status, not
this record.
