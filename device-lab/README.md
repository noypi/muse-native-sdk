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
android sdk install "platform-tools" "platforms;android-29" "system-images;android-29;google_apis;x86_64" "emulator"
echo no | avdmanager create avd -n device-lab-api29 -k "system-images;android-29;google_apis;x86_64" --device "pixel"
emulator -avd device-lab-api29 -no-snapshot -no-window -gpu swiftshader_indirect &
adb wait-for-device
adb devices   # must list one emulator
```

## Running

```sh
device-lab/run.sh ime-smoke      # T5 — exit 0 iff composition + caret/selection + clipboard evidence validates
device-lab/run.sh ime-inset      # T6 — exit 0 iff inset frame evidence validates (latency <= 1 frame, show+hide)
device-lab/run.sh touch-smoke    # T7 — exit 0 iff tap/drag evidence validates
device-lab/run.sh config-change  # T8 — exit 0 iff rotation + bg/fg evidence validates
device-lab/run.sh --help         # lists the four suites, exit 0
```

`run.sh` takes exactly one argument, matched byte-exactly (no case folding,
no extra args). Unknown suite, empty suite, missing arg, or extra args exit 2
with `usage: run.sh <suite>`. There is no `--self-check` mode and no
multi-suite dispatcher: one invocation runs one suite
(`device-lab/self_check.sh` was deleted as superseded).

Order of checks inside `run.sh <suite>`:

1. Hermetic gate first: `run.sh` configures/builds `engine/runtime` if needed
   and runs `ctest --test-dir engine/runtime/build` before touching any
   device. A red gate fails closed with exit 1
   (`run.sh: hermetic gate (ctest engine/runtime) failed — refusing device run`)
   without touching a device.
2. Suite dispatch + evidence validation: exit 0 only when the pulled evidence
   content validates; any failure exits 1 with a named cause
   (`run.sh: suite <suite> failed evidence check`, `missing evidence at ...`,
   `invalid evidence json(l): ...`, or the per-suite `*_dryrun`-mirrored
   verdict naming the failing flag/line).

## Evidence seams (what the instrumented app must write on-device)

Defaults live under the app-specific external files dir
(`Activity.getExternalFilesDir`:
`/storage/emulated/0/Android/data/com.muse.devicelab/files/<suite>/<file>`;
no `/sdcard/` root paths are used). Each `DEVICE_LAB_*_EVIDENCE_PATH` override
retargets the on-device path only — the verdict always validates the evidence
file content, never the env var.

- T5 `ime-smoke` validates `ime-smoke-result.json`
  (override: `DEVICE_LAB_IME_SMOKE_EVIDENCE_PATH`):
  `{"composition": true, "caret": true, "selection": true,
    "clipboardCopyPaste": true, "engineHosted": true}` — every flag must be
  JSON boolean `true` (strict; `"true"`/`1`/`null`/missing fail).
- T6 `ime-inset` validates `ime-inset-frames.jsonl`
  (override: `DEVICE_LAB_IME_INSET_EVIDENCE_PATH`), one JSON object per frame
  line: `{"frame": N, "announcedHeight": A, "appliedHeight": P,
  "latencyFrames": L}` with strict-int `frame`/heights `>= 0`,
  `latencyFrames` int in `[0,1]`, and an ordered show+hide cycle (>= 1 show
  frame with `announcedHeight > 0`, then a hide frame with
  `announcedHeight == 0`).
- T7 `touch-smoke` validates `touch-smoke-result.json`
  (override: `DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH`):
  `{"tapDispatched": true, "dragScrolled": true, "engineHosted": true}` —
  strict boolean `true`; missing evidence fails closed.
- T8 `config-change` validates `config-change-result.json`
  (override: `DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH`):
  `{"rotationPreserved": true, "bgFgPreserved": true, "nodeIdsStable": true,
    "engineHosted": true}` — strict boolean `true` (string `"true"` fails).

## T8 rotation/background sequence (identical every rerun)

1. Launch the instrumented app on the pinned AVD; wait for first frame.
2. Rotate portrait → landscape (`adb shell content insert ... accelerometer_rotation=0`
   + `adb shell settings put system user_rotation 1`); wait 2 s.
3. Rotate landscape → portrait (`user_rotation 0`); wait 2 s.
4. Background: `adb shell input keyevent KEYCODE_HOME`; wait 2 s
   (config-change-while-backgrounded per T3: surface must stay dead, tree intact).
5. Foreground: relaunch the Activity; wait 2 s (surface live again, tree intact).
6. App writes the evidence JSON under its external files dir; `run.sh`
   dispatches the per-suite script with the resolved `EVIDENCE_PATH` and then
   validates the pulled evidence content (fail closed on missing/invalid).

## Environment knobs

| Variable | Purpose |
|----------|---------|
| `ANDROID_HOME` / `ANDROID_SDK_ROOT` | SDK location (required) |
| `DEVICE_LAB_ADB` | adb binary override (default: `adb` from PATH) |
| `DEVICE_LAB_IME_SMOKE_EVIDENCE_PATH` | T5 on-device evidence path override (path only) |
| `DEVICE_LAB_IME_INSET_EVIDENCE_PATH` | T6 on-device evidence path override (path only) |
| `DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH` | T7 on-device evidence path override (path only) |
| `DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH` | T8 on-device evidence path override (path only) |

## Hermetic dry-run tests (no device, no network)

- `device-lab/tests/run_dispatch_dryrun.sh` — T4 dispatch contract: `--help`
  lists all four suites, unknown/empty/missing/extra-arg/case-folded suites
  exit 2, evidence defaults to the app-specific external files dir, no
  `/sdcard/` root, no committed APK, hermetic gate runs before any device
  suite (uses a failing stub `adb` to prove dispatch checks need no device).
- `device-lab/tests/evidence_failclosed_acceptance.sh` — T4–T8 fail-closed
  acceptance with tmp-dir fixtures: unknown suite exits 2, `--help` lists
  four suites, `engineHosted:false`/string-`"true"`/bad-latency/missing
  evidence all fail, valid fixtures pass, overrides retarget path only.
- Per-suite dry-runs: `device-lab/ime-smoke/tests/ime_smoke_dryrun.sh`,
  `device-lab/ime-inset/tests/ime_inset_dryrun.sh`,
  `device-lab/touch-smoke/tests/touch_smoke_dryrun.sh`,
  `device-lab/config-change/tests/config_change_dryrun.sh` — each drives its
  suite script via its `DEVICE_LAB_*_EVIDENCE_PATH` override against tmp-dir
  fixtures (a failing stub `adb` proves no device is contacted) plus static
  checks (app-specific external files dir, no `/sdcard/` root, producer
  contract).

## CI

`.github/workflows/device-lab.yml` (push + pull_request, `contents: read`,
`ubuntu-latest`, 30 min timeout) runs one job with separate steps — no
multi-suite dispatcher invocation:

1. `actions/checkout@v4`.
2. Create AVD `device-lab-api29` via
   `reactivecircus/android-emulator-runner@v2` (`api-level: 29`,
   `target: google_apis`, `arch: x86_64`, `force-avd-creation: true`,
   `emulator-options: -no-window -no-audio -no-boot-anim`, script echoes
   `AVD ready`).
3. Wait-for-boot step: `adb wait-for-device`, then poll
   `sys.boot_completed` (up to 60 × 5 s); exits 1 if boot never completes.
4. Hermetic gate as its own steps: `cmake -S engine/runtime -B
   engine/runtime/build`, `cmake --build engine/runtime/build`,
   `ctest --test-dir engine/runtime/build`.
5. Four separate suite steps, one arg each: `device-lab/run.sh ime-smoke`,
   `device-lab/run.sh ime-inset`, `device-lab/run.sh touch-smoke`,
   `device-lab/run.sh config-change`. Any non-zero suite exit fails the
   workflow.

## Run history

T1–T4 hermetic hardening still holds: per-host counters, NaN/Inf clamped to
`0.0f`, bg-config liveness, and 12/12 translation units including
`src/jni_bridge.cpp` from main. The device halves (T5–T8) are pending a real
emulator run.

## Q-RUNTIME-001 note

These suites are IME/touch/config behavior and renderer-independent. An
emulator without Vulkan support (the Q-RUNTIME-001 question) can still run them
with `-gpu swiftshader_indirect`; gallery pixel-compare rendering is out of
scope here.

## Expected state without an emulator (this machine)

`device-lab/run.sh --help` → exit 0, lists the four suites.
`device-lab/run.sh bogus-suite` (or no args) → exit 2, usage error — no
device is contacted for dispatch-level checks.
`device-lab/run.sh ime-smoke` / `ime-inset` / `touch-smoke` / `config-change`
→ the hermetic gate runs first (exit 1 if the host `engine/runtime` build is
red); with a green gate and no emulator attached, the suite fails its
evidence check with exit 1 (`missing evidence at ...`), never a silent pass.
The hermetic dry-run tests above stay green without any device.
