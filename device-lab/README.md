# device-lab — emulator-in-CI harness (D1)

Batch 2+3 of BUG-RUNTIME-001. These suites assert the device-only halves of
`SPEC-ENGINE-RUNTIME` v1.8 that the hermetic CTest suite cannot reach by
construction: real IME `InputConnection` composition (T5), real animated
inset tracking (T6), real tap/drag dispatch through the Activity (T7), and
real rotation + background/foreground tree preservation (T8). The hermetic suites
(`text_editing_test`, `ime_inset_test`, `touch_dispatch_test`,
`config_change_test`) stay the fast gate; this harness is additive (`Method:
external`).

## Pinned image (D1, API 29+ floor)

- Default pin (ASSUMPTION — coder's choice within D1; this file records it):
  `system-images;android-29;google_apis;x86_64`, AVD name `device-lab-api29`.
- Floor: `ro.build.version.sdk >= 29`. Both suites enforce it and fail otherwise.
- API 29 is the floor because `WindowInsetsAnimation` / animated-inset sampling
  the T6 suite depends on is only stable from API 29+; older images are rejected
  by the suites, not silently tolerated.

## AVD boot

```sh
sdkmanager "platform-tools" "platforms;android-29" "system-images;android-29;google_apis;x86_64" "emulator"
echo no | avdmanager create avd -n device-lab-api29 -k "system-images;android-29;google_apis;x86_64" --device "pixel"
emulator -avd device-lab-api29 -no-snapshot -no-window -gpu swiftshader_indirect &
adb wait-for-device
adb devices   # must list one emulator
```

## Running

```sh
device-lab/run.sh ime-smoke   # T5 — exit 0 iff composition + caret/selection + clipboard round-trip
device-lab/run.sh ime-inset   # T6 — exit 0 iff inset tracking delta <= 1 frame
device-lab/run.sh touch-smoke   # T7 — exit 0 iff tap dispatches handler + drag scrolls list
device-lab/run.sh config-change # T8 — exit 0 iff rotation + bg/fg preserve tree, no crash/blank
device-lab/run.sh --self-check  # hermetic guard check, green without any device
```

Without an emulator both suites FAIL closed (exit 2, `FAIL: missing Android
harness: ...` naming the missing piece) — never a silent pass. Exit 1 means a
real assertion failed on a connected device (the assertion is named).

## Evidence seams (what the instrumented app must write on-device)

- T5 `ime-smoke` pulls `/sdcard/ime-smoke-result.json`
  (override: `DEVICE_LAB_IME_EVIDENCE_PATH`):
  `{"composition": true, "caret": true, "selection": true, "clipboard": true, "method": "<ime id>"}`.
- T6 `ime-inset` pulls `/sdcard/ime-inset-frames.jsonl`
  (override: `DEVICE_LAB_INSET_EVIDENCE_PATH`), one JSON object per animation
  frame: `{"announced": N, "applied": M, "height": H}` with finite `H >= 0` and
  `|applied - announced| <= 1`. NaN/±Inf heights fail (T2 clamp violated).
- T7 `touch-smoke` pulls `/sdcard/touch-smoke-result.json`
  (override: `DEVICE_LAB_TOUCH_EVIDENCE_PATH`):
  `{"tap": true, "handler": "<component id>", "scroll": true, "scroll_delta": <px>}`.
  `handler` must be a non-empty component id and `scroll_delta` non-zero —
  the platform path (Activity-injected tap/drag) reached the T1 per-host seam.
- T8 `config-change` pulls `/sdcard/config-change-result.json`
  (override: `DEVICE_LAB_CONFIG_EVIDENCE_PATH`):
  `{"rotation": true, "background": true, "foreground": true,
    "tree_intact": true, "surface_live": true, "no_crash": true}`.
  Asserts the T3 liveness semantics from the platform side.

## T8 rotation/background sequence (identical every rerun)

1. Launch the instrumented app on the pinned AVD; wait for first frame.
2. Rotate portrait → landscape (`adb shell content insert ... accelerometer_rotation=0`
   + `adb shell settings put system user_rotation 1`); wait 2 s.
3. Rotate landscape → portrait (`user_rotation 0`); wait 2 s.
4. Background: `adb shell input keyevent KEYCODE_HOME`; wait 2 s
   (config-change-while-backgrounded per T3: surface must stay dead, tree intact).
5. Foreground: relaunch the Activity; wait 2 s (surface live again, tree intact).
6. App writes the evidence JSON; `device-lab/run.sh config-change` pulls it.

## Environment knobs

| Variable | Purpose |
|----------|---------|
| `ANDROID_HOME` / `ANDROID_SDK_ROOT` | SDK location (required) |
| `DEVICE_LAB_ADB` | adb binary override (default: `adb` from PATH; tests force a bogus value to prove the guard) |
| `DEVICE_LAB_IME_EVIDENCE_PATH` | T5 on-device evidence path override |
| `DEVICE_LAB_INSET_EVIDENCE_PATH` | T6 on-device evidence path override |
| `DEVICE_LAB_TOUCH_EVIDENCE_PATH` | T7 on-device evidence path override |
| `DEVICE_LAB_CONFIG_EVIDENCE_PATH` | T8 on-device evidence path override |

## CI (T9)

`.github/workflows/device-lab.yml` boots the pinned AVD (`device-lab-api29`,
API 29, `google_apis`/`x86_64`) via the Android emulator runner and runs
`device-lab/run.sh ime-smoke ime-inset touch-smoke config-change` green.

Job order (fail-closed at every stage):

1. `actions/checkout@v4` on `ubuntu-latest`.
2. Hermetic CTest fast gate first: `cmake -S engine/runtime -B build/runtime`,
   `cmake --build build/runtime`, `ctest --test-dir build/runtime`. A red gate
   fails the workflow without booting the emulator.
3. Device gate via `reactivecircus/android-emulator-runner@v2` (`api-level: 29`,
   `target: google_apis`, `arch: x86_64`, `avd-name: device-lab-api29`,
   `-no-snapshot -no-window -gpu swiftshader_indirect`): `adb wait-for-device`,
   then the four suites in order. Any suite exiting non-zero — including exit 2
   when no device boots — fails the workflow; there is no silent green.

There was no repo Actions convention to match (no `.github/` or other CI config
existed), so the job follows the standard checkout + emulator-runner pattern;
CI-green is pending a real runner (validated here by YAML parse only).

## Q-RUNTIME-001 note

These suites are IME/touch/config behavior and renderer-independent. An
emulator without Vulkan support (the Q-RUNTIME-001 question) can still run them
with `-gpu swiftshader_indirect`; gallery pixel-compare rendering is out of
scope here.

## Expected state without an emulator (this machine)

`device-lab/run.sh --self-check` → GREEN (guards hold).
`device-lab/run.sh ime-smoke` / `ime-inset` / `touch-smoke` / `config-change` → RED, exit 2,
`FAIL: missing Android harness: no emulator/device attached ...`.
