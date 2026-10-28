"""Batch 1 (T1+T2+T3) — single-CLI four-tier harness gate (FR-TESTING-001).

Covers FR-TESTING-001 / AC-TESTING-004 / TC-TESTING-004
(`./gradlew :testing:cliAllTiers` runs all four tiers from a single
invocation with machine-readable results CI can gate on):

- T1: ``run_all_tiers(output_dir) -> dict[str, TierResult]`` where
  ``TierResult`` is ``dict`` with keys ``passed: bool``, ``tests: int``,
  ``failures: int``, ``junit_xml: str``; process exit code ``0`` iff
  every tier ``passed`` is true, ``1`` otherwise.
- T2: ``run_cpp_unit(suite_filter: str) -> TierResult``;
  ``run_js_unit(suite_filter: str) -> TierResult`` (empty filter runs
  all suites).
- T3: ``render_snapshot(component: str, variant: str) -> dict`` with
  keys ``passed: bool``, ``diff_pixels: int``,
  ``mean_channel_error: float``, ``png_path: str``;
  ``drive_integration(scenario: str) -> dict`` with keys ``passed:
  bool``, ``steps: int``, ``log_path: str``.

Public boundaries under test: ``testing.cli.run.run_all_tiers``,
``testing.cli.cpp_unit.run_cpp_unit``,
``testing.cli.js_unit.run_js_unit``,
``testing.cli.snapshot.render_snapshot``,
``testing.cli.integration.drive_integration``, plus the
``:testing:cliAllTiers`` case in ``gradlew``.

Hermetic: stdlib unittest only (plus ``unittest.mock`` fakes at the
tier boundary), tmp output dirs, no network/downloads/device/secrets,
no wall-clock or socket reads.

Expected state before implementation: RED — ``testing/cli/*.py`` do
not exist, so every test errors (ModuleNotFoundError) or fails the
file-existence checks.
"""

import os
import tempfile
import unittest
import xml.etree.ElementTree as ET

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI_DIR = os.path.join(REPO_ROOT, "testing", "cli")
RUN_PY = os.path.join(CLI_DIR, "run.py")
INIT_PY = os.path.join(CLI_DIR, "__init__.py")
CPP_PY = os.path.join(CLI_DIR, "cpp_unit.py")
JS_PY = os.path.join(CLI_DIR, "js_unit.py")
SNAP_PY = os.path.join(CLI_DIR, "snapshot.py")
INT_PY = os.path.join(CLI_DIR, "integration.py")
GRADLEW = os.path.join(REPO_ROOT, "gradlew")

# ASSUMPTION (T1.8 shared file): tier keys of run_all_tiers().
EXPECTED_TIERS = ("cpp", "js", "snapshot", "integration")

TIER_RESULT_KEYS = ("passed", "tests", "failures", "junit_xml")


def import_run():
    from testing.cli.run import run_all_tiers
    return run_all_tiers


def assert_tier_result_shape(case, value):
    case.assertIsInstance(value, dict)
    for key in TIER_RESULT_KEYS:
        case.assertIn(key, value)
    case.assertIsInstance(value["passed"], bool)
    case.assertIsInstance(value["tests"], int)
    case.assertIsInstance(value["failures"], int)
    case.assertIsInstance(value["junit_xml"], str)
    case.assertGreaterEqual(value["tests"], 0)
    case.assertGreaterEqual(value["failures"], 0)
    case.assertLessEqual(value["failures"], value["tests"])
    # Consistency: passed iff no failures.
    case.assertEqual(value["passed"], value["failures"] == 0)


