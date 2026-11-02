# Device proof — T4b leg, 127.0.0.1:6555 (2026-11-02, BLOCKED — no device)

No emulator reachable: `adb devices -l` empty; `adb connect 127.0.0.1:6555`
→ Connection refused (6562 likewise refused). No install/launch/tap/screencap
was possible; nothing below is claimed from a device.

Hermetic work done in _run (no code/tests/git touched):
- Rebuilt APK: `make apk sign verify` with
  MUSE_NATIVE_HOME=.../prebuilts/_build/build, ANDROID_PLATFORM=.../platforms/android-35
  (android-34 dir absent; android-35 used). sign+verify OK.
- staged==delivery md5: dist/tree.mui.bin == assets/tree.mui.bin ==
  build/staging/assets/tree.mui.bin == APK assets/tree.mui.bin =
  2d5383670e6869e30e5ccbbb446b4940 (50760 bytes).
  Per-ABI libRuntime.so staged==delivery: arm64-v8a 38828eb5…, armeabi-v7a 45972458…,
  x86_64 058c9e80…, x86 1824ab92… all match.
  APK: build/m3-gallery-debug.apk, 62,538,673 bytes, apksigner debug cert OK
  (note: 6562 leg APK was 6,353,841 bytes — size delta from 4-ABI staging).
- Tests as-is: gallery-proof.test.ts 51/51 PASS; button-chrome + native-intercept +
  shell-bridge + nav 89/89 PASS; gallery-proof-advance 10/11, 1 FAIL:
  `proof default advance still targets the NATIVE_ADVANCE_PAGE broadcast entry`
  (expects `muse.native.NATIVE_ADVANCE_PAGE`; proof lib names
  `PROOF_ADVANCE_PAGE` path) — pre-existing, routed to coder, not edited here.

Device legs (Next advances one / Prev never / no crash / content renders):
UNVERIFIED on 6555 — needs a live emulator at 127.0.0.1:6555, then the 6562-leg
procedure (uninstall, install -r, launch, broadcast/tap, screencap, logcat
FATAL scan) re-run with `-s 127.0.0.1:6555`.
