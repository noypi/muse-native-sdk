"""Batch 1 (T1+T2) — device-lab workflow pin + hermetic-gate-first checks.

Covers NFR-RUNTIME-002 / AC-RUNTIME-026 / TC-RUNTIME-026 (T1 scope), plus
the hermetic-gate-first half of AC-RUNTIME-026 (T2 scope; T2 additionally
references NFR-RUNTIME-001 / AC-RUNTIME-025 as a required dependency,
referenced only — not implemented here).

Scope is Batches 1+2: AVD pin values, emulator boot presence/order, the
ctest hermetic gate being present, first, and unskippable (Batch 1), plus
strict suite order ime-smoke/ime-inset/touch-smoke/config-change and
fail-closed/masking checks (Batch 2: classes TestSuiteOrder,
TestFailClosed).

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
SUITE_ORDER = ("ime-smoke", "ime-inset", "touch-smoke", "config-change")
SUITE_RUN_RE = re.compile(r"device-lab/run\.sh\s+([^\s&|;'\"]+)")


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


def suite_name_of(step):
    """Return the suite arg of a `device-lab/run.sh <suite>` step, else None."""
    run = step.get("run", "")
    if not isinstance(run, str):
        return None
    match = SUITE_RUN_RE.search(run)
    return match.group(1) if match else None


def masking_markers_in_text(text):
    """Return the list of failure-masking markers found in raw workflow text.

    Checks are line-oriented so YAML block scalars (`run: |`) never match
    the `| true` pipe-mask pattern across line breaks.
    """
    found = []
    lowered = text.lower()
    if "continue-on-error" in lowered:
        found.append("continue-on-error")
    for lineno, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith("#"):
            continue
        if re.search(r"\|\|\s*true\b", line):
            found.append("|| true (line %d)" % lineno)
        if re.search(r"\|\|\s*:", line):
            found.append("|| : (line %d)" % lineno)
        if re.search(r"\|\s*true\b", line):
            found.append("| true (line %d)" % lineno)
    return found


def bypass_conditionals_in_steps(steps):
    """Return step indices whose `if:` bypasses fail-closed semantics."""
    bad = []
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            continue
        condition = step.get("if")
        if not isinstance(condition, str):
            continue
        lowered = condition.lower()
        if "always()" in lowered or "failure()" in lowered:
            bad.append(index)
    return bad


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


class TestSuiteOrder(unittest.TestCase):
    """T3 — the four suites run in order via device-lab/run.sh fail-closed."""

    def test_four_suite_steps_present_exactly_once_each(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        names = [
            suite_name_of(step)
            for step in steps
            if isinstance(step, dict) and is_suite_step(step)
        ]
        for suite in SUITE_ORDER:
            self.assertEqual(
                names.count(suite),
                1,
                "suite %r must appear in exactly one run.sh step, found: %r"
                % (suite, names),
            )
        self.assertEqual(
            len(names),
            len(SUITE_ORDER),
            "unexpected extra run.sh suite steps: %r" % (names,),
        )

    def test_suite_steps_in_exact_order(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        names = [
            suite_name_of(step)
            for step in steps
            if isinstance(step, dict) and is_suite_step(step)
        ]
        self.assertEqual(
            [name for name in names if name in SUITE_ORDER],
            list(SUITE_ORDER),
            "suite steps must run ime-smoke, ime-inset, touch-smoke, "
            "config-change in exactly that order",
        )

    def test_suites_run_after_ctest_gate_and_boot(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        gate = find_step_index(steps, is_ctest_step)
        boot = find_step_index(steps, is_boot_step)
        self.assertIsNotNone(gate, "workflow has no ctest gate step")
        self.assertIsNotNone(boot, "workflow has no boot-wait step")
        for suite_index in find_all_step_indices(steps, is_suite_step):
            self.assertLess(
                gate,
                suite_index,
                "ctest gate must run before every suite step",
            )
            self.assertLess(
                boot,
                suite_index,
                "boot-wait step must precede every suite step",
            )

    def test_suite_steps_have_no_bypass_conditional(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        suite_indices = find_all_step_indices(steps, is_suite_step)
        self.assertTrue(suite_indices, "workflow defines no suite steps")
        for index in suite_indices:
            condition = steps[index].get("if")
            self.assertTrue(
                not isinstance(condition, str)
                or ("always()" not in condition.lower()
                    and "failure()" not in condition.lower()),
                "suite step %d must not bypass failure with `if:` %r"
                % (index, condition),
            )

    def test_suite_run_lines_use_single_suite_arg(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        suite_steps = [
            step
            for step in steps
            if isinstance(step, dict) and is_suite_step(step)
        ]
        self.assertTrue(suite_steps, "workflow defines no suite steps")
        for step in suite_steps:
            self.assertRegex(
                step.get("run", ""),
                r"device-lab/run\.sh\s+(ime-smoke|ime-inset|touch-smoke"
                r"|config-change)\s*$",
                "suite step must be a plain `device-lab/run.sh <suite>` line",
            )


class TestFailClosed(unittest.TestCase):
    """T4 — no failure masking anywhere in the workflow."""

    def test_no_continue_on_error_anywhere(self):
        text = read_workflow(self)
        self.assertNotIn(
            "continue-on-error",
            text.lower(),
            "workflow must not mask failures with continue-on-error",
        )
        data = parse_workflow(self, text)
        for index, step in enumerate(workflow_steps(self, data)):
            self.assertTrue(
                not isinstance(step, dict)
                or "continue-on-error" not in {
                    str(key).lower() for key in step
                },
                "step %d must not set continue-on-error" % index,
            )

    def test_no_shell_masking_operators(self):
        text = read_workflow(self)
        self.assertEqual(
            masking_markers_in_text(text),
            [],
            "workflow must not mask failures with "
            "`|| true`, `|| :`, or `| true`",
        )

    def test_no_always_or_failure_bypass_conditional(self):
        data = parse_workflow(self, read_workflow(self))
        steps = workflow_steps(self, data)
        self.assertEqual(
            bypass_conditionals_in_steps(steps),
            [],
            "no step may bypass failure with if: always()/failure()",
        )

    def test_negative_continue_on_error_copy_is_flagged(self):
        text = read_workflow(self)
        tainted = text.replace(
            "run: device-lab/run.sh ime-smoke",
            "run: device-lab/run.sh ime-smoke\n        continue-on-error: true",
            1,
        )
        self.assertNotEqual(
            tainted, text, "scratch copy must differ from the workflow"
        )
        self.assertNotEqual(
            masking_markers_in_text(tainted),
            [],
            "scratch copy with continue-on-error must be flagged",
        )

    def test_negative_pipe_true_copy_is_flagged(self):
        text = read_workflow(self)
        tainted = text.replace(
            "run: device-lab/run.sh ime-smoke",
            "run: device-lab/run.sh ime-smoke || true",
            1,
        )
        self.assertNotEqual(
            tainted, text, "scratch copy must differ from the workflow"
        )
        self.assertNotEqual(
            masking_markers_in_text(tainted),
            [],
            "scratch copy with `|| true` must be flagged",
        )


if __name__ == "__main__":
    unittest.main()
