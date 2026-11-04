# Isolated Scientist Build — engine + demo (no root builds)

Every scientist that needs numbers from a binary follows this page exactly.
Reads may come from the workspace; every build, install, tap, and
measurement runs isolated. A number from any other setup is `CONFOUNDED`,
never proof.

## 0. One-time env (every build shell)

```bash
export MUSE_NATIVE_THIRD_PARTY_DIR=/media/saif/EXTRA/workspaces/meta-hackathon/third-party
# NDK r27 via Android SDK (adjust path to this machine):
export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/27.2.12479018
export PATH=$ANDROID_NDK_HOME:$PATH
```

Without `MUSE_NATIVE_THIRD_PARTY_DIR` the `engine-android` configure dies
at `ft2build.h` (pre-existing hermetic gap) — do not test through it;
report `BLOCKED (missing D3 env)` with the exact command.

## 1. Own worktree (source isolation, required for any build)

From the main checkout, once per leg (never from inside the worktree):

```bash
git worktree add .worktrees/<slug>-<i> <SHA>
git worktree list
git -C .worktrees/<slug>-<i> rev-parse HEAD
```

Rules: never build in the workspace root, never build in
`.worktrees/.research/.../scientist-i/` (a copied subtree cannot
configure — needs repo root + `third-party/` + NDK). The research dir
keeps report + evidence only; the worktree holds checkout + build.
Prove both with `pwd` + `git worktree list` before the first build.

## 2. Build engine (slow — plan for it, run once per worktree)

```bash
cd .worktrees/<slug>-<i>
make engine-android ABIS="x86_64"   # add ABIs only if the leg needs them
sha256sum build/android-x86_64/libRuntime.so  # log bytes + hash
```

Then install the engine into an SDK layout so the demo Makefile can
consume it (needs `VERSION` 3-line + 4 ABI libs
`android-arm64-v8a/-armeabi-v7a/-x86_64/-x86/libRuntime.so`):

```bash
export MUSE_NATIVE_HOME=<sdk-install>/build   # e.g. $PWD/build-sdk
# populate $MUSE_NATIVE_HOME per repo SDK-install recipe, then:
test -s "$MUSE_NATIVE_HOME/VERSION"
test -s "$MUSE_NATIVE_HOME/android-x86_64/libRuntime.so"
```

`make verify` may fail on the pre-existing `jni_contract`
(`nativeCommitEngineRowText`) gap — record it, do not fix it in a
research leg.

## 3. Build + install demo (per serial, single-tenant)

```bash
cd .worktrees/<slug>-<i>/examples/muse-demo   # or examples/m3-gallery
muse-native build .
make apk && make sign && make verify
sha256sum build/muse-demo-debug.apk           # log APK hash
adb -s <own-serial> install -r build/muse-demo-debug.apk
adb -s <own-serial> shell dumpsys package com.muse.host | grep -i -m2 "versionCode\|lastUpdate"
adb -s <own-serial> shell getprop ro.build.display.id
```

Constraints: `adb -s` to the assigned serial ONLY; one tap/install at a
time per serial (`adb -s <serial> logcat -c` before/after + screencap
deltas); two legs on one serial at once = BLOCKED, not data. Pin =
worktree SHA + base.apk/libRuntime.so sha256 + versionCode/versionName/
lastUpdateTime + display.id. Stale-APK (older than pinned SHA) or
shared-tenant-without-lock numbers are `CONFOUNDED` — discard, re-pin.

The correct app recipe is this Makefile. `device-lab/app` builds
nothing (not in root `settings.gradle`, no `build.gradle`) — do not go
there for an APK.

## 4. Cleanup (mandatory, git-only)

Evidence lives in the research dir; nothing valuable lives only in the
worktree. When the leg is done:

```bash
# from the main checkout, never from inside the worktree:
git worktree remove --force .worktrees/<slug>-<i>
git worktree prune
git worktree list          # entry gone
git status --short         # clean
ls .worktrees/.research/<slug>/round-<n>/   # report + evidence only
```

Never `rm -rf` a worktree, never bare `git clean` without the dir path,
never touch the workspace root, a delivery `_run` tree, or a sibling
leg's dir. Report kept-vs-removed.
