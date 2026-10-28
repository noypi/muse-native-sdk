"""Batch 1 (T1) — 30-minute soak probe harness.

Covers NFR-PERFORMANCE-008 (AC-PERFORMANCE-019): 30-min soak p99 +
battery budget.

Public boundary under test (plan Checklist T1)::

    measure_soak(minutes: int = 30) -> SoakReport(
        p99_ms: float, drain_mah: float, jank_cliff: bool)

``measure_soak`` lives at ``benchmark.soak.run`` and raises
``ThermalError(reason: str)`` when thermal control is unavailable
(fail closed, never crashes).

Hermetic: stdlib unittest only, no network/secrets/services.
Unavailable-control behavior is asserted via ThermalError shape with
``MUSE_THERMAL_CONTROL=unavailable`` set in-process (restored after).

Expected state before implementation: RED — ``benchmark/soak/run.py``
does not exist, so every test fails (ModuleNotFoundError).
"""

import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOAK_RUN = os.path.join(REPO_ROOT, "benchmark", "soak", "run.py")

# ASSUMPTION: "p99 within budget" means the 8 ms complex-frame budget
# from NFR-PERFORMANCE-003 (plan T1 detail).
COMPLEX_P99_MAX_MS = 8.0


def import_soak_run():
    """Import the soak harness through its public boundary."""
    from benchmark.soak.run import ThermalError, measure_soak
    return ThermalError, measure_soak


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


class SoakReportTest(unittest.TestCase):
    def test_harness_module_exists(self):
        self.assertTrue(os.path.isfile(SOAK_RUN))

    def test_default_window_returns_report(self):
        # AC-PERFORMANCE-019: default 30-min window.
        _, measure_soak = import_soak_run()
        report = measure_soak()
        for field in ("p99_ms", "drain_mah", "jank_cliff"):
            self.assertTrue(hasattr(report, field), field)

    def test_default_window_p99_within_budget(self):
        _, measure_soak = import_soak_run()
        report = measure_soak()
        self.assertGreaterEqual(report.p99_ms, 0)
        self.assertLessEqual(report.p99_ms, COMPLEX_P99_MAX_MS)

    def test_no_jank_cliff(self):
        # AC-PERFORMANCE-019: no jank cliff over the soak window.
        _, measure_soak = import_soak_run()
        report = measure_soak()
        self.assertFalse(report.jank_cliff)

    def test_battery_drain_recorded(self):
        # AC-PERFORMANCE-019: battery budget — drain recorded.
        _, measure_soak = import_soak_run()
        report = measure_soak()
        self.assertGreaterEqual(report.drain_mah, 0)

    def test_explicit_window_returns_report(self):
        _, measure_soak = import_soak_run()
        report = measure_soak(minutes=30)
        self.assertGreaterEqual(report.p99_ms, 0)
        self.assertFalse(report.jank_cliff)

    def test_short_window_never_crashes(self):
        # ASSUMPTION: a shorter window still returns a report, never crashes.
        _, measure_soak = import_soak_run()
        report = measure_soak(minutes=5)
        self.assertGreaterEqual(report.p99_ms, 0)
        self.assertGreaterEqual(report.drain_mah, 0)


class SoakErrorTest(unittest.TestCase):
    def test_unavailable_control_raises_thermal_error(self):
        ThermalError, measure_soak = import_soak_run()
        with self.assertRaises(ThermalError) as ctx:
            run_unavailable(measure_soak)
        self.assertTrue(ctx.exception.reason)

    def test_error_is_exception(self):
        ThermalError, _ = import_soak_run()
        self.assertTrue(issubclass(ThermalError, Exception))

    def test_error_message_does_not_leak_secrets(self):
        ThermalError, _ = import_soak_run()
        exc = ThermalError("thermal control unavailable")
        text = str(exc).lower()
        self.assertNotIn("api_key", text)
        self.assertNotIn("token", text)


if __name__ == "__main__":
    unittest.main()
