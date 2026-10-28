"""Batch 1 (T1) — power-saver probe harness.

Covers NFR-PERFORMANCE-008 (AC-PERFORMANCE-018): power-saver reduces
non-essential work.

Public boundary under test (plan Checklist T1)::

    measure_power_saver(power_save_on: bool) -> PowerSaverReport(
        reduced: bool, deferred: list[str], p99_ms: float)

``measure_power_saver`` lives at ``benchmark.powersaver.run`` and raises
``ThermalError(reason: str)`` when thermal control is unavailable
(fail closed, never crashes).

Hermetic: stdlib unittest only, no network/secrets/services.
Unavailable-control behavior is asserted via ThermalError shape with
``MUSE_THERMAL_CONTROL=unavailable`` set in-process (restored after).

Expected state before implementation: RED — ``benchmark/powersaver/run.py``
does not exist, so every test fails (ModuleNotFoundError).
"""

import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POWERSAVER_RUN = os.path.join(REPO_ROOT, "benchmark", "powersaver", "run.py")

# ASSUMPTION: "p99 within budget" means the 8 ms complex-frame budget
# from NFR-PERFORMANCE-003 (plan T1 detail).
COMPLEX_P99_MAX_MS = 8.0


def import_powersaver_run():
    """Import the power-saver harness through its public boundary."""
    from benchmark.powersaver.run import ThermalError, measure_power_saver
    return ThermalError, measure_power_saver


def run_unavailable(measure, *args, **kwargs):
    """Call ``measure`` with thermal control unavailable (fail-closed)."""
    old = os.environ.get("MUSE_THERMAL_CONTROL")
    os.environ["MUSE_THERMAL_CONTROL"] = "unavailable"
    try:
        return measure(*args, **kwargs)
    finally:
        if old is None:
            del os.environ["MUSE_THERMAL_CONTROL"]
        else:
            os.environ["MUSE_THERMAL_CONTROL"] = old


class PowerSaverReportTest(unittest.TestCase):
    def test_harness_module_exists(self):
        self.assertTrue(os.path.isfile(POWERSAVER_RUN))

    def test_on_reduces_non_essential_work(self):
        # AC-PERFORMANCE-018: power-saver on reduces non-essential work.
        _, measure_power_saver = import_powersaver_run()
        report = measure_power_saver(True)
        self.assertTrue(report.reduced)

    def test_on_defers_work_by_name(self):
        # Deferred background/animation work is named.
        _, measure_power_saver = import_powersaver_run()
        report = measure_power_saver(True)
        self.assertIsInstance(report.deferred, list)
        self.assertGreater(len(report.deferred), 0)
        for name in report.deferred:
            self.assertIsInstance(name, str)
            self.assertTrue(name)

    def test_on_p99_within_budget(self):
        _, measure_power_saver = import_powersaver_run()
        report = measure_power_saver(True)
        self.assertGreaterEqual(report.p99_ms, 0)
        self.assertLessEqual(report.p99_ms, COMPLEX_P99_MAX_MS)

    def test_off_does_not_reduce(self):
        _, measure_power_saver = import_powersaver_run()
        report = measure_power_saver(False)
        self.assertFalse(report.reduced)
        self.assertEqual(report.deferred, [])

    def test_off_p99_within_budget(self):
        _, measure_power_saver = import_powersaver_run()
        report = measure_power_saver(False)
        self.assertGreaterEqual(report.p99_ms, 0)
        self.assertLessEqual(report.p99_ms, COMPLEX_P99_MAX_MS)

    def test_never_crashes_on_either_mode(self):
        # Both modes return a report without raising.
        _, measure_power_saver = import_powersaver_run()
        for mode in (True, False):
            report = measure_power_saver(mode)
            self.assertIsNotNone(report)


class PowerSaverErrorTest(unittest.TestCase):
    def test_unavailable_control_raises_thermal_error_on(self):
        ThermalError, measure_power_saver = import_powersaver_run()
        with self.assertRaises(ThermalError) as ctx:
            run_unavailable(measure_power_saver, True)
        self.assertTrue(ctx.exception.reason)

    def test_unavailable_control_raises_thermal_error_off(self):
        ThermalError, measure_power_saver = import_powersaver_run()
        with self.assertRaises(ThermalError):
            run_unavailable(measure_power_saver, False)

    def test_error_is_exception(self):
        ThermalError, _ = import_powersaver_run()
        self.assertTrue(issubclass(ThermalError, Exception))

    def test_error_message_does_not_leak_secrets(self):
        ThermalError, _ = import_powersaver_run()
        exc = ThermalError("thermal control unavailable")
        text = str(exc).lower()
        self.assertNotIn("api_key", text)
        self.assertNotIn("token", text)


if __name__ == "__main__":
    unittest.main()
