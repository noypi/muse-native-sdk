# device-lab — emulator-in-CI harness (D1)

Batch 2 of BUG-RUNTIME-001. These suites assert the device-only halves of
`SPEC-ENGINE-RUNTIME` v1.8 that the hermetic CTest suite cannot reach by
construction: real IME `InputConnection` composition (T5) and real animated
inset tracking (T6). The hermetic suites (`text_editing_test`,
`ime_inset_test`) stay the fast gate; this harness is additive (`Method:
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

## Environment knobs

| Variable | Purpose |
|----------|---------|
| `ANDROID_HOME` / `ANDROID_SDK_ROOT` | SDK location (required) |
| `DEVICE_LAB_ADB` | adb binary override (default: `adb` from PATH; tests force a bogus value to prove the guard) |
| `DEVICE_LAB_IME_EVIDENCE_PATH` | T5 on-device evidence path override |
| `DEVICE_LAB_INSET_EVIDENCE_PATH` | T6 on-device evidence path override |

## Q-RUNTIME-001 note

These suites are IME/touch/config behavior and renderer-independent. An
emulator without Vulkan support (the Q-RUNTIME-001 question) can still run them
with `-gpu swiftshader_indirect`; gallery pixel-compare rendering is out of
scope here.

## Expected state without an emulator (this machine)

`device-lab/run.sh --self-check` → GREEN (guards hold).
`device-lab/run.sh ime-smoke` / `ime-inset` → RED, exit 2,
`FAIL: missing Android harness: no emulator/device attached ...`.
