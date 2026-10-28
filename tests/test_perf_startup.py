"""Batch 2 (T3) — startup measurement harness + profile check.

Covers NFR-PERFORMANCE-006 (AC-PERFORMANCE-006) and NFR-PERFORMANCE-009
(AC-PERFORMANCE-020, AC-PERFORMANCE-021, AC-PERFORMANCE-022) per
specs-performance.md: cold/warm/hot starts each have a defined budget and
a phase-attributed trace; Baseline Profile + snapshot verified active in
release; release cold start within the 50-80 ms envelope with per-phase
timings and no unattributed startup work.

Public boundary under test (plan Checklist T3)::

    measure_startup(mode: str) -> StartupReport(cold_ms, warm_ms, hot_ms,
        phases: list[Phase(name, ms)], unattributed_ms)
    StartupError(mode, reason)
    is_profile_active(build_dir: str) -> bool

``measure_startup`` lives at ``benchmark.startup.run``; ``is_profile_active``
lives at ``benchmark/profile-check/check.py`` (hyphenated directory, loaded
by file path). Phase labels come from ``benchmark/startup/phase_trace.py``.

Hermetic: stdlib unittest only, tmp dirs, no network/secrets/services.
Trace/profile inputs are tmp files; the repo tree is only read.

Expected state before implementation: RED — ``benchmark/startup/run.py``
and ``benchmark/profile-check/check.py`` do not exist, so every test fails
(ModuleNotFoundError / missing file).
"""

import importlib.util
import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STARTUP_RUN = os.path.join(REPO_ROOT, "benchmark", "startup", "run.py")
PHASE_TRACE = os.path.join(REPO_ROOT, "benchmark", "startup", "phase_trace.py")
PROFILE_CHECK = os.path.join(REPO_ROOT, "benchmark", "profile-check", "check.py")

# ASSUMPTION: the harness measures the scenarios named by AC-PERFORMANCE-020.
VALID_MODES = ("cold", "warm", "hot")

# ASSUMPTION (plan A4): cold-start envelope 50-80 ms verbatim from
# AC-PERFORMANCE-006 / AC-PERFORMANCE-022.
COLD_MIN_MS = 50.0
COLD_MAX_MS = 80.0


def import_startup_run():
    """Import the startup harness through its public boundary."""
    from benchmark.startup.run import StartupError, measure_startup
    return StartupError, measure_startup


def load_profile_check():
    """Load benchmark/profile-check/check.py by file path (hyphenated dir)."""
    spec = importlib.util.spec_from_file_location(
        "profile_check", PROFILE_CHECK)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_phase_trace():
    """Load benchmark/startup/phase_trace.py by file path."""
    spec = importlib.util.spec_from_file_location(
        "phase_trace", PHASE_TRACE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class StartupReportShapeTest(unittest.TestCase):
    def test_cold_returns_report_with_all_budgets(self):
        _, measure_startup = import_startup_run()
        report = measure_startup("cold")
        self.assertGreater(report.cold_ms, 0)
        self.assertGreater(report.warm_ms, 0)
        self.assertGreater(report.hot_ms, 0)

    def test_warm_mode_returns_report(self):
        _, measure_startup = import_startup_run()
        report = measure_startup("warm")
        self.assertGreater(report.warm_ms, 0)

    def test_hot_mode_returns_report(self):
        _, measure_startup = import_startup_run()
        report = measure_startup("hot")
        self.assertGreater(report.hot_ms, 0)

    def test_cold_within_envelope(self):
        # AC-PERFORMANCE-006 / AC-PERFORMANCE-022: 50-80 ms first frame.
        _, measure_startup = import_startup_run()
        report = measure_startup("cold")
        self.assertGreaterEqual(report.cold_ms, COLD_MIN_MS)
        self.assertLessEqual(report.cold_ms, COLD_MAX_MS)

    def test_phases_present_with_names_and_timings(self):
        # AC-PERFORMANCE-020 / AC-PERFORMANCE-022: per-phase timings.
        _, measure_startup = import_startup_run()
        report = measure_startup("cold")
        self.assertTrue(len(report.phases) > 0)
        for phase in report.phases:
            self.assertTrue(phase.name)
            self.assertGreaterEqual(phase.ms, 0)

    def test_no_unattributed_startup_work(self):
        # AC-PERFORMANCE-022: no unattributed startup work.
        _, measure_startup = import_startup_run()
        report = measure_startup("cold")
        self.assertGreaterEqual(report.unattributed_ms, 0)
        self.assertEqual(report.unattributed_ms, 0)


class StartupErrorTest(unittest.TestCase):
    def test_unknown_mode_raises_startup_error(self):
        StartupError, measure_startup = import_startup_run()
        with self.assertRaises(StartupError):
            measure_startup("bogus")

    def test_empty_mode_raises_startup_error(self):
        StartupError, measure_startup = import_startup_run()
        with self.assertRaises(StartupError):
            measure_startup("")

    def test_path_traversal_mode_rejected_fail_closed(self):
        StartupError, measure_startup = import_startup_run()
        with self.assertRaises(StartupError):
            measure_startup("../../etc/passwd")

    def test_error_carries_mode_and_reason(self):
        StartupError, measure_startup = import_startup_run()
        try:
            measure_startup("bogus")
        except StartupError as exc:
            self.assertEqual(exc.mode, "bogus")
            self.assertTrue(exc.reason)
        else:
            self.fail("StartupError not raised")


class ProfileCheckTest(unittest.TestCase):
    def test_empty_build_dir_profile_not_active(self):
        import tempfile
        module = load_profile_check()
        with tempfile.TemporaryDirectory() as build_dir:
            self.assertFalse(module.is_profile_active(build_dir))

    def test_returns_bool(self):
        import tempfile
        module = load_profile_check()
        with tempfile.TemporaryDirectory() as build_dir:
            self.assertIsInstance(
                module.is_profile_active(build_dir), bool)

    def test_missing_build_dir_not_active(self):
        # Fail closed: a nonexistent dir reports inactive, never raises.
        module = load_profile_check()
        self.assertFalse(module.is_profile_active(
            os.path.join(REPO_ROOT, "does-not-exist-build-dir")))


class PhaseTraceModuleTest(unittest.TestCase):
    def test_phase_trace_module_exists(self):
        # The per-phase breakdown source named by the plan file list.
        self.assertTrue(os.path.isfile(PHASE_TRACE))
        load_phase_trace()


if __name__ == "__main__":
    unittest.main()
