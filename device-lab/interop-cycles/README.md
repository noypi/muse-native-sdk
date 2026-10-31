# device-lab/interop-cycles — interop soak procedure (T8)

Scope: FR-PVIEWS-009, AC-PVIEWS-012, TC-PVIEWS-012 (external / device-lab).

## What this verifies

A Compose screen embedded in the engine **and** an engine surface embedded in
Compose both survive 500 navigation cycles and configuration changes with:

- exactly one lifecycle dispatch per event (`dispatchesPerEvent: 1`),
- no leaked Views (`leakedViews: 0`),
- no duplicated TalkBack nodes (`talkbackDupes: 0`),
- correct z-order for dialogs and popups crossing the boundary
  (`zOrder: correct`).

Both directions are cycled (Compose-in-engine via `attachComposeNode`,
engine-in-Compose via `attachEngineSurface`); dialogs/popups crossing the
boundary keep z-order (constrained by T4 disposal + T5 semantics merge).

## Prerequisites (device-lab env)

- Emulator AVD per `device-lab/README.md` (API 29+ floor), `adb` attached.
- Instrumented app that performs the nav/config cycles and writes the
  evidence XML (coder work — absent, so this procedure currently fails
  closed; see Red state below).
- `INTEROP_CYCLES_EVIDENCE_PATH` optionally retargets the evidence path
  (path only; the verdict validates file content, never the env var).

## Running

```sh
device-lab/interop-cycles/run.sh --cycles 500   # required soak (TC-PVIEWS-012)
device-lab/interop-cycles/run.sh --help         # usage, exit 0
```

Exit codes: `0` all checks pass; `1` missing/malformed evidence or any
failing metric (names the failing check); `2` bad arguments (usage).

## Evidence seam (JUnit XML)

Host-side path: `device-lab/interop-cycles/interop-cycles-result.xml`
(on-device producer writes it; the runner validates the pulled file).
Minimal valid example:

```xml
<testsuite name="interop-cycles" tests="4" failures="0">
  <properties>
    <property name="cycles" value="500"/>
    <property name="dispatchesPerEvent" value="1"/>
    <property name="leakedViews" value="0"/>
    <property name="talkbackDupes" value="0"/>
    <property name="zOrder" value="correct"/>
  </properties>
  <testcase classname="interop" name="composeInEngineCycles"/>
  <testcase classname="interop" name="engineInComposeCycles"/>
  <testcase classname="interop" name="dialogPopupZOrder"/>
  <testcase classname="interop" name="talkbackDedupe"/>
</testsuite>
```

## Boundary / error cases

| Case | Expected |
|------|----------|
| `--cycles` missing value, non-numeric, `< 1`, or unknown flag | exit 2 + usage |
| `--help` / `-h` | exit 0, no device touched |
| evidence file absent | exit 1, `FAIL — missing evidence at <path>` |
| evidence without `<testsuite>` | exit 1, invalid-evidence |
| `cycles` property != requested N | exit 1, names `cycles` |
| `dispatchesPerEvent != 1` (double dispatch) | exit 1, names `dispatchesPerEvent` |
| `leakedViews != 0` | exit 1, names `leakedViews` |
| `talkbackDupes != 0` | exit 1, names `talkbackDupes` |
| `zOrder != correct` | exit 1, names `zOrder` |

Host-side checks (arg validation, XML shape) run without a device; the soak
itself requires device-lab — without evidence the runner reports FAIL, never
SKIP-as-pass.

## Red state (batch-first)

At contract time `device-lab/interop-cycles/` was absent (missing files).
This stub is the test file: until the instrumented app + device run exist,
every invocation without evidence exits 1 (`missing evidence`), i.e. RED for
the expected reason (behavior missing, not test malformed).
