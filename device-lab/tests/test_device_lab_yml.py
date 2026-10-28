"""Batch 1 (T1+T2) — device-lab workflow pin + hermetic-gate-first checks.

Covers NFR-RUNTIME-002 / AC-RUNTIME-026 / TC-RUNTIME-026 (T1 scope), plus
the hermetic-gate-first half of AC-RUNTIME-026 (T2 scope; T2 additionally
references NFR-RUNTIME-001 / AC-RUNTIME-025 as a required dependency,
referenced only — not implemented here).

Scope is Batch 1 ONLY: AVD pin values, emulator boot presence/order, and
the ctest hermetic gate being present, first, and unskippable. Suite order
(T3) and fail-closed/masking checks (T4) are Batch 2 and are NOT asserted
here; where a Batch 1 acceptance mentions suite steps that do not exist
yet, the ordering assertion holds vacuously and is re-checked strictly by
Batch 2.

Hermetic: stdlib unittest only (no pytest import, no network, no device);
static YAML/text scan of `.github/workflows/device-lab.yml`.
Runs via both `python3 device-lab/tests/test_device_lab_yml.py` and
`python3 -m pytest device-lab/tests/test_device_lab_yml.py -v`.

Expected state before implementation: RED — the workflow file does not
exist, so every test fails on the missing file.
"""

import os
import re
import unittest

try:
    import yaml
except ImportError:  # pragma: no cover - PyYAML is expected per plan
    yaml = None

REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
)
WORKFLOW = os.path.join(REPO_ROOT, ".github", "workflows", "device-lab.yml")

AVD_NAME = "device-lab-api29"
CTEST_SELECTOR = "ctest --test-dir engine/runtime/build"
CMAKE_CONFIGURE = "cmake -S engine/runtime -B engine/runtime/build"


def read_workflow(testcase):
    """Return the raw workflow text, or fail the test when it is absent."""
    if not os.path.isfile(WORKFLOW):
        testcase.fail(
            "missing workflow file: .github/workflows/device-lab.yml "
            "(T1 has not created it yet)"
        )
    with open(WORKFLOW, "r", encoding="utf-8") as handle:
        return handle.read()


def parse_workflow(testcase, text):
    """Return the parsed workflow mapping, or fail when YAML is unusable."""
    if yaml is None:
        testcase.fail("PyYAML is required to parse device-lab.yml")
    try:
        data = yaml.safe_load(text)
    except Exception as exc:  # noqa: BLE001 - surface as test failure
        testcase.fail("device-lab.yml does not parse as YAML: %s" % exc)
    if not isinstance(data, dict):
        testcase.fail("device-lab.yml parses to a non-mapping document")
    return data


def workflow_steps(testcase, data):
    """Return the ordered step list of the single device-lab job."""
    jobs = data.get("jobs")
    if not isinstance(jobs, dict) or not jobs:
        testcase.fail("device-lab.yml defines no jobs mapping")
    if len(jobs) != 1:
        testcase.fail(
            "device-lab.yml must define exactly one job, found: %s"
            % sorted(jobs)
        )
    job = next(iter(jobs.values()))
    steps = job.get("steps")
    if not isinstance(steps, list) or not steps:
        testcase.fail("device-lab job defines no steps list")
    return steps


def step_text(step):
    """Concatenate the searchable text fields of one workflow step."""
    parts = []
    for key in ("name", "run", "uses", "with", "if"):
        value = step.get(key)
        if isinstance(value, dict):
            parts.extend("%s=%s" % (k, v) for k, v in value.items())
        elif value is not None:
            parts.append(str(value))
    return "\n".join(parts)


def find_step_index(steps, predicate):
    """Return the index of the first step matching predicate, else None."""
    for index, step in enumerate(steps):
        if isinstance(step, dict) and predicate(step):
            return index
    return None


def find_all_step_indices(steps, predicate):
    """Return the indices of every step matching predicate."""
    return [
        index
        for index, step in enumerate(steps)
        if isinstance(step, dict) and predicate(step)
    ]


def is_suite_step(step):
    """A suite step invokes device-lab/run.sh (owned by FR-RUNTIME-008)."""
    run = step.get("run", "")
    return isinstance(run, str) and "device-lab/run.sh" in run


def is_boot_step(step):
    """A boot step waits for the emulator/device before suites run."""
    text = step_text(step).lower()
    return ("adb wait-for-device" in text) or ("boot-complete" in text)