class CliFilesTest(unittest.TestCase):
    def test_cli_package_exists(self):
        self.assertTrue(os.path.isfile(INIT_PY))

    def test_run_module_exists(self):
        self.assertTrue(os.path.isfile(RUN_PY))

    def test_cpp_unit_module_exists(self):
        self.assertTrue(os.path.isfile(CPP_PY))

    def test_js_unit_module_exists(self):
        self.assertTrue(os.path.isfile(JS_PY))

    def test_snapshot_module_exists(self):
        self.assertTrue(os.path.isfile(SNAP_PY))

    def test_integration_module_exists(self):
        self.assertTrue(os.path.isfile(INT_PY))

    def test_gradlew_has_cli_all_tiers_task(self):
        with open(GRADLEW, "r", encoding="utf-8") as handle:
            content = handle.read()
        self.assertIn(":testing:cliAllTiers", content)


class RunAllTiersTest(unittest.TestCase):
    def test_returns_four_tiers(self):
        run_all_tiers = import_run()
        with tempfile.TemporaryDirectory() as output_dir:
            result = run_all_tiers(output_dir)
        self.assertIsInstance(result, dict)
        self.assertEqual(len(result), 4)
        for tier in EXPECTED_TIERS:
            self.assertIn(tier, result)

    def test_tier_result_shape(self):
        run_all_tiers = import_run()
        with tempfile.TemporaryDirectory() as output_dir:
            result = run_all_tiers(output_dir)
        for tier, tier_result in result.items():
            with self.subTest(tier=tier):
                assert_tier_result_shape(self, tier_result)

    def test_junit_xml_files_exist_and_parse(self):
        run_all_tiers = import_run()
        with tempfile.TemporaryDirectory() as output_dir:
            result = run_all_tiers(output_dir)
            for tier, tier_result in result.items():
                with self.subTest(tier=tier):
                    self.assertTrue(
                        os.path.isfile(tier_result["junit_xml"]),
                        msg=tier_result["junit_xml"],
                    )
                    tree = ET.parse(tier_result["junit_xml"])
                    self.assertEqual(tree.getroot().tag, "testsuite")

    def test_junit_xml_counts_match_tier_result(self):
        # Machine-readable gate: XML tests/failures agree with the dict.
        run_all_tiers = import_run()
        with tempfile.TemporaryDirectory() as output_dir:
            result = run_all_tiers(output_dir)
            for tier, tier_result in result.items():
                with self.subTest(tier=tier):
                    root = ET.parse(tier_result["junit_xml"]).getroot()
                    self.assertEqual(
                        int(root.get("tests", tier_result["tests"])),
                        tier_result["tests"],
                    )
                    self.assertEqual(
                        int(root.get("failures", tier_result["failures"])),
                        tier_result["failures"],
                    )

    def test_failing_tier_flips_aggregate(self):
        # A deliberately failing tier flips the aggregate to not-passed
        # (CLI exit code 1 path); faked at the tier boundary.
        from unittest import mock

        import testing.cli.run as run_mod

        failing = {
            "passed": False,
            "tests": 3,
            "failures": 1,
            "junit_xml": os.path.join("x", "y.xml"),
        }
        with tempfile.TemporaryDirectory():
            with mock.patch.object(
                run_mod, "run_cpp_unit", return_value=dict(failing)
            ):
                result = run_mod.run_all_tiers(
                    tempfile.mkdtemp(prefix="tiers-")
                )
        self.assertFalse(all(v["passed"] for v in result.values()))
        self.assertFalse(result["cpp"]["passed"])

    def test_output_dir_created(self):
        # Boundary: a missing output_dir is created, not an error.
        run_all_tiers = import_run()
        with tempfile.TemporaryDirectory() as parent:
            output_dir = os.path.join(parent, "tiers-out")
            run_all_tiers(output_dir)
            self.assertTrue(os.path.isdir(output_dir))


