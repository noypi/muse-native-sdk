# Cold-start trace — device-lab procedure (TC-CORE-033 / AC-CORE-033)

Scope: cold-start trace attribution for the deterministic engine bootstrap
(NFR-CORE-004 / NFR-CORE-012). Verifies a cold start attributes all six
bootstrap phases, reports the total first-frame time against the perf
50–80 ms envelope reference (NFR-PERFORMANCE-009), and shows zero
thread/handle events before their declared phases (AC-CORE-033 gate from
T3). Execution is external/manual on a reference device — not
CI-verifiable (plan Risks: Close flips Status only after a human
device-lab run).

Trace source: `StartupTrace collectStartupTrace() noexcept` (see
`engine/core/startup/include/core/startup/trace.h`, implemented in
`engine/core/startup/src/trace.cc`) returning
`struct StartupTrace { PhaseTiming phases[6]; uint64_t firstFrameMs; }`.
`PhaseTiming` is the T1 shape from `core/startup/bootstrap.h`: index,
phase name, and monotonic `[startNs, endNs]`. `firstFrameMs` is the
end-to-end cold-start total in whole milliseconds. The 50–80 ms envelope
numbers are owned by the perf spec (NFR-PERFORMANCE-009); this procedure
records the observed value against them and changes no budget manifest.

Reference-device caveat: the `firstFrameMs` number is meaningful only on
a recorded reference device (model + OS build below). It is not a host
CI gate and must not be compared across different devices, OS builds, or
host machines. Only the attribution properties (six phases present,
monotonic, zero pre-phase events) transfer across machines.

This procedure invokes NO `device-lab/run.sh` suite: `device-lab/run.sh`
accepts only `ime-smoke|ime-inset|touch-smoke|config-change`, so there is
no `startup-trace` suite. The human runs the host build and the trace
binary directly, then performs the envelope check below.

## 1. Exact run

- Host build: `cmake -S engine/core/startup -B <build-dir>`,
  then `cmake --build <build-dir>` (e.g. `<build-dir>` =
  `/tmp/startup-build`). The `startup` static library includes
  `src/trace.cc`; the trace binary is linked against lib `startup`.
- Trace binary: run the trace binary linked against lib `startup` on the
  host to confirm the trace shape (six attributed phases plus
  `firstFrameMs` present), then run the instrumented `device-lab` app
  build on the reference device for the measured cold start.
- Reference device: one recorded device (model + OS build in the run
  record), cold-start capable (app fully stopped, no cached process).
  `adb` connected: `adb devices` lists exactly the reference device.
- Build variant + app entry point: instrumented `device-lab` app build
  that calls `collectStartupTrace()` on cold start and writes the trace
  JSON artifact (see §2). Record the build variant and entry point in
  the run record.

## 2. Trace capture

- `collectStartupTrace()` output is obtained on the device as JSON and
  pulled to the host by the human (no `run.sh` suite involved).
- Required content: all six `phases[i]` attributed by name in fixed
  bootstrap order (`memory/allocator`, `thread pools`, `GPU device`,
  `JsRuntime`, `UI tree`, `first frame`), each with name + `startNs` /
  `endNs` present, monotonic and non-overlapping in order; plus
  `firstFrameMs` total present. No phase slot empty.
- On-device path: the app-specific external files dir
  (`/storage/emulated/0/Android/data/com.muse.devicelab/files/startup-trace/startup-trace.json`).
- Host copy: `device-lab/evidence/startup-trace.json` (one file per run;
  do not overwrite the previous run's file — suffix with the run date).

## 3. Envelope check

- Read `StartupTrace.firstFrameMs` from the pulled trace artifact and
  compare it against the NFR-PERFORMANCE-009 50–80 ms envelope reference
  on this reference device.
- Record the observed value and PASS (within envelope) or FAIL (outside
  envelope) in the run record. The envelope values are a reference, not
  a CI gate — the device run asserts the number (see reference-device
  caveat above).

## 4. Phase-gate check

- Inspect the trace's six `phases[i]` windows and confirm zero
  thread/handle creation events before the declared thread-pools phase
  (index 1, per `creationAllowedForPhase`).
- Any pre-phase creation event is an AC-CORE-033 gate violation: FAIL.

## 5. Repro steps a human can run

Prerequisites:

- Host build green: the startup CTest suites pass (hermetic gate).
- One reference device (record model + OS build), cold-start capable
  (app fully stopped, no cached process).
- `adb` connected: `adb devices` lists exactly the reference device.
- Instrumented `device-lab` app build that calls `collectStartupTrace()`
  on cold start and writes the trace JSON artifact (see §2).

Commands in order:

1. Configure and build the startup suites (hermetic gate — must pass
   before touching the device):
   `cmake -S engine/core/startup -B <build-dir>` then
   `cmake --build <build-dir>`, e.g.
   `cmake -S engine/core/startup -B /tmp/startup-build && cmake --build /tmp/startup-build`
   then `ctest --test-dir <build-dir> --output-on-failure`.
   Confirm every suite passes (bootstrap determinism, pool warmup,
   startup gate). A red gate fails closed: do not run the device steps.
2. Run the trace binary linked against lib `startup` from
   `<build-dir>` and confirm it prints/returns six attributed phases
   plus `firstFrameMs`. A missing phase or missing total fails closed:
   do not run the device steps.
3. Install the instrumented `device-lab` app build on the reference
   device (build variant recorded) and force-stop it so the next launch
   is a cold start: `adb shell am force-stop com.muse.devicelab`.
4. Launch the cold-start entry point, let the app write the on-device
   trace JSON (§2), then pull it to the host, e.g.
   `adb pull /storage/emulated/0/Android/data/com.muse.devicelab/files/startup-trace/startup-trace.json device-lab/evidence/startup-trace-<date>.json`.
5. Envelope check (§3): read `firstFrameMs` from the pulled artifact,
   compare against the 50–80 ms reference, record value + PASS/FAIL.
6. Phase-gate check (§4): confirm zero pre-phase thread/handle events;
   record PASS/FAIL.
7. Repeat steps 3–6 for a second cold start. Confirm both traces
   attribute the same six phases in the same order with the same phase
   names (determinism, AC-CORE-005 carryover).

Expected output:

- Host suites pass; trace binary shows six named phases + `firstFrameMs`.
- Each pulled artifact contains six `phases[i]` (name, `startNs`,
  `endNs`) plus `firstFrameMs`.
- Run record holds observed `firstFrameMs` with envelope PASS/FAIL and
  phase-gate PASS/FAIL per run.

What counts as evidence for Close (all must hold on the reference
device):

- [ ] Both cold-start traces show all six bootstrap phases attributed
      (`memory/allocator`, `thread pools`, `GPU device`, `JsRuntime`,
      `UI tree`, `first frame`), each with name + start/end present,
      monotonic and non-overlapping in order — no phase slot empty.
- [ ] `firstFrameMs` total present in each trace; observed value recorded
      against the 50–80 ms envelope reference.
- [ ] Zero thread/handle creation events before the declared
      thread-pools phase in each trace.
- [ ] Tester name, date, device model + OS build, app build variant +
      commit, and one block per cold-start run: the trace artifact path,
      the six phase names in order, the observed `firstFrameMs` with
      envelope PASS/FAIL, and the phase-gate PASS/FAIL. Both runs
      attached for Close.