def is_ctest_step(step):
    """The hermetic gate step runs the pinned ctest selector."""
    run = step.get("run", "")
    return isinstance(run, str) and CTEST_SELECTOR in run


class TestDeviceLabWorkflowPin(unittest.TestCase):
    """T1 — device-lab workflow exists with the pinned emulator image."""

    def test_workflow_file_exists(self):
        self.assertTrue(
            os.path.isfile(WORKFLOW),
            "missing workflow file: .github/workflows/device-lab.yml",
        )

    def test_workflow_parses_as_yaml_with_push_pr_triggers(self):
        data = parse_workflow(self, read_workflow(self))
        triggers = data.get("on", data.get(True))
        self.assertIsNotNone(triggers, "workflow has no `on:` triggers")
        if isinstance(triggers, dict):
            names = set(triggers)
        elif isinstance(triggers, list):
            names = set(triggers)
        else:
            names = {triggers}
        self.assertIn("push", names, "workflow must trigger on push")
        self.assertIn("pull_request", names, "workflow must trigger on PR")

    def test_single_job_runs_on_ubuntu_latest(self):
        data = parse_workflow(self, read_workflow(self))
        jobs = data.get("jobs")
        self.assertIsInstance(jobs, dict, "workflow has no jobs mapping")
        self.assertEqual(len(jobs), 1, "workflow must define one job")
        job = next(iter(jobs.values()))
        self.assertEqual(
            job.get("runs-on"),
            "ubuntu-latest",
            "device-lab job must run on ubuntu-latest",
        )

    def test_avd_name_is_device_lab_api29(self):
        text = read_workflow(self)
        self.assertIn(
            AVD_NAME,
            text,
            "workflow must create AVD named exactly device-lab-api29",
        )

    def test_api_level_is_at_least_29(self):
        text = read_workflow(self)
        match = re.search(r"api-?level\s*[:=]\s*['\"]?(\d+)", text)
        self.assertIsNotNone(match, "workflow sets no api-level pin")
        self.assertGreaterEqual(
            int(match.group(1)),
            29,
            "api-level pin must be >= 29",
        )

    def test_target_is_google_apis(self):
        text = read_workflow(self)
        self.assertIn(
            "google_apis",
            text,
            "workflow must pin target=google_apis",
        )

    def test_arch_is_x86_64(self):
        text = read_workflow(self)
        self.assertIn(
            "x86_64",
            text,
            "workflow must pin arch=x86_64",
        )

    def test_boot_wait_step_present(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        index = find_step_index(steps, is_boot_step)
        self.assertIsNotNone(
            index,
            "workflow must wait for boot (adb wait-for-device / "
            "boot-complete) before any suite step",
        )

    def test_boot_precedes_every_suite_step(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        boot = find_step_index(steps, is_boot_step)
        self.assertIsNotNone(boot, "workflow has no boot-wait step")
        for suite_index in find_all_step_indices(steps, is_suite_step):
            self.assertLess(
                boot,
                suite_index,
                "boot-wait step must precede every suite step",
            )


class TestHermeticGateFirst(unittest.TestCase):
    """T2 — hermetic ctest gate runs first; non-zero fails before suites."""

    def test_ctest_gate_step_present(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        index = find_step_index(steps, is_ctest_step)
        self.assertIsNotNone(
            index,
            "workflow must run `%s`" % CTEST_SELECTOR,
        )

    def test_cmake_configure_step_present(self):
        text = read_workflow(self)
        self.assertIn(
            CMAKE_CONFIGURE,
            text,
            "workflow must configure the host build dir via `%s`"
            % CMAKE_CONFIGURE,
        )

    def test_ctest_before_every_suite_step(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        gate = find_step_index(steps, is_ctest_step)
        self.assertIsNotNone(gate, "workflow has no ctest gate step")
        for suite_index in find_all_step_indices(steps, is_suite_step):
            self.assertLess(
                gate,
                suite_index,
                "ctest gate must run before every suite step",
            )

    def test_ctest_step_has_no_skip_conditional(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        gate = find_step_index(steps, is_ctest_step)
        self.assertIsNotNone(gate, "workflow has no ctest gate step")
        self.assertNotIn(
            "if",
            steps[gate],
            "ctest gate must have no `if:` conditional that could skip it",
        )


if __name__ == "__main__":
    unittest.main()