class CppJsUnitTest(unittest.TestCase):
    def test_cpp_empty_filter_runs_all_suites(self):
        from testing.cli.cpp_unit import run_cpp_unit

        result = run_cpp_unit("")
        assert_tier_result_shape(self, result)
        self.assertGreater(result["tests"], 0)

    def test_js_empty_filter_runs_all_suites(self):
        from testing.cli.js_unit import run_js_unit

        result = run_js_unit("")
        assert_tier_result_shape(self, result)
        self.assertGreater(result["tests"], 0)

    def test_cpp_junit_xml_parseable(self):
        from testing.cli.cpp_unit import run_cpp_unit

        result = run_cpp_unit("")
        try:
            root = ET.parse(result["junit_xml"]).getroot()
        finally:
            pass
        self.assertEqual(root.tag, "testsuite")

    def test_js_junit_xml_parseable(self):
        from testing.cli.js_unit import run_js_unit

        result = run_js_unit("")
        root = ET.parse(result["junit_xml"]).getroot()
        self.assertEqual(root.tag, "testsuite")

    def test_cpp_filter_narrows_suites(self):
        # ASSUMPTION: suite_filter is a substring match; a selective
        # filter runs a subset of the full run.
        from testing.cli.cpp_unit import run_cpp_unit

        full = run_cpp_unit("")
        names = [
            case.get("classname", "") + "/" + case.get("name", "")
            for case in ET.parse(full["junit_xml"]).getroot().iter("testcase")
        ]
        self.assertTrue(names)
        token = names[0].split("/")[0][: max(1, len(names[0].split("/")[0]) // 2)]
        narrowed = run_cpp_unit(token)
        assert_tier_result_shape(self, narrowed)
        self.assertLessEqual(narrowed["tests"], full["tests"])
        self.assertGreaterEqual(narrowed["tests"], 1)

    def test_js_filter_narrows_suites(self):
        # ASSUMPTION: as for C++ above.
        from testing.cli.js_unit import run_js_unit

        full = run_js_unit("")
        names = [
            case.get("classname", "") + "/" + case.get("name", "")
            for case in ET.parse(full["junit_xml"]).getroot().iter("testcase")
        ]
        self.assertTrue(names)
        token = names[0].split("/")[0][: max(1, len(names[0].split("/")[0]) // 2)]
        narrowed = run_js_unit(token)
        assert_tier_result_shape(self, narrowed)
        self.assertLessEqual(narrowed["tests"], full["tests"])
        self.assertGreaterEqual(narrowed["tests"], 1)

    def test_unknown_filter_matches_nothing_without_raising(self):
        # ASSUMPTION: an unmatched filter returns an empty passing tier
        # rather than raising.
        from testing.cli.cpp_unit import run_cpp_unit
        from testing.cli.js_unit import run_js_unit

        for runner in (run_cpp_unit, run_js_unit):
            with self.subTest(runner=runner.__module__):
                result = runner("__no_such_suite__")
                assert_tier_result_shape(self, result)
                self.assertEqual(result["tests"], 0)
                self.assertTrue(result["passed"])


class SnapshotTest(unittest.TestCase):
    def test_snapshot_result_shape(self):
        from testing.cli.snapshot import render_snapshot

        result = render_snapshot("Button", "light")
        self.assertIsInstance(result, dict)
        for key in ("passed", "diff_pixels", "mean_channel_error", "png_path"):
            self.assertIn(key, result)
        self.assertIsInstance(result["passed"], bool)
        self.assertIsInstance(result["diff_pixels"], int)
        self.assertIsInstance(result["mean_channel_error"], float)
        self.assertIsInstance(result["png_path"], str)

    def test_snapshot_png_written(self):
        from testing.cli.snapshot import render_snapshot

        result = render_snapshot("Button", "light")
        self.assertTrue(os.path.isfile(result["png_path"]))
        self.assertGreater(os.path.getsize(result["png_path"]), 0)

    def test_snapshot_pass_zeroes_diffs(self):
        # ASSUMPTION: a passing render reports zero diffs.
        from testing.cli.snapshot import render_snapshot

        result = render_snapshot("Button", "light")
        self.assertTrue(result["passed"])
        self.assertEqual(result["diff_pixels"], 0)
        self.assertEqual(result["mean_channel_error"], 0.0)

    def test_snapshot_byte_identical_rerun(self):
        from testing.cli.snapshot import render_snapshot

        first = render_snapshot("Button", "light")
        second = render_snapshot("Button", "light")
        with open(first["png_path"], "rb") as handle:
            first_bytes = handle.read()
        with open(second["png_path"], "rb") as handle:
            second_bytes = handle.read()
        self.assertEqual(first_bytes, second_bytes)

    def test_snapshot_variant_changes_output(self):
        # light vs dark variants must not render identical bytes.
        from testing.cli.snapshot import render_snapshot

        light = render_snapshot("Button", "light")
        dark = render_snapshot("Button", "dark")
        with open(light["png_path"], "rb") as handle:
            light_bytes = handle.read()
        with open(dark["png_path"], "rb") as handle:
            dark_bytes = handle.read()
        self.assertNotEqual(light_bytes, dark_bytes)

    def test_snapshot_empty_component_raises(self):
        # ASSUMPTION: empty component is rejected fail-closed.
        from testing.cli.snapshot import render_snapshot

        with self.assertRaises(Exception):
            render_snapshot("", "light")


class IntegrationTest(unittest.TestCase):
    def test_integration_result_shape(self):
        from testing.cli.integration import drive_integration

        result = drive_integration("launch")
        self.assertIsInstance(result, dict)
        for key in ("passed", "steps", "log_path"):
            self.assertIn(key, result)
        self.assertIsInstance(result["passed"], bool)
        self.assertIsInstance(result["steps"], int)
        self.assertIsInstance(result["log_path"], str)

    def test_integration_log_written_with_steps(self):
        from testing.cli.integration import drive_integration

        result = drive_integration("launch")
        self.assertTrue(result["passed"])
        self.assertGreater(result["steps"], 0)
        self.assertTrue(os.path.isfile(result["log_path"]))
        with open(result["log_path"], "r", encoding="utf-8") as handle:
            lines = [line for line in handle.read().splitlines() if line.strip()]
        self.assertEqual(len(lines), result["steps"])

    def test_integration_log_preserves_step_order(self):
        from testing.cli.integration import drive_integration

        result = drive_integration("launch")
        with open(result["log_path"], "r", encoding="utf-8") as handle:
            lines = [line for line in handle.read().splitlines() if line.strip()]
        indices = [int(line.split(":")[0]) for line in lines]
        self.assertEqual(indices, sorted(indices))
        self.assertEqual(indices, list(range(len(lines))))

    def test_integration_byte_identical_rerun(self):
        from testing.cli.integration import drive_integration

        first = drive_integration("launch")
        with open(first["log_path"], "r", encoding="utf-8") as handle:
            first_log = handle.read()
        second = drive_integration("launch")
        with open(second["log_path"], "r", encoding="utf-8") as handle:
            second_log = handle.read()
        self.assertEqual(first_log, second_log)

    def test_integration_unknown_scenario_raises(self):
        # ASSUMPTION: an unscripted scenario is rejected fail-closed.
        from testing.cli.integration import drive_integration

        with self.assertRaises(Exception):
            drive_integration("__no_such_scenario__")


class TierHermeticTest(unittest.TestCase):
    FORBIDDEN = ("socket", "urllib", "http.client", "requests")

    def test_cli_sources_import_no_live_io(self):
        # AC-TESTING-015 support: tier code must not use live sockets.
        for name in (
            "run.py",
            "cpp_unit.py",
            "js_unit.py",
            "snapshot.py",
            "integration.py",
        ):
            path = os.path.join(CLI_DIR, name)
            with open(path, "r", encoding="utf-8") as handle:
                source = handle.read()
            for token in self.FORBIDDEN:
                self.assertNotIn(
                    "import %s" % token, source, msg="%s: %s" % (name, token)
                )


if __name__ == "__main__":
    unittest.main()
