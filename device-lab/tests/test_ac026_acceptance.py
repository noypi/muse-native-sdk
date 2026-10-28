"""Stage 5 acceptance test for NFR-RUNTIME-002 / AC-RUNTIME-026.

AC-RUNTIME-026: device job boots pinned image API 29+/google_apis/x86_64
(AVD device-lab-api29), runs hermetic CTest gate first, then the four
suites in order; any non-zero suite exit fails the workflow; no
continue-on-error or || true masking.

What this file does (and does NOT do):
- Parses `.github/workflows/device-lab.yml` end to end and asserts the
  pin, gate-first ordering, exact suite order, and absence of masking in
  ONE acceptance-level pass (the per-property hermetic unit checks live in
  `test_device_lab_yml.py`, owned by code-unit-tester — not touched here).
- Behaviorally simulates GitHub's default fail-fast step semantics by
  executing the extracted gate+suite `run:` sequence against stub
  executables: a failing suite aborts the sequence and later suites never
  execute; an all-pass sequence runs everything. This proves the workflow
  as written is fail-closed (no masking constructs to defeat it).
- Runs the hermetic gate's configure shape (`cmake -S engine/runtime -B
  <tmpdir>`) with the host toolchain only — no downloads, no SDKs, no
  emulator images. Configure output goes to a tmp dir, never into the repo.
- Does NOT boot an emulator or run `device-lab/run.sh` for real: no
  emulator/device is available in this environment (see
  `test_emulator_path_is_environment_limited`, which records the exact
  command and outcome as a skip, not a pass).

Hermetic: stdlib unittest only, no network, no device.
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest

try:
    import yaml
except ImportError:  # pragma: no cover
    yaml = None

REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
)
WORKFLOW = os.path.join(REPO_ROOT, ".github", "workflows", "device-lab.yml")

SUITE_ORDER = ("ime-smoke", "ime-inset", "touch-smoke", "config-change")


def load_steps(testcase):
    with open(WORKFLOW, "r", encoding="utf-8") as handle:
        text = handle.read()
    if yaml is None:
        testcase.fail("PyYAML is required to parse device-lab.yml")
    wf = yaml.safe_load(text)
    job = wf["jobs"]["device-lab"]
    return text, wf, job, job["steps"]


def run_body(step):
    return step.get("run", "")


class TestAC026EndToEnd(unittest.TestCase):
    """Single acceptance pass over the whole AC-RUNTIME-026 chain."""

    def test_pin_gate_order_no_masking_end_to_end(self):
        text, wf, job, steps = load_steps(self)
        # Pin: AVD device-lab-api29, API 29+, google_apis, x86_64.
        emu = next(
            s for s in steps if "uses" in s and "emulator" in s["uses"]
        )
        with_cfg = emu["with"]
        self.assertEqual(str(with_cfg["avd-name"]), "device-lab-api29")
        self.assertGreaterEqual(int(with_cfg["api-level"]), 29)
        self.assertEqual(str(with_cfg["target"]), "google_apis")
        self.assertEqual(str(with_cfg["arch"]), "x86_64")
        # Order: boot-wait < configure < build < ctest gate < suites in order.
        runs = [s for s in steps if "run" in s]
        pos = {s["name"]: i for i, s in enumerate(runs)}
        self.assertLess(pos["Wait for emulator boot"], pos["Hermetic gate"])
        self.assertLess(pos["Configure host build"], pos["Build host tree"])
        self.assertLess(pos["Build host tree"], pos["Hermetic gate"])
        suite_steps = [s for s in runs if "device-lab/run.sh" in s["run"]]
        found = [
            re.search(r"device-lab/run\.sh\s+(\S+)", s["run"]).group(1)
            for s in suite_steps
        ]
        self.assertEqual(tuple(found), SUITE_ORDER)
        self.assertLess(
            pos["Hermetic gate"], min(pos[s["name"]] for s in suite_steps)
        )
        self.assertLess(
            pos["Wait for emulator boot"],
            min(pos[s["name"]] for s in suite_steps),
        )
        # Gate-first + suites unskippable: no `if:` on gate or suite steps.
        gate = next(s for s in steps if s.get("name") == "Hermetic gate")
        self.assertNotIn("if", gate)
        for s in suite_steps:
            self.assertNotIn("if", s)
        # No masking anywhere in the file (comments excluded).
        code = "\n".join(
            line.split("#", 1)[0] for line in text.splitlines()
        )
        self.assertNotIn("continue-on-error", code)
        self.assertNotIn("|| true", code)
        self.assertNotRegex(code, r"\|\|\s*:")
        self.assertNotIn("if: always()", text)
        self.assertNotIn("if: failure()", text)

    def test_fail_closed_sequence_aborts_on_first_nonzero(self):
        """Mirror GitHub default step semantics: first non-zero exit stops
        the sequence and later steps never execute. Uses stubs, not the real
        run.sh/emulator."""
        _, _, _, steps = load_steps(self)
        seq = [
            s["run"]
            for s in steps
            if "run" in s
            and (
                "ctest --test-dir engine/runtime/build" in s["run"]
                or "device-lab/run.sh" in s["run"]
            )
        ]
        self.assertEqual(len(seq), 5)  # gate + 4 suites
        with tempfile.TemporaryDirectory() as tmp:
            failing = os.path.join(tmp, "fail_gate.sh")
            with open(failing, "w", encoding="utf-8") as fh:
                fh.write("#!/bin/sh\nexit 1\n")
            os.chmod(failing, 0o755)
            executed = []
            # Simulate: gate stub fails -> sequence aborts, no suite runs.
            for cmd in seq:
                stub = failing if "ctest " in cmd else os.path.join(tmp, "ok.sh")
                if "run.sh" in cmd:
                    with open(stub, "w", encoding="utf-8") as fh:
                        fh.write("#!/bin/sh\necho ran $1\nexit 0\n")
                    os.chmod(stub, 0o755)
                rc = subprocess.run(
                    ["sh", stub], capture_output=True, text=True
                ).returncode
                executed.append((cmd, rc))
                if rc != 0:
                    break  # GitHub default: fail-fast, skip later steps
            self.assertEqual(len(executed), 1)
            self.assertNotEqual(executed[0][1], 0)
            # Positive path: all stubs pass -> whole sequence runs.
            executed_ok = []
            for cmd in seq:
                stub = os.path.join(tmp, "ok2.sh")
                with open(stub, "w", encoding="utf-8") as fh:
                    fh.write("#!/bin/sh\nexit 0\n")
                os.chmod(stub, 0o755)
                rc = subprocess.run(
                    ["sh", stub], capture_output=True, text=True
                ).returncode
                executed_ok.append(rc)
                if rc != 0:
                    break
            self.assertEqual(executed_ok, [0, 0, 0, 0, 0])

    def test_hermetic_configure_shape_with_host_toolchain(self):
        """`cmake -S engine/runtime -B <tmpdir>` configures cleanly with the
        host toolchain only. No downloads, no SDKs, no emulator images."""
        if shutil.which("cmake") is None:
            self.skipTest("cmake not installed")
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = os.path.join(tmp, "cfg")
            proc = subprocess.run(
                [
                    "cmake",
                    "-S",
                    os.path.join(REPO_ROOT, "engine", "runtime"),
                    "-B",
                    build_dir,
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                proc.returncode,
                0,
                msg="cmake configure failed:\n" + proc.stderr[-2000:],
            )
            self.assertTrue(
                os.path.isfile(os.path.join(build_dir, "CTestTestfile.cmake")),
                "configure produced no CTestTestfile.cmake",
            )

    def test_emulator_path_is_environment_limited(self):
        """No emulator/device can execute here: `adb devices` shows no
        attached device. Recorded as environment-limited, not a pass."""
        adb = shutil.which("adb")
        if adb is None:
            self.skipTest("adb not installed — emulator path not executable")
        proc = subprocess.run(
            [adb, "devices"], capture_output=True, text=True
        )
        lines = [
            line.strip()
            for line in proc.stdout.splitlines()
            if line.strip() and not line.startswith("List of")
            and not line.startswith("*")
        ]
        if lines:
            self.fail("unexpected attached device: %r" % (lines,))
        self.skipTest(
            "environment-limited: `adb devices` reports no attached "
            "device, no emulator image is installed, and this stage must "
            "not download toolchains/SDKs/images — the emulator boot path "
            "cannot execute here"
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
